/*
 * File discovery: recursive, deterministic, ignore-aware. Results are
 * always sorted and deduped so that report output is byte-stable
 * regardless of filesystem enumeration order.
 */
#ifndef OC_DISCOVER_H
#define OC_DISCOVER_H

#include "arena.h"
#include "common.h"
#include "pack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_path_vec {
    char **items; /* arena-owned strings */
    size_t n;
    size_t cap;
} oc_path_vec;

/* OBS-1 fix (Sprint 2): silent discovery skips became counted skips.
 * Both counters are report fields; a reader can now tell exactly why a
 * file on disk is absent from the analysis. */
typedef struct oc_discover_stats {
    size_t skipped_dot_entries;  /* entries starting with '.' (files and dirs) */
    size_t skipped_ignored_dirs; /* vcs/build/venv directories from the ignore list */
} oc_discover_stats;

void oc_paths_init(oc_path_vec *v);
void oc_paths_push(oc_path_vec *v, oc_arena *a, const char *path);
void oc_paths_sort_dedupe(oc_path_vec *v);
/* Frees the heap-owned pointer array (strings stay in the arena). */
void oc_paths_destroy(oc_path_vec *v);

/* Discovers candidate source files under roots (files or directories).
 * Discovery is pack-agnostic: every regular file outside the ignore list
 * is returned; callers match packs and count unknown-type skips. Skips
 * dot-entries and known build/vcs/venv directories -- both are COUNTED
 * into stats when stats is non-NULL; symlinks are not followed. Results
 * are sorted and deduped. */
oc_code oc_discover_files(const char *const *roots, size_t n_roots, const oc_pack_set *ps,
                          oc_arena *a, oc_path_vec *out, oc_discover_stats *stats, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_DISCOVER_H */
