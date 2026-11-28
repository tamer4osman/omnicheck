/*
 * Reusable analysis pipeline (Sprint 4, M3 Beta scope).
 *
 * One scan entry serves every consumer that needs the full pipeline --
 * `oc check` (JSON + human reports), `oc lsp` (diagnostics + document
 * symbols), and `oc mcp` (findings query tools) -- so the three surfaces
 * can never drift apart in what they analyze or report. The module owns
 * the exact `cmd_check` pipeline: packs -> discover -> parse -> resolve ->
 * coverage -> surface -> duplication -> passes -> (triage) -> sort.
 *
 * Determinism contract is inherited unchanged: same inputs, same byte
 * output. Findings are returned already sorted in the canonical
 * (file, line, col, rule) order; triage (if requested) ran BEFORE the
 * sort (report order never depends on provider output).
 *
 * Parse failures are recorded, never fatal: every file that did not parse
 * cleanly is listed in result->parse_errors and counted in meta. Callers
 * decide the exit-code policy (`oc check` dominates with exit 2; LSP and
 * MCP surface them as diagnostics / structured content).
 */
#ifndef OC_SCAN_H
#define OC_SCAN_H

#include "arena.h"
#include "brain.h"
#include "coverage.h"
#include "discover.h"
#include "dup.h"
#include "finding.h"
#include "graph.h"
#include "pack.h"
#include "pass.h"
#include "triage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_scan_options {
    const char *packs_dir;
    const char *const *paths; /* analysis roots, as given on the command line */
    size_t n_paths;
    const char *const *coverage; /* Istanbul / V8 / coverage.py JSON files */
    size_t n_coverage;
    const char *osv_snapshot; /* vendored OSV snapshot (OC009); NULL = off */
    const oc_config *cfg;     /* pass options; must outlive the call */
    int want_triage;          /* OmniDecide rank-only sidecar (fail-open) */
    int want_triage_embedded; /* --triage-embedded: in-process kernel (T2);
                               * the sidecar keeps absolute precedence */
    int want_triage_explain;  /* --triage-explain: attach the 9 kernel
                               * rationale channels per finding (T1) */
    int want_triage_brain;    /* --triage-brain: consult the brain memory
                               * (advisory; requires want_triage) (T4) */
    /* T7: kernel customization resources (borrowed pointers; each must
     * outlive the call). A kernel refusal is LOUD. */
    const char *const *triage_lexicons;
    size_t n_triage_lexicons;
    const char *triage_model;
} oc_scan_options;

/* T7: honest provenance of the opt-in kernel customization resources
 * (--triage-lexicon / --triage-model). Each accepted resource carries its
 * content sha256; applied counters come from the embedded responder. */
#define OC_MAX_KERNEL_FILES 16
typedef struct oc_kernel_resource {
    char path[512];
    char sha256[65];
} oc_kernel_resource;

typedef struct oc_kernel_files_outcome {
    int requested; /* any lexicon or model given */
    size_t n_lexicons;
    oc_kernel_resource lexicons[OC_MAX_KERNEL_FILES];
    int model_requested;
    oc_kernel_resource model;
    size_t lexicons_applied; /* embedded responder counters */
    int model_applied;
    char reason[192];
} oc_kernel_files_outcome;

/* One file that failed to parse cleanly (named, never silent). */
typedef struct oc_scan_parse_error {
    const char *file;
    size_t error_nodes;
    size_t first_line;   /* 1-based; 0 when unknown */
    size_t first_column; /* 1-based byte column; 0 when unknown */
} oc_scan_parse_error;

/* One CRAP leaderboard row (M4 gate criterion 1, WBS 4.3): a measured
 * function ranked by CRAP desc. Composed only when coverage was given. */
typedef struct oc_crap_entry {
    const char *path;
    size_t line;
    const char *symbol;
    size_t complexity;
    size_t cov_pct;
    size_t crap;
} oc_crap_entry;

#define OC_CRAP_LEADERBOARD_TOP 10

typedef struct oc_scan_result {
    /* The scan owns ONE heap arena that backs the graph, the findings'
     * strings, the packs, and the duplication index. It is destroyed
     * LAST by oc_scan_result_destroy (it holds the real final block
     * list; a struct copy would leak). NULL after destroy. */
    oc_arena *arena;
    /* Graph stays reachable: LSP documentSymbol reads symbol nodes from
     * it; MCP consumers may inspect it. Owned by the result. */
    oc_graph graph;
    oc_finding_vec findings; /* sorted canonically */
    /* Named parse-error records (BUG-1 contract, M1 criterion #2). */
    oc_scan_parse_error *parse_errors;
    size_t n_parse_errors;
    /* Report/meta inputs, mirroring the `oc check` JSON report fields. */
    size_t files_analyzed;
    size_t files_skipped;
    /* Go build-constraint exclusions (SPRINT-14, §Go.8): files dropped
     * from the analyzed set under the pack-declared build context.
     * Counted, never silent; zero when the pack declares no context. */
    size_t build_tag_excluded;
    /* SPRINT-21: the C/C++ header routing counters (the approved
     * architecture decision). Non-zero only when both the c and cpp
     * packs are registered AND .h files were analyzed; the report meta
     * writes the header_routing block from them (never silent). */
    size_t h_sniffed_cpp;
    size_t h_sniffed_c;
    size_t h_default_cpp;
    size_t h_default_c;
    oc_discover_stats dstats;
    size_t functions_total;
    size_t functions_measured;
    oc_triage_outcome triage;             /* provider "none" unless requested */
    oc_brain_outcome brain;               /* advisory no-op unless want_triage_brain;
                                           * the report emits the brain block only
                                           * when brain.requested */
    oc_kernel_files_outcome kernel_files; /* T7 provenance; emitted only
                                           * when requested */
    const char *backend;                  /* parser backend name ("tree-sitter") */
    const oc_pack_set *packs;             /* loaded packs (report + rule context) */
    const oc_dup_index *dup;              /* duplication meta (arena-owned) */
    const oc_coverage *cov;               /* NULL when no coverage was composed */
    /* CRAP leaderboard (WBS 4.3): top OC_CRAP_LEADERBOARD_TOP measured
     * functions by CRAP desc (ties: path asc, line asc); NULL when no
     * coverage was composed. Arena-owned. */
    oc_crap_entry *leaderboard;
    size_t n_leaderboard;
    /* Supply tier meta (WBS 4.2): manifests seen in the analyzed tree;
     * osv_snapshot_date is the vendored snapshot's visible date or NULL
     * when --osv-snapshot was not given. */
    size_t manifests_scanned;
    /* BUGFIX N-2: manifests that could not be read/parsed were SKIPPED
     * (recorded, never silent). Parallel arrays of file/reason strings,
     * arena-owned; counts are zero and the arrays NULL when nothing was
     * skipped, so clean runs stay byte-stable. */
    size_t manifests_skipped;
    const char **manifest_skip_files;
    const char **manifest_skip_reasons;
    const char *osv_snapshot_date;
    /* Opaque internals (imports, surface vecs, parser, file list).
     * Freed by oc_scan_result_destroy; public pointers above point into
     * them and die with them. */
    void *_impl;
} oc_scan_result;

/* Runs the full pipeline. On failure returns an oc_code != OC_OK with the
 * reason in st (hard tool error: bad packs dir, unreadable file, OOM) --
 * parse failures of ANALYZED files are not failures, they are records.
 * The result is always safe to destroy, on success and failure alike. */
oc_code oc_scan_run(const oc_scan_options *opt, oc_scan_result *res, oc_status *st);

void oc_scan_result_destroy(oc_scan_result *res);

#ifdef __cplusplus
}
#endif
#endif /* OC_SCAN_H */
