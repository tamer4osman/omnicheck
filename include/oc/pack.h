/*
 * Language packs: declarative, versioned data files (register-not-train,
 * plan Ch4 Table 5). A pack binds file extensions to a grammar and the
 * set of analysis passes it enables. Packs carry data only -- no code.
 */
#ifndef OC_PACK_H
#define OC_PACK_H

#include "arena.h"
#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_pack {
    char *id;
    char *display_name;
    char *grammar;
    char *version;
    char *notes;       /* nullable */
    char **extensions; /* ".js" etc. */
    size_t n_extensions;
    char **entry_points; /* declared in v0, enforced from WBS 3.2 */
    size_t n_entry_points;
    char **passes; /* enabled pass ids */
    size_t n_passes;
    /* Declared Go build context (SPRINT-14, docs/LANGUAGES.md §Go.8):
     * pack data -- the deterministic analysis target the pack pins for
     * build-constraint evaluation. All NULL/0 when the pack does not
     * declare a build_context (no constraint evaluation). goos/goarch are
     * REQUIRED when the context is declared; compiler defaults to "gc",
     * cgo to 0, extra tags to none. */
    char *build_goos;
    char *build_goarch;
    char *build_compiler; /* nullable -> "gc" */
    int build_cgo;
    char **build_tags;
    size_t n_build_tags;
    char *source_path;
} oc_pack;

typedef struct oc_pack_set {
    oc_arena *arena;
    oc_pack *packs;
    size_t n_packs;
    size_t cap_packs;
} oc_pack_set;

void oc_packs_init(oc_pack_set *ps, oc_arena *arena);
/* Frees the heap-owned pack array (strings stay in the arena). */
void oc_packs_destroy(oc_pack_set *ps);
/* Loads every <dir>/<subdir>/pack.json. Returns OC_ERR_PACK with a
 * precise message on the first invalid pack; missing dir is an error. */
oc_code oc_packs_load_dir(oc_pack_set *ps, const char *dir, oc_status *st);

const oc_pack *oc_packs_match_path(const oc_pack_set *ps, const char *path);
const oc_pack *oc_packs_find(const oc_pack_set *ps, const char *id);
int oc_pack_has_pass(const oc_pack *p, const char *pass_id);
int oc_packs_write_json(const oc_pack_set *ps, oc_jsonw *w);

/* SPRINT-21: the C/C++ header routing (the approved architecture
 * decision, packs/c/pack.json + docs/LANGUAGES.md C++ section 9). Plain
 * extension matching stops being well-defined the moment BOTH the `c`
 * and `cpp` packs claim `.h` -- routing for that one extension goes
 * through the approved treatment:
 *   1. content sniff (oc_parser_sniff_header, parse facts only): a clean
 *      cpp parse carrying a C++-distinctive node routes to `cpp`; a
 *      content that errors under cpp but parses clean as C routes to `c`;
 *   2. project-root default (inconclusive sniff): `cpp` iff the
 *      DISCOVERED file set carries at least one file with a NON-.h cpp
 *      pack extension (the project uses C++), else `c`.
 * Every routed-by-treatment file lands in the report meta's
 * header_routing block (never silent). Every other path (unique or
 * first extension match) routes with reason OC_ROUTE_EXT. A build
 * without the tree-sitter backend sniffs inconclusive and the root
 * default decides (the documented fallback). src may be NULL (no
 * content available): the treatment then degrades to the root default,
 * never to a guess. */
typedef enum oc_route_reason {
    OC_ROUTE_EXT = 0,       /* plain extension match */
    OC_ROUTE_H_SNIFF_CPP,   /* .h: cpp-positive content sniff */
    OC_ROUTE_H_SNIFF_C,     /* .h: cpp errored, C parse clean */
    OC_ROUTE_H_DEFAULT_CPP, /* .h: root default (cpp source in the tree) */
    OC_ROUTE_H_DEFAULT_C    /* .h: root default (C-only project) */
} oc_route_reason;

const oc_pack *oc_packs_route_source(const oc_pack_set *ps, const char *path, const char *src,
                                     size_t len, const char *const *discovered, size_t n_discovered,
                                     oc_arena *a, oc_route_reason *out_reason);

#ifdef __cplusplus
}
#endif
#endif /* OC_PACK_H */
