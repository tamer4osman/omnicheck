/*
 * Mutation engine (WBS 4.5, M4 RC scope). Plan Ch4 4.6: operator sets
 * defined as tree-sitter queries -- relational-operator swap, logical-
 * operator swap, condition negation, constant tweak, statement deletion,
 * return-value change and argument swap, mapped per grammar -- run the
 * project's test suite per mutant and record killed or survived.
 *
 * Interpreter-level injection: Python and JavaScript sources are mutated
 * IN PLACE (exact byte splice) and restored after the run -- no
 * compilation step exists, so source-level injection IS interpreter-level
 * for these packs; compiled-language strategies stay post-1.0 by plan.
 *
 * Determinism contract:
 *   - Mutants are collected in tree order (byte offset asc, op id asc)
 *     from the SAME pinned grammars the analysis pipeline uses.
 *   - The runner executes mutants in collection order, one at a time,
 *     serial (single-core budget honesty).
 *   - A per-mutant cache is keyed by a content hash of the ORIGINAL file
 *     bytes + the exact edit; any file change invalidates the entry.
 *   - Statuses: killed (suite failed), survived (suite passed), timeout
 *     (suite hung; counted as killed in the kill ratio, reported
 *     separately for honesty). "skipped" entries never ran (cache hit
 *     or coverage-guided exclusion) and carry their reason.
 *   - Test files are never mutated: a path whose basename starts with
 *     "test" or contains ".test." / "_test." is excluded by convention
 *     (the same entry-point family the surface module uses).
 */
#ifndef OC_MUTATE_H
#define OC_MUTATE_H

#include "arena.h"
#include "common.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum oc_mut_status {
    OC_MUT_PENDING = 0,
    OC_MUT_KILLED,
    OC_MUT_SURVIVED,
    OC_MUT_TIMEOUT,
    OC_MUT_SKIPPED_CACHE, /* cached verdict reused, no run */
    OC_MUT_SKIPPED_COV,   /* coverage-guided exclusion */
    OC_MUT_SKIPPED_BUDGET /* beyond --max-mutants */
} oc_mut_status;

const char *oc_mut_status_str(oc_mut_status s);

typedef struct oc_mutant {
    const char *file;
    size_t line, col; /* 1-based position of the edit */
    size_t byte_start, byte_end;
    const char *op_id;       /* rel-swap / logic-swap / negate-flip /
                              * const-tweak / stmt-del / ret-change /
                              * arg-swap */
    const char *original;    /* replaced bytes (arena copy) */
    const char *replacement; /* arena copy */
    uint64_t content_hash;   /* FNV-1a 64 of the ORIGINAL source bytes at
                              * collection time: any file change invalidates
                              * the cache key built from this record */
    oc_mut_status status;
} oc_mutant;

typedef struct oc_mutant_vec {
    oc_mutant *items;
    size_t n;
    size_t cap;
} oc_mutant_vec;

void oc_mutants_init(oc_mutant_vec *v);
void oc_mutants_destroy(oc_mutant_vec *v);

/* Collects the operator-set mutants for ONE source buffer using the
 * grammar's tree-sitter queries. Deterministic order. */
oc_code oc_mutate_collect(const char *grammar, const char *path, const char *src, size_t len,
                          oc_arena *a, oc_mutant_vec *out, oc_status *st);

typedef struct oc_mutate_options {
    const char *packs_dir;
    const char *const *paths; /* analysis roots */
    size_t n_paths;
    const char *test_cmd;        /* REQUIRED: the suite command; runs in cwd */
    long timeout_s;              /* per mutant, default 30 */
    const char *cache_path;      /* nullable */
    size_t max_mutants;          /* 0 = no cap */
    const char *const *coverage; /* nullable; enables coverage-guided
                                  * mutant selection (test-reachable
                                  * functions only) */
    size_t n_coverage;
} oc_mutate_options;

typedef struct oc_mutate_report {
    size_t files_mutated;
    size_t total, executed, cache_hits;
    size_t killed, survived, timeouts;
    double wall_seconds;
    oc_mutant_vec mutants; /* all records, collection order */
} oc_mutate_report;

/* Full engine: discover -> parse -> collect -> (coverage filter) ->
 * execute -> report JSON to `out`. Exit semantics are the caller's. */
oc_code oc_mutate_run(const oc_mutate_options *o, oc_mutate_report *rep, FILE *out, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_MUTATE_H */
