/* Internal parser definitions shared by parser.c and parser_ts.c.
 * Not part of the public API. */
#ifndef OC_PARSER_INTERNAL_H
#define OC_PARSER_INTERNAL_H

#include "oc/imports.h"
#include "oc/parser.h"

#if defined(OC_WITH_TREE_SITTER)
#include <tree_sitter/api.h>
#endif

/* SPRINT-22 (the Kotlin adoption): the 13th registered language.
 * SPRINT-23 (the Swift adoption): the 14th registered language.
 * SPRINT-24 (the Scala adoption): the 15th registered language. */
#define OC_PARSER_MAX_LANGS 15

/* Mutation engine seam (WBS 4.5): the TSLanguage behind a grammar id.
 * Defined only when OC_WITH_TREE_SITTER; NULL for unknown ids. */
#if defined(OC_WITH_TREE_SITTER)
const TSLanguage *oc_ts_language_for(const char *grammar);
#endif

struct oc_lang_entry {
    const char *grammar; /* pack-facing id, e.g. "javascript" */
    oc_lang_family family;
#if defined(OC_WITH_TREE_SITTER)
    const TSLanguage *lang;
    TSQuery *q_functions;
    TSQuery *q_comments;
    TSQuery *q_imports;
    TSQuery *q_exports;
    TSQuery *q_exports_cjs; /* BUGFIX N-3: CommonJS export channel; NULL
                             * for every family except JS (javascript /
                             * typescript / tsx grammars) */
    TSQuery *q_package;   /* per-file package-clause fact; NULL when the
                           * language has none (SPRINT-10: go) */
    TSQuery *q_main;      /* per-file main-method fact; NULL when the
                           * language has none (SPRINT-13: csharp) */
    TSQuery *q_top;       /* per-file top-level-statements fact; NULL when the
                           * language has none (SPRINT-13: csharp) */
    TSQuery *q_bodyless;  /* SPRINT-14: bodyless-symbol query; NULL when the
                           * language has none (SPRINT-14: java) */
    TSQuery *q_accessors; /* SPRINT-14: accessor query; NULL when the
                           * language has none (SPRINT-14: csharp) */
    const char *const *cx_kinds;
    const char *const *cx_ops;
    int (*cx_filter)(TSNode n, const char *src); /* per-node exclusion for
                                                  * listed cx kinds, or NULL
                                                  * (SPRINT-11: java) */
    const char *cx_ops_node;                     /* SPRINT-16: the binary-operator container
                                                  * node type; NULL -> the shared
                                                  * binary_expression default (Ruby: binary) */
#endif
};

struct oc_parser {
    const char *backend_name;
    oc_arena *arena;
    size_t n_langs;
    struct oc_lang_entry langs[OC_PARSER_MAX_LANGS];
#if defined(OC_WITH_TREE_SITTER)
    TSParser *parser;
#endif
};

#if defined(OC_WITH_TREE_SITTER)
/* Provided by parser_ts.c. */
oc_parser *oc_parser_ts_new(oc_arena *arena, oc_status *st);
void oc_parser_ts_free(oc_parser *p);
oc_code oc_parser_ts_parse(oc_parser *p, const oc_parser_input *in, oc_graph *g, oc_parse_out *out,
                           oc_status *st);
#endif

#endif /* OC_PARSER_INTERNAL_H */
