#include "oc/pass.h"

#include <stdio.h>
#include <stdlib.h>

#include "passes/passes.h"
/* ---------------------------------------------------------------- config */

void oc_config_init(oc_config *c) {
    c->keys = NULL;
    c->vals = NULL;
    c->n = 0;
    c->cap = 0;
}

int oc_config_set(oc_config *c, const char *key, const char *val) {
    for (size_t i = 0; i < c->n; i++) {
        if (strcmp(c->keys[i], key) == 0) {
            snprintf(c->vals[i], sizeof(c->vals[0]), "%s", val);
            return 1;
        }
    }
    if (c->n == c->cap) {
        size_t cap = c->cap ? c->cap * 2 : 8;
        char(*nk)[64] = (char(*)[64])realloc(c->keys, cap * sizeof(*nk));
        if (!nk)
            return 0;
        c->keys = nk; /* commit keys growth; oversized buffer stays safe */
        char(*nv)[64] = (char(*)[64])realloc(c->vals, cap * sizeof(*nv));
        if (!nv)
            return 0; /* vals realloc failed: original buffer still valid */
        c->vals = nv;
        c->cap = cap;
    }
    snprintf(c->keys[c->n], sizeof(c->keys[0]), "%s", key);
    snprintf(c->vals[c->n], sizeof(c->vals[0]), "%s", val);
    c->n++;
    return 1;
}

long long oc_config_get_int(const oc_config *c, const char *key, long long fallback) {
    /* NULL config is legal (library seam: "no options" = all defaults);
     * found by the WBS 5.1 fuzz harness -- oc_scan_run accepts
     * opt.cfg == NULL and the dup index read through it unguarded. */
    if (!c)
        return fallback;
    for (size_t i = 0; i < c->n; i++) {
        if (strcmp(c->keys[i], key) == 0) {
            char *end = NULL;
            long long v = strtoll(c->vals[i], &end, 10);
            if (end && *end == '\0' && end != c->vals[i])
                return v;
            return fallback;
        }
    }
    return fallback;
}

void oc_config_destroy(oc_config *c) {
    free(c->keys);
    free(c->vals);
    c->keys = NULL;
    c->vals = NULL;
    c->n = 0;
    c->cap = 0;
}

/* ------------------------------------------------------------- registry */

static const oc_pass *const BUILTIN_PASSES[] = {
    &oc_pass_long_function, &oc_pass_todo_comment,   &oc_pass_import_cycle,
    &oc_pass_complexity,    &oc_pass_unused_export,  &oc_pass_unreferenced_module,
    &oc_pass_crap_hotspot,  &oc_pass_duplicate_code, &oc_pass_dep_vuln,
    &oc_pass_licence,       &oc_pass_secret,
};

const oc_pass *oc_pass_lookup(const char *id) {
    for (size_t i = 0; i < sizeof(BUILTIN_PASSES) / sizeof(BUILTIN_PASSES[0]); i++) {
        if (strcmp(BUILTIN_PASSES[i]->id, id) == 0)
            return BUILTIN_PASSES[i];
    }
    return NULL;
}

const oc_pass *const *oc_pass_all(size_t *n) {
    *n = sizeof(BUILTIN_PASSES) / sizeof(BUILTIN_PASSES[0]);
    return BUILTIN_PASSES;
}
