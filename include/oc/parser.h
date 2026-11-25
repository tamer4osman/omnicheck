/*
 * Parser interface: the substitution seam between grammars and the graph
 * (plan Ch4 layer 2). Sprint 0 ships the tree-sitter backend; future
 * backends plug in behind the same vtable without touching passes.
 */
#ifndef OC_PARSER_H
#define OC_PARSER_H

#include "arena.h"
#include "graph.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_parser_input {
    const char *path;    /* repo-relative path as it should appear in findings */
    const char *source;  /* NUL-terminated source text */
    size_t len;          /* byte length excluding the terminator */
    const char *grammar; /* grammar id from the pack, e.g. "javascript" */
} oc_parser_input;

typedef struct oc_parser oc_parser;

/* Per-file parse-quality diagnostics. tree-sitter recovers from syntax
 * errors by building a partial tree with ERROR / MISSING nodes, so a file
 * can "parse" and still be syntactically broken. Callers MUST surface
 * had_error somewhere user-visible (report field, exit code, stderr) --
 * a run that parsed nothing cleanly must never look clean. This closes
 * the silent-partial-analysis failure mode named in plan v1.1. */
typedef struct oc_parse_diagnostics {
    int had_error;      /* 1 when the parse tree contains ERROR or MISSING nodes */
    size_t error_nodes; /* number of ERROR/MISSING nodes found in the tree */
    size_t first_line;  /* 1-based line of the first error node; 0 when none */
    size_t first_col;   /* 1-based byte column of the first error node; 0 when none */
} oc_parse_diagnostics;

struct oc_import_vec;
struct oc_bind_vec;
struct oc_export_vec;
struct oc_hint_vec;

/* Everything one file parse hands back to the caller besides the graph:
 * parse-quality diagnostics, raw import specifiers (resolved into
 * OC_EDGE_IMPORTS edges later), import BINDINGS and EXPORT candidates
 * (resolved/marked by the module-surface build for OC005/OC006), and the
 * per-file parse hints (shebang; the Go package_clause name since
 * SPRINT-10) used by entry-point classification (OC006). */
typedef struct oc_parse_out {
    oc_parse_diagnostics diag;
    struct oc_import_vec *imports; /* nullable; records appended during parse */
    struct oc_bind_vec *binds;     /* nullable; named import bindings */
    struct oc_export_vec *exports; /* nullable; export candidates */
    struct oc_hint_vec *hints;     /* nullable; per-file parse hints */
} oc_parse_out;

/* Creates the best backend compiled into this binary. Never NULL; a build
 * without tree-sitter yields a backend that reports OC_ERR_INTERNAL on
 * use with an actionable message. */
oc_parser *oc_parser_new(oc_arena *arena, oc_status *st);
void oc_parser_free(oc_parser *p);

const char *oc_parser_backend_name(const oc_parser *p);
/* Supported grammar ids, NULL-terminated array (empty when no backend). */
const char *const *oc_parser_supported_grammars(const oc_parser *p);

/* Parses one file into the graph: adds the file node, symbol nodes
 * (with parse-time cyclomatic complexity), CONTAINS edges and comments;
 * raw import specifiers land in out->imports. On any hard error return
 * out is left untouched. */
oc_code oc_parser_parse_file(oc_parser *p, const oc_parser_input *in, oc_graph *g,
                             oc_parse_out *out, oc_status *st);

/* SPRINT-21: the C/C++ header-routing sniff (the approved architecture
 * decision, packs/c/pack.json + docs/LANGUAGES.md C++ section 9). Parses
 * the source with the pinned cpp grammar and answers ONE question from
 * parse facts only:
 *   - OC_SNIFF_CPP_POSITIVE: the cpp parse is CLEAN and the tree carries
 *     at least one C++-distinctive node kind (namespace_definition,
 *     class_specifier, template_declaration, alias_declaration,
 *     using_declaration, concept_definition, catch_clause,
 *     for_range_loop);
 *   - OC_SNIFF_C_POSITIVE: the cpp parse ERRORED while the C parse is
 *     clean (the content is positively C -- the probed class is the K&R
 *     old-style definition, structurally impossible in C++; keyword
 *     identifiers do NOT qualify: tree-sitter's error recovery parses
 *     `int class = 3;` clean under cpp);
 *   - OC_SNIFF_INCONCLUSIVE: neither (a C-compatible header parses clean
 *     under BOTH grammars -- the caller applies the project-root
 *     default). A build without the tree-sitter backend answers
 *     inconclusive; the root default then decides (documented fallback). */
typedef enum oc_sniff_verdict {
    OC_SNIFF_INCONCLUSIVE = 0,
    OC_SNIFF_CPP_POSITIVE,
    OC_SNIFF_C_POSITIVE
} oc_sniff_verdict;

oc_sniff_verdict oc_parser_sniff_header(const char *src, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* OC_PARSER_H */
