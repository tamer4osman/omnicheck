/*
 * Coverage ingestion and CRAP-ms composition (plan Table 1 v1.1, WBS 3.x).
 *
 * CRAP(m) = comp^2 * (1 - cov/100) + comp is the project's flagship
 * metric; OmniCheck's increment ("CRAP-ms") is the COMPOSITION: coverage
 * produced by other tools in their native formats is mapped onto the
 * unified graph's per-function definition spans, so one number comes out
 * uniformly across languages.
 *
 * Composition contract (documented in docs/ARCHITECTURE.md):
 *   - Anchors are LINE anchors. For Istanbul (JS/TS family) the anchor of
 *     a statement is statementMap[id].start.line; a statement is covered
 *     when s[id] > 0. For coverage.py the anchors are the union of
 *     executed_lines and missing_lines; covered means executed.
 *   - A function's coverage is covered anchors inside its DEFINITION
 *     span / anchors inside its span (OBS-2 fix: one span semantic for
 *     all languages). Integer floor for the percentage.
 *   - Coverage paths are matched against analyzed paths by exact string
 *     first, then by unique "/"-boundary suffix; ambiguous suffixes match
 *     nothing (counted honestly in the report meta).
 *   - A function with zero anchors in its span (empty body, OOM comp) is
 *     NOT measured and never produces a CRAP finding.
 */
#ifndef OC_COVERAGE_H
#define OC_COVERAGE_H

#include "arena.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct oc_graph; /* graph.h includes this header; avoid the include cycle */

typedef struct oc_cov_file {
    char *path;       /* path as written in the coverage source */
    uint32_t *anchor; /* sorted, deduped line numbers */
    uint32_t *covered;
    size_t n_anchor;
    size_t n_covered;
    size_t cap_anchor;
    size_t cap_covered;
    int matched; /* 1 when some analyzed file mapped onto this entry */
} oc_cov_file;

typedef struct oc_coverage {
    oc_cov_file *files; /* heap-owned array, sorted by path */
    size_t n_files;
    size_t cap_files;
    /* Meta counters for the report (loaded = files given to the loader
     * that parsed; matched = entries mapped onto an analyzed file). */
    size_t files_loaded;
    size_t files_matched;
    size_t files_ambiguous;
} oc_coverage;

void oc_coverage_init(oc_coverage *cov);
void oc_coverage_destroy(oc_coverage *cov);

/* Loads one or more coverage report files (Istanbul coverage-final.json
 * or coverage.py coverage.json; format is sniffed per file). Unreadable
 * or unparseable files are hard errors (fail loud, never compose
 * silently). Merge across files is additive: anchors/covered line sets
 * for the same path are unioned. */
oc_code oc_coverage_load(oc_coverage *cov, const char *const *paths, size_t n, oc_arena *a,
                         oc_status *st);

/* Anchor counts for a function span in an analyzed file. Returns 1 when
 * the file matched and anchors_out/covered_out are filled; 0 when the
 * coverage table has no unambiguous entry for the path. Const: never
 * mutates the table. */
int oc_coverage_lookup(const oc_coverage *cov, const char *analyzed_path, size_t def_start,
                       size_t def_end, size_t *anchors_out, size_t *covered_out);

/* Marks which entries mapped onto analyzed files and fills files_matched
 * (call once after load; lookup itself never mutates). */
void oc_coverage_mark_matches(oc_coverage *cov, const struct oc_graph *g);

#ifdef __cplusplus
}
#endif
#endif /* OC_COVERAGE_H */
