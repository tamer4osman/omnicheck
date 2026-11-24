/*
 * Unified graph: the parse product every analysis pass traverses
 * (plan Ch4 "analysis as graph traversal"). Sprint 0 scope: file nodes,
 * symbol nodes (functions/classes/variables), CONTAINS edges, and
 * per-file comment docs. Import resolution lands with WBS 3.2.
 */
#ifndef OC_GRAPH_H
#define OC_GRAPH_H

#include "arena.h"
#include "coverage.h"
#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t oc_node_id;
#define OC_NODE_INVALID ((oc_node_id)UINT32_MAX)

typedef enum oc_node_kind {
    OC_NODE_FILE = 0,
    OC_NODE_FUNCTION,
    OC_NODE_CLASS,
    OC_NODE_VARIABLE
} oc_node_kind;

const char *oc_node_kind_str(oc_node_kind k);

typedef enum oc_edge_kind { OC_EDGE_CONTAINS = 0, OC_EDGE_IMPORTS } oc_edge_kind;

const char *oc_edge_kind_str(oc_edge_kind k);

/* SPRINT-17 (B3, typed-where-declared): one ordered parameter of a
 * function signature. `type` is the RAW source text of the declared
 * type (no resolution, no normalization) or NULL when the language or
 * annotation site declares none -- NULL on either side of a diff means
 * "not comparable", never "equal". `name` is "..." for splat/rest/
 * variadic/unnamed/destructuring markers so arity stays caller-
 * visible while markers stay out of type/rename comparisons.
 * `has_default` feeds the default-awareness policy. */
typedef struct oc_param {
    const char *name; /* arena-owned, NUL-terminated */
    const char *type; /* arena-owned raw text, or NULL */
    int has_default;
} oc_param;

typedef struct oc_node {
    oc_node_kind kind;
    const char *name; /* file node: full path; symbol: identifier */
    const char *path; /* owning file path (== name for file nodes) */
    size_t decl_line;
    /* OBS-2 fix (Sprint 2): one canonical span for every function across
     * all languages -- the DEFINITION EXTENT (first line of the defining
     * construct through its last line). For JS this is the declaration
     * through the closing brace; for Python the `def` line through the
     * last body line (docstring included). Passes and coverage composition
     * (CRAP-ms) must use this span, never the body block alone. */
    size_t def_start_line; /* == decl_line; 0 when the symbol has no span */
    size_t def_end_line;
    /* Cyclomatic complexity (decision points + 1), computed at parse time
     * for function nodes by the tree-sitter backend; 0 = not computed.
     * CRAP-lite proxy (no coverage): comp^2 + comp. Feeds OC004 and the
     * later CRAP-ms composition (plan Table 1 v1.1). */
    size_t complexity;
    /* SPRINT-17 (B3): ordered parameter signature (function nodes).
     * Arena-owned; params == NULL means "no parameters or not
     * extracted" -- consumers must not treat NULL as zero-arg, only as
     * signature-unknown. The `oc graph` JSON dump does NOT carry these
     * fields (byte shape test-locked). */
    oc_param *params;
    size_t n_params;
} oc_node;

typedef struct oc_edge {
    oc_node_id from;
    oc_node_id to;
    oc_edge_kind kind;
} oc_edge;

typedef struct oc_comment {
    size_t line;
    size_t col;
    const char *text; /* arena-owned, NUL-terminated */
    size_t len;
} oc_comment;

typedef struct oc_doc {
    const char *path;
    oc_comment *comments;
    size_t n_comments;
    size_t cap_comments;
} oc_doc;

typedef struct oc_graph {
    oc_arena *arena;
    oc_node *nodes; /* heap-owned array */
    size_t n_nodes;
    size_t cap_nodes;
    oc_edge *edges;
    size_t n_edges;
    size_t cap_edges;
    oc_doc **docs; /* arena-owned side table indexed by file node id */
    size_t docs_cap;
} oc_graph;

void oc_graph_init(oc_graph *g, oc_arena *arena);
void oc_graph_destroy(oc_graph *g);

oc_node_id oc_graph_add_file(oc_graph *g, const char *path); /* deduped */
oc_node_id oc_graph_add_symbol(oc_graph *g, oc_node_kind kind, const char *name,
                               const char *file_path, size_t decl_line, size_t def_start,
                               size_t def_end);
/* SPRINT-17 (B3): attaches a DEEP-COPIED (arena-strdup'd) parameter
 * signature to a function node. n == 0 clears to the NULL/unknown
 * shape. Returns 1 on success, 0 on bad id or arena exhaustion. */
int oc_graph_set_params(oc_graph *g, oc_node_id id, const oc_param *params, size_t n);
int oc_graph_add_edge(oc_graph *g, oc_node_id from, oc_node_id to, oc_edge_kind kind);
oc_node_id oc_graph_find_file(const oc_graph *g, const char *path);
/* Returns the doc for a file node id, creating it on first use. */
oc_doc *oc_graph_doc(oc_graph *g, oc_node_id file_id);
int oc_doc_add_comment(oc_doc *doc, oc_arena *a, size_t line, size_t col, const char *text,
                       size_t len);

/* Deterministic, fixed-field-order dump for `oc graph`. cov is optional:
 * when non-NULL, function nodes gain coverage_anchors/coverage_covered/
 * coverage_pct/crap fields. */
int oc_graph_write_json(const oc_graph *g, const struct oc_coverage *cov, oc_jsonw *w);

#ifdef __cplusplus
}
#endif
#endif /* OC_GRAPH_H */
