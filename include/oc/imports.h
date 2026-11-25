/*
 * Import records and the v1 relative-path resolver (plan WBS 2.x).
 *
 * The parser records raw module specifiers while parsing; resolution runs
 * AFTER every file is parsed, so edges only ever point at files that were
 * actually discovered and analyzed. This keeps the graph honest: an edge
 * means "the target file is part of this analysis", never "some file
 * somewhere might exist".
 *
 * v1 resolution contract (documented in docs/ARCHITECTURE.md):
 *   - JS/TS/TSX: only relative specifiers ("./x", "../x") resolve;
 *     bare specifiers (npm packages) are skipped, not guessed.
 *   - Python: explicit relative imports ("from .x import y") resolve;
 *     absolute specifiers resolve when they match an intra-repo file
 *     (resolver v3, Sprint 4). Stdlib / site-packages names match no
 *     analyzed file and stay unresolved.
 *   - Go (resolver v1-go, SPRINT-10, docs/LANGUAGES.md §Go.5): an import
 *     path resolves to the PACKAGE DIRECTORY it names under an analysis
 *     root, mapped VERBATIM (dots included -- Go's own mapping; the
 *     import path equals the GOPATH-src-relative directory). The whole
 *     package is the target (fan-out edges, package-wide fail-open
 *     surface marking). go.mod is never read; module-prefix, stdlib and
 *     remote specifiers stay unresolved.
 *   - A specifier resolves when a probed candidate path exists as a file
 *     node in the graph. Everything else is left unresolved (recorded in
 *     the input vec, never silently guessed).
 */
#ifndef OC_IMPORTS_H
#define OC_IMPORTS_H

#include "arena.h"
#include "graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Language family of the importing file; drives resolver behavior. */
typedef enum oc_lang_family {
    OC_LANG_JS = 0,
    OC_LANG_PY = 1,
    OC_LANG_GO = 2,
    OC_LANG_JAVA = 3,
    OC_LANG_CSHARP = 4, /* SPRINT-13 (docs/LANGUAGES.md C# section) */
    OC_LANG_RUST = 5,   /* SPRINT-15 (docs/LANGUAGES.md Rust section) */
    OC_LANG_RUBY = 6,   /* SPRINT-16 (docs/LANGUAGES.md Ruby section) */
    OC_LANG_PHP = 7,    /* SPRINT-19 (docs/LANGUAGES.md PHP section) */
    OC_LANG_C = 8,      /* SPRINT-20 (docs/LANGUAGES.md C section) */
    OC_LANG_CPP = 9,    /* SPRINT-21 (docs/LANGUAGES.md C++ section) */
    OC_LANG_KOTLIN = 10, /* SPRINT-22 (docs/LANGUAGES.md Kotlin section) */
    OC_LANG_SWIFT = 11,  /* SPRINT-23 (docs/LANGUAGES.md Swift section) */
    OC_LANG_SCALA = 12   /* SPRINT-24 (docs/LANGUAGES.md Scala section) */
} oc_lang_family;

typedef struct oc_import_rec {
    const char *from_path; /* repo-relative path of the importing file */
    const char *specifier; /* raw module specifier, quotes stripped */
    oc_lang_family family; /* resolver rules to apply */
} oc_import_rec;

typedef struct oc_import_vec {
    oc_import_rec *items; /* heap-owned array; strings live in an arena */
    size_t n;
    size_t cap;
} oc_import_vec;

void oc_imports_init(oc_import_vec *v);
void oc_imports_destroy(oc_import_vec *v);
/* Copies from/spec into the arena and appends a record. Returns 0 on OOM. */
int oc_imports_push(oc_import_vec *v, oc_arena *a, const char *from_path, const char *specifier,
                    oc_lang_family family);

/* Resolves recorded specifiers against file nodes already in the graph and
 * appends OC_EDGE_IMPORTS edges (from file node -> target file node).
 * Duplicate specifiers may produce duplicate edges; passes must tolerate
 * that (they do: cycle detection is SCC-based).
 * Returns the number of edges added, or -1 on allocation failure. */
long oc_imports_resolve(oc_graph *g, const oc_import_vec *in, oc_arena *a);

/* Roots-aware variant: the tsconfig upward search stops at the given
 * analysis roots (the CLI discover roots). Use this from the CLI so edge
 * resolution and surface marking see identical alias behavior. */
long oc_imports_resolve_ex(oc_graph *g, const oc_import_vec *in, oc_arena *a,
                           const char *const *roots, size_t n_roots);

/* Roots + hints variant (SPRINT-22): the hints carry the per-file
 * declared-fact parse data (package clauses, top-level declaration
 * names) that the Kotlin resolver v1-kotlin keys on. The plain _ex
 * variant delegates with NULL hints (every Kotlin specifier stays
 * unresolved there -- the honest degradation). Returns the number of
 * edges added, or -1 on allocation failure. */
struct oc_hint_vec;
typedef struct oc_resolve_ctx oc_resolve_ctx;
long oc_imports_resolve_ex_hints(oc_graph *g, const oc_import_vec *in, oc_arena *a,
                                 const char *const *roots, size_t n_roots,
                                 const struct oc_hint_vec *hints);

/* Attaches the parse-hint vector to a resolve context (SPRINT-22, the
 * Kotlin declared-fact channel). Must be called before the first
 * resolve; the surface build and the scan-path edge pass both set it. */
void oc_resolve_ctx_set_hints(oc_resolve_ctx *cx, const struct oc_hint_vec *hints);

/* ------------------------------------------------------------------ v2
 * Shared specifier resolution (edges AND import-bindings for the module
 * surface). The context owns a sorted path index over the graph's file
 * nodes (binary search instead of the O(n) node scan) and the tsconfig
 * cache used for alias rewriting. roots/n_roots bound the upward
 * tsconfig search at the analysis root. */
typedef struct oc_resolve_ctx oc_resolve_ctx;

/* Returns NULL on allocation failure. roots/n_roots may be 0/NULL: the
 * tsconfig search then stops at the importing file's directory chain
 * top ("." or "/"), never walking above the filesystem. */
oc_resolve_ctx *oc_resolve_ctx_new(oc_arena *a, oc_graph *g, const char *const *roots,
                                   size_t n_roots);

/* Resolves one specifier to a graph file path (arena-owned) or NULL.
 * JS/TS family: relative specifiers plus tsconfig aliases (v2); bare
 * specifiers without a matching alias still skip, never guessed.
 * Python: explicit relative imports (v1) plus absolute intra-repo
 * imports (v3, Sprint 4): package-root probing, then the analysis roots;
 * stdlib/site-packages names never match and stay unresolved.
 * Go: the SINGLE-TARGET view of resolver v1-go -- the package's
 * lexicographically-first discovered file. The edge pass and the surface
 * build enumerate the whole package instead (see below), so this view is
 * for tests and single-file probes. */
const char *oc_resolve_specifier(oc_resolve_ctx *cx, oc_arena *a, const char *from_path,
                                 const char *spec, oc_lang_family family);

/* Go (v1-go): the package directory a dotted specifier names among the
 * analyzed files, or NULL. Root-order probing, direct-children rule,
 * never-guess guards; go.mod is never read (docs/LANGUAGES.md §Go.5).
 * Callers enumerate the package's files by scanning the graph's file
 * nodes directly inside this directory (whole-package fan-out for edges,
 * package-wide fail-open marking for the surface). */
const char *oc_resolve_go_pkg_dir(const oc_resolve_ctx *cx, const char *spec, oc_arena *a);

/* Java (v1-java): the package directory a dotted package path names among
 * the analyzed files, or NULL -- the directory exists iff at least one
 * .java file sits DIRECTLY inside it under an analysis root (root-order
 * probing, same never-guess guards as Go; no classpath, build tool or
 * manifest is ever read, docs/LANGUAGES.md §Java.5). Used for wildcard
 * import claims: whole-package fan-out for edges and fail-open
 * package-level marking for the surface; single-type/static imports
 * resolve through oc_resolve_specifier instead (one class file). */
const char *oc_resolve_java_pkg_dir(const oc_resolve_ctx *cx, const char *spec, oc_arena *a);

/* C# (resolver v1-csharp, SPRINT-13): the NAMESPACE directory a plain
 * using specifier `A.B.C` names among the analyzed files, or NULL --
 * the verbatim dots-to-slashes mapping, probed per analysis root in
 * command-line order; resolves iff at least one discovered .cs file
 * sits DIRECTLY inside. Callers enumerate the namespace's files by
 * scanning the graph's file nodes directly inside this directory
 * (whole-namespace fan-out for edges, fail-open marking for the
 * surface). Static/alias claims never go through this view (see
 * oc_resolve_csharp_type_file). */
const char *oc_resolve_csharp_ns_dir(const oc_resolve_ctx *cx, const char *spec, oc_arena *a);

/* C# (resolver v1-csharp, SPRINT-13): the TYPE file a `static A.B.C`
 * (or the type reading of an alias) claim names, or NULL -- the exact
 * `<root>/A/B/C.cs` probe per analysis root, command-line order. */
const char *oc_resolve_csharp_type_file(const oc_resolve_ctx *cx, const char *spec, oc_arena *a);

/* Rust (resolver v1-rust, SPRINT-15, docs/LANGUAGES.md Rust section 5):
 * the MODULE file a use-tree leaf names, or NULL. The specifier carries
 * its own head (`crate::a::A`, `self::x::y`, `super::a::A`); external
 * crate heads stay unresolved (no manifest is ever read). Anchors are
 * computed from the importing file's own path: the self anchor
 * (dirname, or dirname/stem by Rust's stem rule), the crate anchor (the
 * nearest `src`/`tests` ancestor, with the src/bin own-crate exception),
 * the super anchor (the parent module's anchor). The item-reading pop
 * ladder probes the full path as a module chain, then pops the last
 * segment down to the first; the anchor file itself is never probed.
 * Mod-tree channel records (`mod name`) resolve to the one module file
 * the declaration names: <search-dir>/name.rs or <search-dir>/name/
 * mod.rs, where search-dir follows the same self/stem rule. */
const char *oc_resolve_rust_use_path(const oc_resolve_ctx *cx, const char *from_path,
                                     const char *spec, oc_arena *a);
const char *oc_resolve_rust_mod_decl(const oc_resolve_ctx *cx, const char *from_path,
                                     const char *spec, oc_arena *a);

/* Ruby (resolver v1-ruby, SPRINT-16, docs/LANGUAGES.md Ruby section 5):
 * the file a require record names, or NULL. The specifier carries its own
 * channel tag: `require_relative <payload>` resolves against the
 * requiring file's directory (Ruby's own rule, `.rb` appended); a bare
 * `require <payload>` probes the two documented load-root conventions
 * under the analysis root (`<root>/p.rb`, then `<root>/lib/p.rb` -- the
 * gemspec/bundler lib layout). $LOAD_PATH is never read from any
 * manifest; the probes hit only the analyzed file set, so everything
 * outside stays unresolved by construction. */
const char *oc_resolve_ruby_require(const oc_resolve_ctx *cx, const char *from_path,
                                    const char *spec, oc_arena *a);

/* PHP (resolver v1-php, SPRINT-19, docs/LANGUAGES.md PHP section 5):
 * two channels, both specifiers self-tagged like the Rust/Ruby records.
 * A bare qualified name (no space) is a `use` declaration: it resolves
 * through the composer.json PSR-4/PSR-0 mapping read from the ANALYSIS
 * ROOT (longest-prefix rule, backslash->slash, anchored at the root; a
 * use whose leading prefix matches no composer prefix stays unresolved
 * -- external, never guessed from include_path or runtime autoloading).
 * `require <p>` / `require_once <p>` / `include <p>` /
 * `include_once <p>` resolve against the requiring file's directory
 * (the payload must be one static string, the parser's argument guard;
 * absolute and escape-guarded payloads stay unresolved). Both channels
 * hit only the analyzed file set; the imports relation is a SET. */
const char *oc_resolve_php_use(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                               oc_arena *a);
const char *oc_resolve_php_include(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                                   oc_arena *a);

/* C (resolver v1-c, SPRINT-20, docs/LANGUAGES.md C section 5): the one
 * file an include directive names, or NULL. The specifier carries its own
 * channel tag (the Rust `mod ` precedent): `include <p>` is the quoted
 * form and resolves against the including file's directory (the C
 * preprocessor's own rule, the PHP include-channel discipline); `system
 * <p>` is the angle-bracket form and NEVER resolves -- system headers are
 * outside the analyzed set by definition, recorded-and-unresolved (the
 * Go module-boundary precedent; the dependency signal stays visible in
 * the import records without fabricating a manifest tier). A macro
 * include path (`#include HEADER`) is never recorded at the parser (not
 * a static path). Absolute payloads and backslash separators stay
 * unresolved. Probes hit only the analyzed file set.
 *
 * SPRINT-21: the SAME semantics are resolver v1-cpp (docs/LANGUAGES.md
 * C++ section 5) -- C++ uses the preprocessor's include model unchanged,
 * so the cpp family calls this entry point; the `using` family records
 * NOTHING in v1 (no honest file mapping exists without a manifest). */
const char *oc_resolve_c_include(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                                 oc_arena *a);

/* Kotlin (resolver v1-kotlin, SPRINT-22, docs/LANGUAGES.md Kotlin section
 * 5): the analyzed files an import record names, or NULL. Kotlin's
 * package model is DECOUPLED from the file system (no package==directory
 * rule exists, none is invented), so the resolver keys on DECLARED parse
 * facts only: the per-file package_header clause (recorded as a parse
 * hint, the Go package_clause channel) and the per-file top-level symbol
 * names (the graph's own nodes). `a.b.C` (single, the last segment is
 * the declaration) resolves to every analyzed file whose declared
 * package equals `a.b` AND that declares a top-level symbol `C`;
 * `a.b.*` (the wildcard claim, the Java asterisk-marker precedent)
 * resolves to every analyzed file whose declared package equals `a.b`;
 * a member import `a.b.C.f` resolves through the LONGEST recorded
 * package prefix, the next segment being the claimed symbol. Zero or
 * several matches are both honest: several files may legitimately carry
 * the same top-level declaration in one package, so the edge fans out
 * over all matches (the Go package fan-out discipline, set semantics,
 * lexicographic order). Specifiers never resolve against directories,
 * manifests or build files -- everything outside the declared facts
 * stays unresolved. */
long oc_resolve_kotlin_import(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                              oc_arena *a, const char ***out_targets, size_t *out_n);

/* Resolver v1-swift (SPRINT-23, docs/LANGUAGES.md Swift section 5):
 * the declared-fact resolver, the module spelling. Swift's import
 * specifier names a compilation TARGET -- a fact no source file
 * carries -- so resolution keys on the manifest-declared facts
 * recorded as hints: the per-target (name, declared path) pairs of
 * every analyzed Package.swift. Each analyzed Swift file joins its
 * module by the DECLARED path (an explicit `path:` argument, else the
 * SPM documented default `Sources/<name>`); files outside every
 * declared target stay module-less and are never import targets (none
 * is invented from the directory tree). `M` (whole-module, submodule
 * and kind-scoped-module shapes) resolves to every analyzed file whose
 * declared module equals `M` -- importing a module loads all of it,
 * the fan-out is wholesale (set semantics, lexicographic order). Zero
 * matches and NULL hints are both honest non-resolutions. */
long oc_resolve_swift_import(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                             oc_arena *a, const char ***out_targets, size_t *out_n);

#ifdef __cplusplus
}
#endif
#endif /* OC_IMPORTS_H */
