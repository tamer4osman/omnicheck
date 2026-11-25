/*
 * Module surface: exported symbols and import bindings (WBS 3.x).
 *
 * Sprint 1's import edges say WHICH FILES reference each other; this
 * module says WHICH SYMBOLS cross file boundaries. The parser records
 * export candidates and import bindings while parsing; after every file
 * is in the graph, oc_surface_build() resolves bindings (using the same
 * resolvers as the edge pass, including tsconfig paths) and marks each
 * export as used or unused. Fail-open policy: anything that cannot be
 * proven a named import (wildcards, require()/dynamic import(), default
 * imports, __all__ re-exports) marks the whole target file used.
 *
 * Consumers:
 *   - OC005 unused-export: a named export never referenced by any
 *     analyzed file, reported only when the containing file itself is
 *     imported (so dead files stay OC006's domain and barrels stay quiet).
 *   - OC006 unreferenced-module: an analyzed file with zero inbound
 *     import edges that no entry-point rule claims (pack entry_points
 *     globs, shebang, test conventions). Severity info: structurally
 *     true, intent-blind -- the OmniDecide layer's job.
 */
#ifndef OC_SURFACE_H
#define OC_SURFACE_H

#include "arena.h"
#include "common.h"
#include "graph.h"
#include "pack.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "oc/imports.h" /* oc_lang_family */

typedef struct oc_export_rec {
    const char *path; /* repo-relative owning file */
    const char *name; /* exported name; "default" pseudo-export for JS default */
    size_t line;      /* anchor line for findings */
    int from_all;     /* 1 when recorded from Python __all__ */
    int used;         /* filled by oc_surface_build */
} oc_export_rec;

typedef struct oc_export_vec {
    oc_export_rec *items; /* heap-owned array; strings live in an arena */
    size_t n;
    size_t cap;
} oc_export_vec;

typedef struct oc_bind_rec {
    const char *from_path;
    const char *specifier;
    const char *name; /* imported name, "default", or NULL = wildcard */
    oc_lang_family family;
} oc_bind_rec;

typedef struct oc_bind_vec {
    oc_bind_rec *items; /* heap-owned array; strings live in an arena */
    size_t n;
    size_t cap;
} oc_bind_vec;

void oc_surface_exports_init(oc_export_vec *v);
void oc_surface_exports_destroy(oc_export_vec *v);
int oc_surface_export_push(oc_export_vec *v, oc_arena *a, const char *path, const char *name,
                           size_t line, int from_all);

void oc_surface_binds_init(oc_bind_vec *v);
void oc_surface_binds_destroy(oc_bind_vec *v);
int oc_surface_bind_push(oc_bind_vec *v, oc_arena *a, const char *from_path, const char *specifier,
                         const char *name, oc_lang_family family);

/* Per-file hints gathered during parsing: shebang (any language) and
 * the Go package_clause name (SPRINT-10) -- the cheap parse facts the
 * entry-point classification consumes (OC006). SPRINT-14 adds the C#
 * declared-namespace list for same-namespace OC005 marking (the same
 * parse-fact channel). */
typedef struct oc_file_hint {
    const char *path;
    int has_shebang;
    int has_main;             /* parsed public-static-main fact (Java, SPRINT-12);
                               * parsed static-Main fact (C#, SPRINT-13) */
    int has_top_statements;   /* parsed top-level-statements fact (C# 9, SPRINT-13) */
    const char *package_name; /* parsed package_clause name; NULL when absent */
    char **namespaces;        /* SPRINT-14: declared dotted namespaces (C#);
                               * NULL when the file declares none */
    size_t n_namespaces;
    char **symbols;           /* SPRINT-22: top-level declaration names (Kotlin,
                               * the C# namespace-hint channel); the resolver
                               * v1-kotlin keys the declared-fact import matching
                               * on it. NULL when none */
    size_t n_symbols;
    char **swift_targets;     /* SPRINT-23: Package.swift declared target names
                               * (the Swift module model: the import specifier
                               * names a compilation TARGET, a fact the file
                               * itself never carries -- the manifest declares
                               * it). Recorded ONLY on the Package.swift file's
                               * own hint. NULL when none */
    char **swift_target_paths; /* the target's declared `path:` argument as
                                * written, or NULL (the SPM documented default
                                * `Sources/<name>` applies -- the resolver owns
                                * that convention). Parallel to swift_targets */
    size_t n_swift_targets;
} oc_file_hint;

typedef struct oc_hint_vec {
    oc_file_hint *items;
    size_t n;
    size_t cap;
} oc_hint_vec;

void oc_surface_hints_init(oc_hint_vec *v);
void oc_surface_hints_destroy(oc_hint_vec *v);
int oc_surface_hint_push(oc_hint_vec *v, oc_arena *a, const char *path, int has_shebang);

/* Records (or extends) the hint for one file with its parsed
 * package_clause name. Find-or-create by path: a file may carry both a
 * shebang and a package fact in principle. Returns 0 on OOM. */
int oc_surface_hint_push_package(oc_hint_vec *v, oc_arena *a, const char *path,
                                 const char *package_name);
int oc_surface_hint_push_main(oc_hint_vec *v, oc_arena *a, const char *path);

/* Records (or extends) the hint for one file with its parsed
 * top-level-statements fact (C# 9: a compilation_unit with global
 * statements IS the program entry point, SPRINT-13). Find-or-create by
 * path. Returns 0 on OOM. */
int oc_surface_hint_push_top(oc_hint_vec *v, oc_arena *a, const char *path);

/* Records (or extends) the hint for one file with one declared dotted
 * namespace (C#, SPRINT-14: same-namespace OC005 marking). Find-or-create
 * by path; a file may declare several; duplicates within one file
 * collapse. Arena-owned storage (grown by re-allocation). Returns 0 on
 * OOM. */
int oc_surface_hint_push_namespace(oc_hint_vec *v, oc_arena *a, const char *path, const char *ns);

/* Records (or extends) the hint for one file with one top-level
 * declaration name (Kotlin, SPRINT-22: the declared-fact resolver
 * channel). Find-or-create by path; a file may declare several;
 * duplicates within one file collapse. Arena-owned storage (grown by
 * re-allocation). Returns 0 on OOM. */
int oc_surface_hint_push_symbol(oc_hint_vec *v, oc_arena *a, const char *path, const char *name);

/* Records (or extends) the hint for one file with one Package.swift
 * declared target (Swift, SPRINT-23: the declared-fact resolver
 * channel -- the manifest declares the module NAME and its declared
 * `path:` as written; NULL path means the SPM documented default
 * `Sources/<name>` applies). Find-or-create by path; one manifest may
 * declare several targets. `path` of the HINT is the manifest file
 * itself. Arena-owned storage (grown by re-allocation). Returns 0 on
 * OOM. */
int oc_surface_hint_push_swift_target(oc_hint_vec *v, oc_arena *a, const char *path,
                                      const char *name, const char *declared_path);

/* Precomputed global state handed to passes through oc_pass_ctx.surface.
 * Entry classification is deterministic and pure: pack entry_points
 * globs, shebang, parsed package_clause (`package main`, SPRINT-10),
 * test-file conventions (see surface.c for the exact list). */
typedef struct oc_surface {
    const oc_graph *g;
    oc_export_vec exports; /* used flags filled in place */
    size_t *in_degree;     /* OC_EDGE_IMPORTS degree per file node id */
    unsigned char *entry;  /* entry-point classified per file node id */
    size_t binds_resolved;
} oc_surface;

/* Builds the surface state. roots/n_roots bound the tsconfig upward
 * search; ps provides pack entry_points globs. Returns NULL on OOM (st
 * set); never on "nothing found" (empty surface is valid). */
oc_surface *oc_surface_build(oc_arena *a, oc_graph *g, const oc_bind_vec *binds,
                             oc_export_vec *exports, const oc_hint_vec *hints,
                             const char *const *roots, size_t n_roots, const oc_pack_set *ps,
                             oc_status *st);

/* Entry-point classification for one analyzed file (exposed for tests). */
int oc_surface_is_entry(const oc_surface *s, const char *path, oc_node_id file_id);

/* Pack entry_points glob matcher ("**" = any segments, "*" = within one
 * segment). Exposed for unit tests. */
int oc_glob_match_entry(const char *pattern, const char *path);

/* Language-blind test-file convention matcher (runner data the test
 * runner executes directly -- a zero in-degree is expected and is never
 * OC006 material): the JS/TS/Python/Go/Java suffixes and names, the
 * Jest/vitest __tests__ directories, and since SPRINT-15 a .rs file
 * directly inside a tests/ directory (cargo compiles each as its own
 * integration-test crate; docs/LANGUAGES.md Rust section 6). */
int oc_path_is_test_convention(const char *path);

#ifdef __cplusplus
}
#endif
#endif /* OC_SURFACE_H */
