#include "oc/tsconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oc/json.h"
#include "pathutil.h"

/* A per-directory cache entry. EVERYTHING lives in the caller's arena:
 * entries are singly linked, so no heap array can leak and stale
 * pointers from earlier lookups stay valid (no realloc ever moves them).
 * Sprint 2 dev lesson: the first draft heap-allocated the entry array
 * and never freed it (LSan), and an unzeroed set struct crashed the
 * unit test with a garbage pointer. Arena-only storage removes both
 * failure modes structurally. */
typedef struct tsconfig_cache_entry {
    char *dir; /* arena copy of the directory that owns this config */
    oc_tsconfig tc;
    int loaded; /* 0 = no config file exists here (negative cache) */
    struct tsconfig_cache_entry *next;
} tsconfig_cache_entry;

struct oc_tsconfig_set {
    oc_arena *a;
    tsconfig_cache_entry *head;
    size_t n;
};

oc_tsconfig_set *oc_tsconfig_set_new(oc_arena *a) {
    oc_tsconfig_set *s = (oc_tsconfig_set *)oc_arena_alloc(a, sizeof(*s));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->a = a;
    return s;
}

/* Parses one config file into tc. Returns 1 on success, 0 on error (st
 * carries a message the caller may surface as a warning). */
static int tsconfig_parse(oc_tsconfig *tc, oc_arena *a, const char *dir, const char *text,
                          size_t len, oc_status *st) {
    memset(tc, 0, sizeof(*tc));
    oc_json *root = oc_json_parse_lenient(a, text, len, st);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_PACK, "tsconfig in '%s' is not a JSON object", dir);
        return 0;
    }
    const oc_json *co = oc_json_get(root, "compilerOptions");
    if (!co || co->type != OC_JSON_OBJ)
        return 1; /* no compilerOptions: valid but useless config */

    const oc_json *baseUrl = oc_json_get(co, "baseUrl");
    const oc_json *paths = oc_json_get(co, "paths");
    if (!baseUrl && !paths)
        return 1;

    /* base_dir = config dir + baseUrl (or the config dir itself when
     * paths exist without baseUrl -- the legacy TS default). */
    char base[1024];
    if (baseUrl && baseUrl->type == OC_JSON_STR) {
        const char *bu = baseUrl->str;
        if (bu[0] == '/') {
            snprintf(base, sizeof(base), "%s", bu);
        } else {
            if (!oc_path_join(base, sizeof(base), dir, bu)) {
                oc_status_set(st, OC_ERR_PACK, "baseUrl too long in tsconfig at '%s'", dir);
                return 0;
            }
        }
        oc_path_normalize(base);
        tc->has_base_url = 1;
    } else {
        snprintf(base, sizeof(base), "%s", dir);
    }
    tc->base_dir = oc_arena_strdup(a, base);
    if (!tc->base_dir)
        return 0;

    if (!paths || paths->type != OC_JSON_OBJ)
        return 1;
    size_t np = paths->n_fields;
    /* Exact-size arena arrays: no calloc/free pair to forget. */
    tc->pattern = (char **)oc_arena_alloc(a, (np ? np : 1) * sizeof(char *));
    tc->target = (char ***)oc_arena_alloc(a, (np ? np : 1) * sizeof(char **));
    tc->n_target = (size_t *)oc_arena_alloc(a, (np ? np : 1) * sizeof(size_t));
    if (!tc->pattern || !tc->target || !tc->n_target)
        return 0;
    for (size_t i = 0; i < np; i++) {
        const oc_json *tgts = paths->vals[i];
        if (!tgts || tgts->type != OC_JSON_ARR)
            continue;
        size_t nt = tgts->n_items;
        char **tarr = (char **)oc_arena_alloc(a, (nt ? nt : 1) * sizeof(char *));
        if (!tarr)
            return 0;
        size_t w = 0;
        for (size_t j = 0; j < nt; j++) {
            const char *t = oc_json_as_str(tgts->items[j], NULL);
            if (t) {
                tarr[w] = oc_arena_strdup(a, t);
                if (!tarr[w])
                    return 0;
                w++;
            }
        }
        tc->pattern[tc->n_patterns] = oc_arena_strdup(a, paths->keys[i]);
        if (!tc->pattern[tc->n_patterns])
            return 0;
        tc->target[tc->n_patterns] = tarr;
        tc->n_target[tc->n_patterns] = w;
        tc->n_patterns++;
    }
    /* Sort pattern/target/n_target together by pattern length desc
     * (longest-prefix-first rewrite semantics). */
    for (size_t i = 1; i < tc->n_patterns; i++) {
        char *pk = tc->pattern[i];
        char **tv = tc->target[i];
        size_t tn = tc->n_target[i];
        size_t j = i;
        while (j > 0 && strlen(tc->pattern[j - 1]) < strlen(pk)) {
            tc->pattern[j] = tc->pattern[j - 1];
            tc->target[j] = tc->target[j - 1];
            tc->n_target[j] = tc->n_target[j - 1];
            j--;
        }
        tc->pattern[j] = pk;
        tc->target[j] = tv;
        tc->n_target[j] = tn;
    }
    return 1;
}

static const char *const CONFIG_NAMES[] = {"tsconfig.json", "jsconfig.json"};

/* Finds, parses and caches the config for dir (no upward walk). */
static const oc_tsconfig *tsconfig_for_dir(oc_tsconfig_set *s, const char *dir) {
    /* cache lookup (linear; the set is tiny in practice) */
    for (tsconfig_cache_entry *it = s->head; it; it = it->next) {
        if (strcmp(it->dir, dir) == 0)
            return it->loaded ? &it->tc : NULL;
    }
    tsconfig_cache_entry *e = (tsconfig_cache_entry *)oc_arena_alloc(s->a, sizeof(*e));
    if (!e)
        return NULL;
    memset(e, 0, sizeof(*e));
    e->next = s->head;
    s->head = e;
    s->n++;
    e->dir = oc_arena_strdup(s->a, dir);
    if (!e->dir) {
        e->loaded = 0;
        e->dir = "";
        return NULL;
    }

    for (size_t k = 0; k < 2; k++) {
        char cfg[1100];
        if (!oc_path_join(cfg, sizeof(cfg), dir, CONFIG_NAMES[k]))
            continue;
        char *data = NULL;
        size_t len = 0;
        oc_status tmp;
        oc_status_clear(&tmp);
        if (oc_read_file(cfg, &data, &len, &tmp) != OC_OK)
            continue; /* no config here (or unreadable: treat as absent) */
        oc_status_clear(&tmp);
        int ok = tsconfig_parse(&e->tc, s->a, dir, data, len, &tmp);
        free(data);
        if (!ok) {
            /* Broken config: surface once via stderr from the caller side
             * is not available here; fail-open (no aliasing) and keep the
             * negative cache so the message does not repeat per file. */
            e->loaded = 0;
            return NULL;
        }
        e->loaded = 1;
        return &e->tc;
    }
    e->loaded = 0;
    return NULL;
}

const oc_tsconfig *oc_tsconfig_for(oc_tsconfig_set *s, oc_arena *a, const char *from_dir,
                                   const char *root_dir, oc_status *st) {
    (void)a;
    (void)st;
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", from_dir);
    for (int guard = 0; guard < 64; guard++) {
        const oc_tsconfig *tc = tsconfig_for_dir(s, dir);
        if (tc)
            return tc;
        /* Stop at the analysis root (inclusive): a config outside the
         * analysis is invisible by contract. */
        if (strcmp(dir, root_dir) == 0 || strcmp(dir, ".") == 0 || strcmp(dir, "/") == 0)
            return NULL;
        char up[1024];
        oc_path_up(dir, 1, up, sizeof(up));
        if (strcmp(up, dir) == 0)
            return NULL; /* cannot climb further */
        snprintf(dir, sizeof(dir), "%s", up);
    }
    return NULL;
}

size_t oc_tsconfig_rewrite(const oc_tsconfig *tc, const char *spec, char *out, size_t cap) {
    for (size_t i = 0; i < tc->n_patterns; i++) {
        const char *pat = tc->pattern[i];
        size_t pl = strlen(pat);
        int wildcard = pl > 0 && pat[pl - 1] == '*';
        size_t prefix_len = wildcard ? pl - 1 : pl;
        if (wildcard) {
            if (strncmp(spec, pat, prefix_len) != 0)
                continue;
        } else if (strcmp(spec, pat) != 0) {
            continue;
        }
        const char *rest = wildcard ? spec + prefix_len : "";
        for (size_t j = 0; j < tc->n_target[i]; j++) {
            const char *tgt = tc->target[i][j];
            char joined[1024];
            /* Target with '*' gets the captured rest; exact targets are
             * used as-is. Relative targets sit under base_dir. */
            const char *star = strchr(tgt, '*');
            char subst[1024];
            if (star) {
                size_t head = (size_t)(star - tgt);
                if (snprintf(subst, sizeof(subst), "%.*s%s%s", (int)head, tgt, rest, star + 1) >=
                    (int)sizeof(subst))
                    continue;
            } else {
                snprintf(subst, sizeof(subst), "%s", tgt);
            }
            if (subst[0] == '/') {
                snprintf(joined, sizeof(joined), "%s", subst);
            } else if (!oc_path_join(joined, sizeof(joined), tc->base_dir, subst)) {
                continue;
            }
            oc_path_normalize(joined);
            size_t n = strlen(joined);
            if (n + 1 > cap)
                continue;
            memcpy(out, joined, n + 1);
            return n;
        }
    }
    return 0;
}
