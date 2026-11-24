/*
 * Finding model and sink. Findings are the single output currency of
 * every analysis pass; triage may only annotate them (rank-only
 * guardrail), never drop or re-severity them.
 */
#ifndef OC_FINDING_H
#define OC_FINDING_H

#include "arena.h"
#include "fingerprint.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum oc_severity { OC_SEV_INFO = 0, OC_SEV_WARN = 1, OC_SEV_ERROR = 2 } oc_severity;

const char *oc_severity_str(oc_severity s);
int oc_severity_from_str(const char *s, oc_severity *out);

typedef struct oc_finding {
    const char *rule_id;   /* "OC001" */
    const char *rule_name; /* "long-function" */
    oc_severity severity;
    const char *file; /* repo-relative path */
    size_t line;      /* 1-based */
    size_t col;       /* 1-based */
    size_t end_line;
    const char *symbol; /* anchor identifier, NULL for file-level findings */
    const char *message;
    char fingerprint[OC_FINGERPRINT_HEX_LEN + 1];
    /* Filled by the triage layer; never affects severity or existence. */
    int triaged;
    int triage_rank;
    double triage_confidence;
} oc_finding;

typedef struct oc_finding_vec {
    oc_finding *items; /* heap-owned; strings point into an arena */
    size_t n;
    size_t cap;
} oc_finding_vec;

typedef struct oc_finding_spec {
    const char *rule_id;
    const char *rule_name;
    oc_severity severity;
    const char *file;
    size_t line;
    size_t col;
    size_t end_line;
    const char *symbol; /* nullable */
    const char *message;
    const char *fp_text; /* optional normalized text folded into the fp */
} oc_finding_spec;

void oc_findings_init(oc_finding_vec *v);
/* Computes the fingerprint and copies owned strings into the arena. */
int oc_findings_push(oc_finding_vec *v, oc_arena *a, const oc_finding_spec *spec);
void oc_findings_sort(oc_finding_vec *v); /* (file, line, col, rule) order */
oc_severity oc_findings_max_severity(const oc_finding_vec *v);
void oc_findings_destroy(oc_finding_vec *v);

#ifdef __cplusplus
}
#endif
#endif /* OC_FINDING_H */
