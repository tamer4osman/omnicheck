#include "parser_internal.h"

#include <stdio.h>

/*
 * Backend dispatcher. When OC_WITH_TREE_SITTER is defined (set by CMake
 * when vendored sources are present), parser_ts.c provides the real
 * backend; otherwise we register a stub that fails with an actionable
 * message instead of silently degrading (deterministic behavior over
 * silent approximation, per plan Ch4).
 */

oc_parser *oc_parser_new(oc_arena *arena, oc_status *st) {
#if defined(OC_WITH_TREE_SITTER)
    return oc_parser_ts_new(arena, st);
#else
    (void)st;
    oc_parser *p = (oc_parser *)oc_arena_alloc(arena, sizeof(oc_parser));
    if (p) {
        memset(p, 0, sizeof(*p));
        p->backend_name = "none";
    }
    return p;
#endif
}

const char *oc_parser_backend_name(const oc_parser *p) {
    return p ? p->backend_name : "none";
}

const char *const *oc_parser_supported_grammars(const oc_parser *p) {
    static const char *names[OC_PARSER_MAX_LANGS + 1];
    if (!p || p->n_langs == 0)
        return NULL;
    for (size_t i = 0; i < p->n_langs; i++)
        names[i] = p->langs[i].grammar;
    names[p->n_langs] = NULL;
    return names;
}

oc_code oc_parser_parse_file(oc_parser *p, const oc_parser_input *in, oc_graph *g,
                             oc_parse_out *out, oc_status *st) {
#if defined(OC_WITH_TREE_SITTER)
    return oc_parser_ts_parse(p, in, g, out, st);
#else
    (void)in;
    (void)g;
    if (out) {
        memset(&out->diag, 0, sizeof(out->diag));
        out->imports = NULL;
    }
    oc_status_set(st, OC_ERR_INTERNAL,
                  "this binary was built without AST parsing backends; run "
                  "scripts/vendor.sh and reconfigure to enable them");
    return OC_ERR_INTERNAL;
#endif
}

void oc_parser_free(oc_parser *p) {
#if defined(OC_WITH_TREE_SITTER)
    oc_parser_ts_free(p);
#else
    (void)p;
#endif
}

#if !defined(OC_WITH_TREE_SITTER)
oc_sniff_verdict oc_parser_sniff_header(const char *src, size_t len) {
    /* No AST backend: the sniff cannot produce parse facts, so it answers
     * inconclusive and the caller's project-root default decides (the
     * documented SPRINT-21 fallback -- never a fabricated verdict). */
    (void)src;
    (void)len;
    return OC_SNIFF_INCONCLUSIVE;
}
#endif
