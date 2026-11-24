#include "oc/finding.h"

#include <stdlib.h>

const char *oc_severity_str(oc_severity s) {
    switch (s) {
    case OC_SEV_INFO:
        return "info";
    case OC_SEV_WARN:
        return "warn";
    case OC_SEV_ERROR:
        return "error";
    }
    return "info";
}

int oc_severity_from_str(const char *s, oc_severity *out) {
    if (strcmp(s, "info") == 0) {
        *out = OC_SEV_INFO;
        return 1;
    }
    if (strcmp(s, "warn") == 0) {
        *out = OC_SEV_WARN;
        return 1;
    }
    if (strcmp(s, "error") == 0) {
        *out = OC_SEV_ERROR;
        return 1;
    }
    return 0;
}

void oc_findings_init(oc_finding_vec *v) {
    v->items = NULL;
    v->n = 0;
    v->cap = 0;
}

static int findings_reserve(oc_finding_vec *v, size_t need) {
    if (v->n + need <= v->cap)
        return 1;
    size_t cap = v->cap ? v->cap : 32;
    while (cap < v->n + need)
        cap *= 2;
    oc_finding *items = (oc_finding *)realloc(v->items, cap * sizeof(oc_finding));
    if (!items)
        return 0;
    v->items = items;
    v->cap = cap;
    return 1;
}

int oc_findings_push(oc_finding_vec *v, oc_arena *a, const oc_finding_spec *spec) {
    (void)a; /* spec strings are already arena-owned; reserved for copies */
    if (!findings_reserve(v, 1))
        return 0;
    oc_finding *f = &v->items[v->n++];
    memset(f, 0, sizeof(*f));
    f->rule_id = spec->rule_id;
    f->rule_name = spec->rule_name;
    f->severity = spec->severity;
    f->file = spec->file;
    f->line = spec->line;
    f->col = spec->col;
    f->end_line = spec->end_line;
    f->symbol = spec->symbol;
    f->message = spec->message;
    f->triaged = 0;
    f->triage_rank = -1;
    f->triage_confidence = 0.0;
    uint64_t h = oc_fingerprint_u64(spec->rule_id, spec->file, spec->symbol, spec->fp_text);
    oc_fingerprint_hex(h, f->fingerprint);
    return 1;
}

static int cmp_finding(const void *pa, const void *pb) {
    const oc_finding *a = (const oc_finding *)pa;
    const oc_finding *b = (const oc_finding *)pb;
    int c = strcmp(a->file, b->file);
    if (c != 0)
        return c;
    if (a->line != b->line)
        return a->line < b->line ? -1 : 1;
    if (a->col != b->col)
        return a->col < b->col ? -1 : 1;
    c = strcmp(a->rule_id, b->rule_id);
    if (c != 0)
        return c;
    return strcmp(a->fingerprint, b->fingerprint);
}

void oc_findings_sort(oc_finding_vec *v) {
    if (v->n > 1)
        qsort(v->items, v->n, sizeof(oc_finding), cmp_finding);
}

oc_severity oc_findings_max_severity(const oc_finding_vec *v) {
    oc_severity max = OC_SEV_INFO;
    for (size_t i = 0; i < v->n; i++) {
        if (v->items[i].severity > max)
            max = v->items[i].severity;
    }
    return max;
}

void oc_findings_destroy(oc_finding_vec *v) {
    free(v->items);
    v->items = NULL;
    v->n = 0;
    v->cap = 0;
}
