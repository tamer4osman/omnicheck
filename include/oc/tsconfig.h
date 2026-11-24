/*
 * tsconfig.json / jsconfig.json alias resolution (resolver v2, WBS 3.x).
 *
 * TypeScript projects address intra-repo modules through compilerOptions
 * paths + baseUrl aliases ("@/lib/x"). v1 skipped those as bare
 * specifiers; v2 loads the nearest config per importing file and rewrites
 * matching specifiers to candidate repo paths that go through the SAME
 * suffix probing as relative imports. Edges therefore still only point
 * at analyzed files -- a config that aliases outside the analysis simply
 * does not produce an edge.
 *
 * Honest scope (descope-not-slip):
 *   - Search walks upward from the importing file's directory and STOPS
 *     at the analysis root (the CLI root arguments). A tsconfig outside
 *     the analysis is invisible, by contract.
 *   - Supports baseUrl + "paths" (exact keys and single "*" wildcard,
 *     longest-pattern-first), and jsconfig.json as fallback.
 *   - JSONC tolerance (comments, trailing commas) via the lenient JSON
 *     parser; package.json "exports"/node_modules resolution stays out
 *     of scope (bare npm specifiers still skip).
 */
#ifndef OC_TSCONFIG_H
#define OC_TSCONFIG_H

#include "arena.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_tsconfig {
    char *dir; /* directory containing the config file (repo-relative) */
    char *base_dir;
    /* Resolved paths patterns, longest-first sorted. Each pattern is the
     * literal key ("@foo/bar" or "@foo" + star); targets are the
     * pattern's substitution candidates (one pattern may list several). */
    char **pattern;
    char ***target;
    size_t *n_target;
    size_t n_patterns;
    int has_base_url;
} oc_tsconfig;

typedef struct oc_tsconfig_set oc_tsconfig_set;

/* The set and every config it parses are arena-owned: no free call
 * exists by design (removing the whole class of cache-heap leaks). */
oc_tsconfig_set *oc_tsconfig_set_new(oc_arena *a);

/* Returns the nearest tsconfig.json/jsconfig.json for from_dir, bounded
 * by root_dir (inclusive), or NULL when none exists. Configs are parsed
 * once and cached per directory. Returns NULL on lookup miss; sets st
 * only on IO/parse errors that make configs untrustworthy? No -- a
 * broken config yields NULL plus a warning status message the caller
 * may surface; the run continues (fail-open for the resolver). */
const oc_tsconfig *oc_tsconfig_for(oc_tsconfig_set *s, oc_arena *a, const char *from_dir,
                                   const char *root_dir, oc_status *st);

/* Rewrites a bare specifier through the config's paths patterns. On a
 * match fills out with the substituted candidate path (before suffix
 * probing) and returns the number of characters written; 0 = no match. */
size_t oc_tsconfig_rewrite(const oc_tsconfig *tc, const char *spec, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* OC_TSCONFIG_H */
