#include "oc/discover.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

void oc_paths_init(oc_path_vec *v) {
    v->items = NULL;
    v->n = 0;
    v->cap = 0;
}

void oc_paths_push(oc_path_vec *v, oc_arena *a, const char *path) {
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 64;
        char **ni = (char **)realloc(v->items, cap * sizeof(char *));
        if (!ni)
            return; /* allocation failure: drop silently; callers count via n */
        v->items = ni;
        v->cap = cap;
    }
    v->items[v->n++] = oc_arena_strdup(a, path);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

void oc_paths_destroy(oc_path_vec *v) {
    free(v->items);
    v->items = NULL;
    v->n = 0;
    v->cap = 0;
}

void oc_paths_sort_dedupe(oc_path_vec *v) {
    if (v->n == 0)
        return;
    qsort(v->items, v->n, sizeof(char *), cmp_str);
    size_t w = 1;
    for (size_t i = 1; i < v->n; i++) {
        if (strcmp(v->items[i], v->items[w - 1]) != 0)
            v->items[w++] = v->items[i];
    }
    v->n = w;
}

static int is_ignored_dir(const char *name) {
    static const char *const ignored[] = {
        ".git",   "node_modules", "__pycache__", ".venv",         "venv",  "dist",   "build",
        "target", ".tox",         ".mypy_cache", ".pytest_cache", ".idea", ".vscode"};
    for (size_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++) {
        if (strcmp(name, ignored[i]) == 0)
            return 1;
    }
    return 0;
}

static void discover_walk(const char *dir, int depth, const oc_pack_set *ps, oc_arena *a,
                          oc_path_vec *out, oc_discover_stats *stats) {
    if (depth > 24)
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 && stats)
                stats->skipped_dot_entries++; /* OBS-1: counted, not silent */
            continue;
        }
        char sub[1024];
        if ((size_t)snprintf(sub, sizeof(sub), "%s/%s", dir, ent->d_name) >= sizeof(sub))
            continue;
        struct stat sb;
        if (lstat(sub, &sb) != 0)
            continue; /* symlinks are not followed */
        if (S_ISDIR(sb.st_mode)) {
            if (!is_ignored_dir(ent->d_name)) {
                discover_walk(sub, depth + 1, ps, a, out, stats);
            } else if (stats) {
                stats->skipped_ignored_dirs++; /* OBS-1: counted, not silent */
            }
        } else if (S_ISREG(sb.st_mode)) {
            /* Discovery is pack-agnostic: every candidate source file is
             * returned so callers can count unknown-type skips. */
            (void)ps;
            oc_paths_push(out, a, sub);
        }
    }
    closedir(d);
}

oc_code oc_discover_files(const char *const *roots, size_t n_roots, const oc_pack_set *ps,
                          oc_arena *a, oc_path_vec *out, oc_discover_stats *stats, oc_status *st) {
    if (stats) {
        stats->skipped_dot_entries = 0;
        stats->skipped_ignored_dirs = 0;
    }
    for (size_t i = 0; i < n_roots; i++) {
        struct stat sb;
        if (stat(roots[i], &sb) != 0) {
            oc_status_set(st, OC_ERR_IO, "path not found: '%s'", roots[i]);
            return OC_ERR_IO;
        }
        if (S_ISDIR(sb.st_mode)) {
            discover_walk(roots[i], 0, ps, a, out, stats);
        } else if (S_ISREG(sb.st_mode)) {
            (void)ps; /* single-file roots are taken as-is */
            oc_paths_push(out, a, roots[i]);
        }
    }
    oc_paths_sort_dedupe(out);
    oc_status_clear(st);
    return OC_OK;
}
