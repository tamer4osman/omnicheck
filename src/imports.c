#include "oc/imports.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oc/common.h"
#include "oc/json.h"
#include "oc/surface.h"
#include "oc/tsconfig.h"
#include "pathutil.h"

/* The Kotlin declared-fact index entry (SPRINT-22): one per analyzed
 * file that declares a package clause and/or top-level symbols. */
typedef struct {
    const char *pkg; /* the declared package clause, "" = default package */
    const char *path;
    oc_node_id id;
    char **syms;
    size_t n_syms;
} kt_ent;

void oc_imports_init(oc_import_vec *v) {
    v->items = NULL;
    v->n = 0;
    v->cap = 0;
}

void oc_imports_destroy(oc_import_vec *v) {
    free(v->items);
    v->items = NULL;
    v->n = 0;
    v->cap = 0;
}

int oc_imports_push(oc_import_vec *v, oc_arena *a, const char *from_path, const char *specifier,
                    oc_lang_family family) {
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 64;
        oc_import_rec *grown = (oc_import_rec *)realloc(v->items, cap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = cap;
    }
    oc_import_rec *r = &v->items[v->n++];
    r->from_path = oc_arena_strdup(a, from_path);
    r->specifier = oc_arena_strdup(a, specifier);
    r->family = family;
    return r->from_path != NULL && r->specifier != NULL;
}

/* -------------------------------------------------------------- probing */

static int is_file_node_idx(const oc_resolve_ctx *cx, const char *path);

/* JS/TS probing order is explicit and deterministic: exact specifier
 * first (the source said what it wants), then common extensions, then
 * index files, then the TypeScript ".js" -> ".ts" convention. */
static const char *const JS_SUFFIXES[] = {"",           ".js",        ".ts",        ".tsx",
                                          ".mjs",       ".cjs",       ".mts",       ".cts",
                                          "/index.js",  "/index.ts",  "/index.tsx", "/index.mjs",
                                          "/index.cjs", "/index.mts", "/index.cts"};

/* ------------------------------------------------------------ context */

struct oc_resolve_ctx {
    oc_graph *g;
    oc_tsconfig_set *tsconfigs;
    /* Sorted index over file node paths: binary search probes. */
    struct {
        const char **path;
        oc_node_id *id;
        size_t n;
    } index;
    const char *const *roots;
    size_t n_roots;
    /* Kotlin (SPRINT-22): the per-file parse hints -- the declared
     * package_header clauses and top-level declaration names the
     * resolver v1-kotlin keys the declared-fact import matching on
     * (docs/LANGUAGES.md Kotlin section 5). Set by the scan path (the
     * edge pass) and the surface build; NULL means the caller could
     * not provide hints and every Kotlin specifier stays unresolved
     * (the honest degradation: without the declared facts there is no
     * mapping, none is guessed). The declared-fact index over the
     * hints is built lazily on the first Kotlin resolve. */
    const oc_hint_vec *hints;
    struct {
        int built;
        kt_ent *items;
        size_t n;
    } kt;
    /* Swift (SPRINT-23): the declared-fact index over the SAME entry
     * shape -- package_name becomes the declared MODULE (Package.swift
     * target), built lazily on the first Swift resolve. */
    struct {
        int built;
        kt_ent *items;
        size_t n;
    } swift;
    /* PHP (SPRINT-19): the composer.json PSR-4/PSR-0 mapping of the
     * analysis root, loaded lazily on first use. Resolver CONFIG --
     * never an imports edge (docs/LANGUAGES.md PHP section 5). A
     * missing, unreadable or unparseable composer.json means NO config:
     * use declarations stay unresolved (the tsconfig fail-open
     * discipline), never guessed from include_path or runtime
     * autoloading. */
    struct {
        const char *root; /* the analysis root the table was built for */
        struct {
            const char *prefix; /* the map key, trailing backslashes
                                 * normalized away for the match */
            const char *dir;    /* the mapping target as written */
            int psr4;           /* 1 = PSR-4 (prefix stripped), 0 = PSR-0
                                 * (full name path kept) */
        } *items;
        size_t n;
        int loaded; /* 0 = not yet, 1 = loaded (possibly empty) */
    } composer;
};

static int cmp_index_entry(const void *a, const void *b) {
    /* Entries compared by path; called through qsort on a parallel-array
     * build is awkward, so the index is built by sorting a temp array of
     * structs and splitting. This comparator is for that temp array. */
    const struct {
        const char *path;
        oc_node_id id;
    } *x = a, *y = b;
    return strcmp(x->path, y->path);
}

oc_resolve_ctx *oc_resolve_ctx_new(oc_arena *a, oc_graph *g, const char *const *roots,
                                   size_t n_roots) {
    oc_resolve_ctx *cx = (oc_resolve_ctx *)oc_arena_alloc(a, sizeof(*cx));
    if (!cx)
        return NULL;
    memset(cx, 0, sizeof(*cx));
    cx->g = g;
    cx->roots = roots;
    cx->n_roots = n_roots;
    cx->tsconfigs = oc_tsconfig_set_new(a);
    if (!cx->tsconfigs)
        return NULL;

    size_t n_files = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].kind == OC_NODE_FILE)
            n_files++;
    }
    typedef struct {
        const char *path;
        oc_node_id id;
    } idx_ent;
    idx_ent *tmp = (idx_ent *)malloc((n_files ? n_files : 1) * sizeof(idx_ent));
    if (!tmp)
        return NULL;
    size_t w = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].kind == OC_NODE_FILE) {
            tmp[w].path = g->nodes[i].path;
            tmp[w].id = (oc_node_id)i;
            w++;
        }
    }
    qsort(tmp, w, sizeof(idx_ent), cmp_index_entry);
    /* Arena-owned: the context lives for the whole analysis; heap arrays
     * here leaked (LSan, Sprint 2 dev). */
    const char **paths = (const char **)oc_arena_alloc(a, (w ? w : 1) * sizeof(char *));
    oc_node_id *ids = (oc_node_id *)oc_arena_alloc(a, (w ? w : 1) * sizeof(oc_node_id));
    if (!paths || !ids) {
        free(tmp);
        return NULL;
    }
    for (size_t i = 0; i < w; i++) {
        paths[i] = tmp[i].path;
        ids[i] = tmp[i].id;
    }
    free(tmp);
    cx->index.path = paths;
    cx->index.id = ids;
    cx->index.n = w;
    return cx;
}

static int is_file_node_idx(const oc_resolve_ctx *cx, const char *path) {
    size_t lo = 0, hi = cx->index.n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(cx->index.path[mid], path);
        if (c == 0)
            return 1;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

/* Longest analysis root that is a DIRECTORY ancestor of the file, for
 * bounding the tsconfig upward search and anchoring the dirname-joined
 * resolvers. BUGFIX N-6: the old boundary rule let an explicit FILE
 * root (oc check f0.c f1.c ...) match from_path exactly
 * (from_path[rl] == '\0'), so the "analysis root" became the file
 * itself and every dirname-anchored join was prefixed with the full
 * file path -- C/C++ quoted includes, Ruby require_relative and the
 * Python absolute-import fallback all went dead on file-root scans
 * (the chunked nodejs/node rerun resolved ZERO quoted includes:
 * header cycles like LIEF utfcpp went unreported and parse-error
 * headers surfaced as phantom OC006). Directory-root scans never saw
 * it: a directory root is always a strict path-prefix. A root now
 * qualifies only when from_path CONTINUES with '/' past the root (a
 * strict directory ancestor); the "." root keeps its documented
 * lowest-priority meaning. Falls back to "." semantics. */
static const char *analysis_root_for(const oc_resolve_ctx *cx, const char *from_path) {
    const char *best = NULL;
    size_t best_len = 0;
    for (size_t i = 0; i < cx->n_roots; i++) {
        const char *r = cx->roots[i];
        size_t rl = strlen(r);
        if (rl == 0)
            continue;
        if (strcmp(r, ".") == 0) {
            if (1 > best_len) {
                best = r;
                best_len = 1;
            }
            continue;
        }
        if (strncmp(from_path, r, rl) == 0 && from_path[rl] == '/' && rl > best_len) {
            best = r;
            best_len = rl;
        }
    }
    return best ? best : ".";
}

/* ------------------------------------------------------- JS resolving -- */

/* Probes one joined candidate path through the suffix ladder. Returns an
 * arena copy of the matched graph path, or NULL. */
static const char *probe_js(const oc_resolve_ctx *cx, oc_arena *a, const char *joined) {
    char cand[1100];
    for (size_t i = 0; i < sizeof(JS_SUFFIXES) / sizeof(JS_SUFFIXES[0]); i++) {
        if (snprintf(cand, sizeof(cand), "%s%s", joined, JS_SUFFIXES[i]) >= (int)sizeof(cand))
            continue;
        if (is_file_node_idx(cx, cand))
            return oc_arena_strdup(a, cand);
    }
    /* TypeScript ".js" convention: "./x.js" may mean "./x.ts" on disk. */
    size_t jl = strlen(joined);
    if (jl > 3 && strcmp(joined + jl - 3, ".js") == 0) {
        static const char *const TS_ALTS[] = {".ts", ".tsx"};
        for (size_t i = 0; i < 2; i++) {
            if (snprintf(cand, sizeof(cand), "%.*s%s", (int)(jl - 3), joined, TS_ALTS[i]) >=
                (int)sizeof(cand))
                continue;
            if (is_file_node_idx(cx, cand))
                return oc_arena_strdup(a, cand);
        }
    }
    return NULL;
}

static const char *resolve_js(const oc_resolve_ctx *cx, oc_arena *a, const char *from_path,
                              const char *spec) {
    char dir[1024];
    oc_path_dirname(from_path, dir, sizeof(dir));

    if (spec[0] == '.' && (spec[1] == '/' || spec[1] == '.')) {
        char joined[1024];
        if (!oc_path_join(joined, sizeof(joined), dir, spec))
            return NULL;
        oc_path_normalize(joined);
        return probe_js(cx, a, joined);
    }
    if (spec[0] == '/')
        return NULL; /* absolute: outside the analysis, never guessed */

    /* Resolver v2: bare specifiers go through the nearest tsconfig. */
    const char *root = analysis_root_for(cx, from_path);
    const oc_tsconfig *tc = oc_tsconfig_for(cx->tsconfigs, a, dir, root, NULL);
    char rewritten[1024];
    if (tc && oc_tsconfig_rewrite(tc, spec, rewritten, sizeof(rewritten)))
        return probe_js(cx, a, rewritten);
    return NULL; /* bare specifier without an alias: skipped by design */
}

/* ------------------------------------------------------ PY resolving --- */

/* Probes one joined module path through the Python suffix ladder
 * (<mod>.py, then <mod>/__init__.py). Deterministic: file order within
 * the ladder is fixed, first graph match wins. */
static const char *probe_py_mod(const oc_resolve_ctx *cx, oc_arena *a, const char *base,
                                const char *dotted_path) {
    char modpath[1100];
    if (!oc_path_join(modpath, sizeof(modpath), base, dotted_path))
        return NULL;
    oc_path_normalize(modpath);
    char cand[1200];
    static const char *const PY_SUFFIXES[] = {".py", "/__init__.py"};
    for (size_t i = 0; i < 2; i++) {
        if (snprintf(cand, sizeof(cand), "%s%s", modpath, PY_SUFFIXES[i]) >= (int)sizeof(cand))
            continue;
        if (is_file_node_idx(cx, cand))
            return oc_arena_strdup(a, cand);
    }
    return NULL;
}

/* Resolver v3 (Sprint 4): absolute intra-repo imports ("import pkg.mod",
 * "from pkg.sub import x"). The importing file's package root is its
 * nearest ancestor directory that is NOT a Python package (no
 * __init__.py among the analyzed files) -- for packaged code that is the
 * repository root, for loose scripts the scripts directory itself.
 * Probing starts at that root, then falls back to each analysis root in
 * command-line order (src-layout / monorepo bases). Specifiers that match
 * no intra-repo file (stdlib, site-packages) stay unresolved -- never
 * guessed, so an edge still always means "target is part of this
 * analysis". */
static const char *resolve_py_absolute(const oc_resolve_ctx *cx, oc_arena *a, const char *from_path,
                                       const char *spec) {
    char dotted[1024];
    snprintf(dotted, sizeof(dotted), "%s", spec);
    for (char *p = dotted; *p; p++) {
        if (*p == '.')
            *p = '/';
    }

    /* Package root: climb while the directory is a package. */
    char dir[1024];
    oc_path_dirname(from_path, dir, sizeof(dir));
    for (int depth = 0; depth < 64; depth++) {
        char init_cand[1100];
        if (snprintf(init_cand, sizeof(init_cand), "%s/__init__.py", dir) >= (int)sizeof(init_cand))
            break;
        oc_path_normalize(init_cand); /* dirname may yield "." ("./__init__.py") */
        if (!is_file_node_idx(cx, init_cand))
            break; /* not a package: this dir is the package root */
        char up[1024];
        oc_path_up(dir, 1, up, sizeof(up));
        if (strcmp(up, dir) == 0)
            break; /* filesystem top */
        snprintf(dir, sizeof(dir), "%s", up);
    }

    const char *hit = probe_py_mod(cx, a, dir, dotted);
    if (hit)
        return hit;
    for (size_t i = 0; i < cx->n_roots; i++) {
        const char *r = cx->roots[i];
        if (!r || r[0] == '\0')
            continue;
        hit = probe_py_mod(cx, a, r, dotted);
        if (hit)
            return hit;
    }
    return NULL;
}

/* Python probing: explicit relative imports ("from .x import y") keep the
 * v1 contract; absolute imports resolve via resolver v3 (Sprint 4).
 * Level-1 dots -> the importing file's directory; each extra dot climbs
 * one package level. Then <mod>.py or <mod>/__init__.py. */
static const char *resolve_py(const oc_resolve_ctx *cx, oc_arena *a, const char *from_path,
                              const char *spec) {
    size_t dots = 0;
    while (spec[dots] == '.')
        dots++;
    if (dots == 0)
        return resolve_py_absolute(cx, a, from_path, spec);
    const char *rest = spec + dots; /* may be "" */

    char start[1024];
    oc_path_dirname(from_path, start, sizeof(start));
    char base[1024];
    oc_path_up(start, dots - 1, base, sizeof(base));

    char modpath[1100];
    if (rest[0] != '\0') {
        char dotted[1024];
        snprintf(dotted, sizeof(dotted), "%s", rest);
        for (char *p = dotted; *p; p++) {
            if (*p == '.')
                *p = '/';
        }
        if (!oc_path_join(modpath, sizeof(modpath), base, dotted))
            return NULL;
        oc_path_normalize(modpath);
    } else {
        snprintf(modpath, sizeof(modpath), "%s", base); /* "from . import x": package dir */
    }

    char cand[1200];
    static const char *const PY_SUFFIXES[] = {".py", "/__init__.py"};
    for (size_t i = 0; i < 2; i++) {
        if (snprintf(cand, sizeof(cand), "%s%s", modpath, PY_SUFFIXES[i]) >= (int)sizeof(cand))
            continue;
        if (is_file_node_idx(cx, cand))
            return oc_arena_strdup(a, cand);
    }
    return NULL;
}

/* ------------------------------------------------------ GO resolving -- */

/* Resolver v1-go (SPRINT-10, docs/LANGUAGES.md §Go.5): an import path
 * resolves iff the package directory it names exists among the analyzed
 * file set, under an analysis root. Mechanics, all deterministic and
 * filesystem-free (the analyzed file set IS the universe):
 *   - VERBATIM mapping: the candidate directory is <root>/<specifier>
 *     exactly as written, dots included -- Go's own mapping (the import
 *     path equals the GOPATH-src-relative directory; dotted hosts like
 *     "golang.org" are literal directory names). The SPRINT-9 wording
 *     "dotted import paths" was ambiguous; the real-corpus gate proved
 *     a Python-style dot-to-slash conversion wrong (it would destroy
 *     "golang.org") and the contract amendment is recorded in §Go.5.
 *     Roots are probed in command-line order, the same discipline as
 *     the Python absolute-import resolver;
 *   - DIRECT-CHILDREN rule: the package is the discovered .go files
 *     directly inside that directory; subdirectories are separate
 *     packages and are never claimed by the parent's specifier;
 *   - the whole package is the target of resolution (Go compiles whole
 *     packages): the edge pass fans one record out to every direct
 *     child, and the surface build marks the whole package used;
 *   - never-guess guards: specifiers starting with "." or "/", or
 *     containing "."/".."/empty SEGMENTS, stay unresolved (no path
 *     escapes); stdlib and remote paths simply exist in no analyzed
 *     directory, so they stay unresolved too;
 *   - NO manifest reading: go.mod is never consulted and module-prefix
 *     stripping is out of scope (WBS 4.x precedent), so a module-prefix
 *     specifier ("mycorp/store" for module "mycorp") stays unresolved
 *     even when store/ exists in the tree -- the gate pins this. */
static int go_spec_slash(const char *spec, char *out, size_t cap) {
    if (spec[0] == '\0' || spec[0] == '/' || spec[0] == '.')
        return 0;
    size_t o = 0, i = 0;
    while (1) {
        size_t start = i;
        while (spec[i] != '\0' && spec[i] != '/')
            i++;
        size_t seg = i - start;
        if (seg == 0)
            return 0; /* empty segment: "a//b", "a/", trailing slash */
        if (seg == 1 && spec[start] == '.')
            return 0; /* "." segment */
        if (seg == 2 && spec[start] == '.' && spec[start + 1] == '.')
            return 0; /* ".." segment: no path escape, ever */
        if (o + seg + 1 >= cap)
            return 0;
        if (o > 0)
            out[o++] = '/';
        memcpy(out + o, spec + start, seg);
        o += seg;
        out[o] = '\0';
        if (spec[i] == '\0')
            return o > 0;
        i++; /* skip the slash; next iteration reads the next segment */
    }
}

/* Forward iterator over the discovered files DIRECTLY inside a package
 * directory, served from the sorted path index (the "dir/" block is
 * contiguous in lexicographic order, so a lower-bound probe finds it).
 * Direct-children rule: entries with a further "/" are subdirectory
 * files and are skipped -- subdirectories are separate packages.
 * _test.go files are ALSO skipped: Go compiles a package import without
 * the directory's test files (they belong to the package's own test
 * binary, never to its importers), and an external test package
 * (package foo_test) shares the directory but is not the imported
 * package. Letting the fan-out target test files would fabricate SCCs
 * between same-directory test files that merely import their own
 * package -- measured on the real-corpus gate, fixed before pinning
 * (docs/LANGUAGES.md §Go.5). */
typedef struct {
    const oc_resolve_ctx *cx;
    size_t i;
    size_t dl;
    char prefix[1250];
} go_pkg_iter;

static int go_pkg_iter_init(go_pkg_iter *it, const oc_resolve_ctx *cx, const char *dir) {
    it->cx = cx;
    it->dl = strlen(dir);
    if (it->dl + 1 >= sizeof(it->prefix))
        return 0;
    memcpy(it->prefix, dir, it->dl);
    it->prefix[it->dl] = '/';
    it->prefix[it->dl + 1] = '\0';
    size_t lo = 0, hi = cx->index.n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (strcmp(cx->index.path[mid], it->prefix) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    it->i = lo;
    return 1;
}

/* Next direct-child, non-test file of the package: node id (or
 * OC_NODE_INVALID at exhaustion) and the index-owned path. */
static oc_node_id go_pkg_iter_next(go_pkg_iter *it, const char **path_out) {
    const oc_resolve_ctx *cx = it->cx;
    while (it->i < cx->index.n) {
        const char *p = cx->index.path[it->i];
        oc_node_id id = cx->index.id[it->i];
        it->i++;
        if (strncmp(p, it->prefix, it->dl + 1) != 0)
            break; /* left the package block: package exhausted */
        if (strchr(p + it->dl + 1, '/') != NULL)
            continue; /* subdirectory file: separate package */
        const char *base = strrchr(p, '/');
        base = base ? base + 1 : p;
        size_t bl = strlen(base);
        if (bl > 8 && strcmp(base + bl - 8, "_test.go") == 0)
            continue; /* test files never join an import */
        if (path_out)
            *path_out = p;
        return id;
    }
    return OC_NODE_INVALID;
}

/* The package directory a specifier names among the analyzed files, or
 * NULL. Arena-owned result. Never touches the filesystem. */
const char *oc_resolve_go_pkg_dir(const oc_resolve_ctx *cx, const char *spec, oc_arena *a) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    char slashed[1024];
    if (!go_spec_slash(spec, slashed, sizeof(slashed)))
        return NULL;
    for (size_t i = 0; i < cx->n_roots; i++) {
        const char *r = cx->roots[i];
        if (!r || r[0] == '\0')
            continue;
        char cand[1200];
        if (!oc_path_join(cand, sizeof(cand), r, slashed))
            continue;
        oc_path_normalize(cand);
        /* The package exists iff at least one discovered file node sits
         * DIRECTLY inside the candidate directory. */
        go_pkg_iter it;
        if (!go_pkg_iter_init(&it, cx, cand))
            continue;
        if (go_pkg_iter_next(&it, NULL) != OC_NODE_INVALID)
            return oc_arena_strdup(a, cand);
    }
    return NULL;
}

/* Single-target view of a Go package: the lexicographically-first
 * discovered file directly inside its directory (graph order is sorted,
 * so this is deterministic). Multi-target consumers enumerate the whole
 * package instead. */
static const char *resolve_go_first(const oc_resolve_ctx *cx, oc_arena *a, const char *spec) {
    const char *dir = oc_resolve_go_pkg_dir(cx, spec, a);
    if (!dir)
        return NULL;
    go_pkg_iter it;
    if (!go_pkg_iter_init(&it, cx, dir))
        return NULL;
    const char *p = NULL;
    if (go_pkg_iter_next(&it, &p) == OC_NODE_INVALID)
        return NULL;
    return p; /* index strings live for the whole analysis */
}

/* ---------------------------------------------------- JAVA resolving -- */

/* Resolver v1-java (SPRINT-12, docs/LANGUAGES.md Java section): an
 * import path resolves against the discovered file set only. Mechanics,
 * all deterministic and filesystem-free (the analyzed file set IS the
 * universe):
 *   - single-type import `a.b.C` (and the static-import qualifier
 *     `a.b.C`, member segment already stripped by the parser): maps
 *     VERBATIM to `a/b/C.java` (dots to slashes, `.java` appended) and
 *     resolves when that EXACT file exists below an analysis root
 *     (roots probed in command-line order -- the same root-order
 *     discipline as the Python absolute-import resolver and v1-go);
 *   - wildcard import `a.b.*`: a PACKAGE claim -- resolves iff the
 *     directory `a/b` holds at least one discovered .java file directly
 *     inside it; the whole package is the target (fan-out edges to
 *     every direct-child .java file, like v1-go's whole-package
 *     semantics);
 *   - never-guess guards: empty segments, `.`/`..` segments, absolute
 *     and empty paths stay unresolved (no path escapes); nested classes
 *     (`a.b.C.D`) probe exactly `a/b/C/D.java` and nothing else -- the
 *     class-file model is the only mapping v1 supports;
 *   - NO manifest/build-tool reading: classpath and Gradle/Maven
 *     dependency resolution is out of scope (WBS 4.x precedent), so
 *     external packages stay unresolved; the package clause is never
 *     cross-checked against the directory (javac's job, not the
 *     resolver's). */
static int java_spec_slash(const char *spec, char *out, size_t cap) {
    if (spec[0] == '\0' || spec[0] == '/' || spec[0] == '.')
        return 0;
    size_t o = 0, i = 0;
    while (1) {
        size_t start = i;
        while (spec[i] != '\0' && spec[i] != '.')
            i++;
        size_t seg = i - start;
        if (seg == 0)
            return 0; /* empty segment: "a..b", "a.", trailing dot */
        if (seg == 1 && spec[start] == '.')
            return 0; /* unreachable after the '.' split, kept symmetric */
        if (seg == 2 && spec[start] == '.' && spec[start + 1] == '.')
            return 0; /* ".." segment: no path escape, ever */
        if (o + seg + 1 >= cap)
            return 0;
        if (o > 0)
            out[o++] = '/';
        memcpy(out + o, spec + start, seg);
        o += seg;
        out[o] = '\0';
        if (spec[i] == '\0')
            return o > 0;
        i++; /* skip the dot; next iteration reads the next segment */
    }
}

/* Forward iterator over the discovered .java files DIRECTLY inside a
 * package directory, served from the sorted path index (same lower-bound
 * probe as the Go iterator). Direct-children rule: entries with a
 * further "/" are subdirectory files (other packages) and are skipped.
 * Unlike Go there is no test-file exclusion: a Java wildcard import
 * claims the whole package as analyzed (the class-file model has no
 * "compiles without test files" rule), and the JDK-standard metadata
 * files (package-info.java, module-info.java) join the claim like any
 * other file of the package -- uniform, never special-cased
 * (docs/LANGUAGES.md §Java.5). */
typedef struct {
    const oc_resolve_ctx *cx;
    size_t i;
    size_t dl;
    char prefix[1250];
} java_pkg_iter;

static int java_pkg_iter_init(java_pkg_iter *it, const oc_resolve_ctx *cx, const char *dir) {
    it->cx = cx;
    it->dl = strlen(dir);
    if (it->dl + 1 >= sizeof(it->prefix))
        return 0;
    memcpy(it->prefix, dir, it->dl);
    it->prefix[it->dl] = '/';
    it->prefix[it->dl + 1] = '\0';
    size_t lo = 0, hi = cx->index.n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (strcmp(cx->index.path[mid], it->prefix) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    it->i = lo;
    return 1;
}

static oc_node_id java_pkg_iter_next(java_pkg_iter *it, const char **path_out) {
    const oc_resolve_ctx *cx = it->cx;
    while (it->i < cx->index.n) {
        const char *p = cx->index.path[it->i];
        oc_node_id id = cx->index.id[it->i];
        it->i++;
        if (strncmp(p, it->prefix, it->dl + 1) != 0)
            break; /* left the package block: package exhausted */
        if (strchr(p + it->dl + 1, '/') != NULL)
            continue; /* subdirectory file: separate package */
        const char *base = strrchr(p, '/');
        base = base ? base + 1 : p;
        size_t bl = strlen(base);
        if (bl <= 5 || strcmp(base + bl - 5, ".java") != 0)
            continue; /* only .java files are package members */
        if (path_out)
            *path_out = p;
        return id;
    }
    return OC_NODE_INVALID;
}

const char *oc_resolve_java_pkg_dir(const oc_resolve_ctx *cx, const char *spec, oc_arena *a) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    size_t sl = strlen(spec);
    if (sl >= 2 && strcmp(spec + sl - 2, ".*") == 0)
        sl -= 2; /* a wildcard claims the package named WITHOUT the ".*" */
    char dotted[1024];
    if (sl >= sizeof(dotted))
        return NULL;
    memcpy(dotted, spec, sl);
    dotted[sl] = '\0';
    char slashed[1024];
    if (!java_spec_slash(dotted, slashed, sizeof(slashed)))
        return NULL;
    for (size_t i = 0; i < cx->n_roots; i++) {
        const char *r = cx->roots[i];
        if (!r || r[0] == '\0')
            continue;
        char cand[1200];
        if (!oc_path_join(cand, sizeof(cand), r, slashed))
            continue;
        oc_path_normalize(cand);
        /* The package exists iff at least one discovered .java file sits
         * DIRECTLY inside the candidate directory. */
        java_pkg_iter it;
        if (!java_pkg_iter_init(&it, cx, cand))
            continue;
        if (java_pkg_iter_next(&it, NULL) != OC_NODE_INVALID)
            return oc_arena_strdup(a, cand);
    }
    return NULL;
}

/* Single-target view of the v1-java class-file model: the file a/b/C.java
 * under the first analysis root that contains it. Wildcard package claims
 * never take this path (callers branch on the ".*" suffix first). */
static const char *resolve_java_first(const oc_resolve_ctx *cx, oc_arena *a, const char *from_path,
                                      const char *spec) {
    (void)from_path; /* Java qualifiers are absolute package paths; the
                      * importing file's location never narrows them */
    char slashed[1024];
    int slash_rc = java_spec_slash(spec, slashed, sizeof(slashed));
    if (!slash_rc)
        return NULL;
    for (size_t i = 0; i < cx->n_roots; i++) {
        const char *r = cx->roots[i];
        if (!r || r[0] == '\0')
            continue;
        char cand[1200];
        if (!oc_path_join(cand, sizeof(cand), r, slashed))
            continue;
        oc_path_normalize(cand);
        /* Append ".java" into a SECOND buffer: snprintf must never take
         * its own output as a %s argument (undefined behaviour -- caught
         * live by the resolver fixtures: the fortified write zeroed the
         * buffer first and every single-type import silently resolved
         * to NULL). */
        char withext[1250];
        if (snprintf(withext, sizeof(withext), "%s.java", cand) >= (int)sizeof(withext))
            continue;
        if (is_file_node_idx(cx, withext))
            return oc_arena_strdup(a, withext);
    }
    return NULL;
}

static int csharp_spec_ok(const char *spec) {
    if (spec[0] == '\0' || spec[0] == '/' || spec[0] == '.')
        return 0;
    size_t start = 0;
    for (size_t i = 0;; i++) {
        char c = spec[i];
        if (c == '.' || c == '\0') {
            size_t seg = i - start;
            if (seg == 0)
                return 0; /* empty dot-segment: "A..B", trailing/leading '.' */
            if (seg == 1 && spec[start] == '.')
                return 0;
            if (seg == 2 && spec[start] == '.' && spec[start + 1] == '.')
                return 0;
            for (size_t k = start; k < i; k++) {
                char s = spec[k];
                int ok = (s >= 'A' && s <= 'Z') || (s >= 'a' && s <= 'z') ||
                         (s >= '0' && s <= '9') || s == '_';
                if (!ok)
                    return 0; /* generics, tuples, pointers, '::': never re-mapped */
            }
            if (c == '\0')
                break;
            start = i + 1;
        }
    }
    return 1;
}

/* Forward iterator over the discovered .cs files DIRECTLY inside a
 * namespace directory (the "dir/" block is contiguous in lexicographic
 * order, so a lower-bound probe finds it). Direct-children rule:
 * entries with a further "/" are subdirectory files -- separate
 * namespaces, never claimed by the parent's specifier. Like Java there
 * is no test-file exclusion: C# test trees follow build conventions,
 * not import semantics, and a using directive of the namespace
 * genuinely names every file the convention places there. */
typedef struct {
    const oc_resolve_ctx *cx;
    size_t i;
    size_t dl;
    char prefix[1250];
} csharp_pkg_iter;

static int csharp_pkg_iter_init(csharp_pkg_iter *it, const oc_resolve_ctx *cx, const char *dir) {
    it->cx = cx;
    it->dl = strlen(dir);
    if (it->dl + 1 >= sizeof(it->prefix))
        return 0;
    memcpy(it->prefix, dir, it->dl);
    it->prefix[it->dl] = '/';
    it->prefix[it->dl + 1] = '\0';
    size_t lo = 0, hi = cx->index.n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (strcmp(cx->index.path[mid], it->prefix) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    it->i = lo;
    return 1;
}

static oc_node_id csharp_pkg_iter_next(csharp_pkg_iter *it, const char **path_out) {
    const oc_resolve_ctx *cx = it->cx;
    while (it->i < cx->index.n) {
        const char *p = cx->index.path[it->i];
        oc_node_id id = cx->index.id[it->i];
        it->i++;
        if (strncmp(p, it->prefix, it->dl + 1) != 0)
            break; /* left the namespace block: exhausted */
        if (strchr(p + it->dl + 1, '/') != NULL)
            continue; /* subdirectory file: separate namespace */
        size_t pl = strlen(p);
        if (pl < 3 || strcmp(p + pl - 3, ".cs") != 0)
            continue; /* non-.cs files never join a namespace import */
        if (path_out)
            *path_out = p;
        return id;
    }
    return OC_NODE_INVALID;
}

/* The NAMESPACE directory a plain specifier `A.B.C` names among the
 * analyzed files, or NULL. Arena-owned result. Never touches the
 * filesystem. Static/alias claims must not be passed here.
 *
 * The C# ecosystem ships TWO directory layout conventions, probed in a
 * DOCUMENTED ORDER (first hit wins, never both -- the JS suffix-ladder
 * discipline): (1) dots mapped to slashes, the SDK/VS convention the
 * .NET tooling mandates for new projects; (2) the dotted single-
 * directory form, mono's historical mcs layout (real-world trees ship
 * it -- the pinned corpus proved the pre-adoption single-convention
 * wording wrong, the SPRINT-10 golang.org lesson repeating; the
 * amendment is recorded in docs/LANGUAGES.md C# section 5). */
static const char *csharp_probe_ns_at_root(const oc_resolve_ctx *cx, const char *r,
                                           const char *slashed, const char *dotted, oc_arena *a) {
    char cand[1200];
    if (oc_path_join(cand, sizeof(cand), r, slashed)) {
        oc_path_normalize(cand);
        csharp_pkg_iter it;
        if (csharp_pkg_iter_init(&it, cx, cand) &&
            csharp_pkg_iter_next(&it, NULL) != OC_NODE_INVALID)
            return oc_arena_strdup(a, cand);
    }
    if (oc_path_join(cand, sizeof(cand), r, dotted)) {
        oc_path_normalize(cand);
        csharp_pkg_iter it;
        if (csharp_pkg_iter_init(&it, cx, cand) &&
            csharp_pkg_iter_next(&it, NULL) != OC_NODE_INVALID)
            return oc_arena_strdup(a, cand);
    }
    return NULL;
}

const char *oc_resolve_csharp_ns_dir(const oc_resolve_ctx *cx, const char *spec, oc_arena *a) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    if (!csharp_spec_ok(spec))
        return NULL;
    char slashed[1024];
    snprintf(slashed, sizeof(slashed), "%s", spec);
    for (char *p = slashed; *p; p++) {
        if (*p == '.')
            *p = '/';
    }
    for (size_t i = 0; i < cx->n_roots; i++) {
        const char *r = cx->roots[i];
        if (!r || r[0] == '\0')
            continue;
        const char *hit = csharp_probe_ns_at_root(cx, r, slashed, spec, a);
        if (hit)
            return hit;
    }
    return NULL;
}

/* The TYPE file a `static A.B.C` (or alias type reading) claim names,
 * or NULL: the exact `<root>/A/B/C.cs` probe per analysis root,
 * command-line order. */
const char *oc_resolve_csharp_type_file(const oc_resolve_ctx *cx, const char *spec, oc_arena *a) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    if (!csharp_spec_ok(spec))
        return NULL;
    char slashed[1024];
    snprintf(slashed, sizeof(slashed), "%s", spec);
    for (char *p = slashed; *p; p++) {
        if (*p == '.')
            *p = '/';
    }
    for (size_t i = 0; i < cx->n_roots; i++) {
        const char *r = cx->roots[i];
        if (!r || r[0] == '\0')
            continue;
        char cand[1200];
        /* convention (1): the slash-mapped file .../A/B/C.cs */
        if (snprintf(cand, sizeof(cand), "%s/%s.cs", r, slashed) >= (int)sizeof(cand))
            continue;
        oc_path_normalize(cand);
        if (is_file_node_idx(cx, cand))
            return oc_arena_strdup(a, cand);
        /* convention (2): the dotted single-directory form A.B.C.cs */
        if (snprintf(cand, sizeof(cand), "%s/%s.cs", r, spec) >= (int)sizeof(cand))
            continue;
        oc_path_normalize(cand);
        if (is_file_node_idx(cx, cand))
            return oc_arena_strdup(a, cand);
    }
    return NULL;
}

/* Single-target view of a namespace claim: the lexicographically-first
 * discovered .cs file directly inside its directory. */
static const char *resolve_csharp_first(const oc_resolve_ctx *cx, oc_arena *a, const char *spec) {
    const char *dir = oc_resolve_csharp_ns_dir(cx, spec, a);
    if (!dir)
        return NULL;
    csharp_pkg_iter it;
    if (!csharp_pkg_iter_init(&it, cx, dir))
        return NULL;
    const char *p = NULL;
    if (csharp_pkg_iter_next(&it, &p) == OC_NODE_INVALID)
        return NULL;
    return p; /* index strings live for the whole analysis */
}

/* An alias claim `X = A.B.C`: the DOCUMENTED ORDERED PROBE -- the type
 * file A/B/C.cs first, then the namespace directory A/B/C (first hit
 * wins, never both; in compile-legal code the two readings cannot
 * coexist). */
static const char *resolve_csharp_alias(const oc_resolve_ctx *cx, oc_arena *a, const char *spec) {
    const char *target = oc_resolve_csharp_type_file(cx, spec, a);
    if (target)
        return target;
    return resolve_csharp_first(cx, a, spec);
}

/* ----------------------------------------------------- RUST resolving -- */

/* Resolver v1-rust (SPRINT-15, docs/LANGUAGES.md Rust section 5): two
 * declaration-level channels, all deterministic and filesystem-free (the
 * analyzed file set IS the universe):
 *   - mod-tree channel (`mod name` records): the declaring file F names
 *     the search directory D = dirname(F) when basename(F) is lib.rs,
 *     main.rs or mod.rs (the directory IS the module), else
 *     dirname(F)/stem(F) (Rust's stem rule). The record resolves to the
 *     exact file D/name.rs or D/name/mod.rs -- one edge, never a
 *     fan-out: a mod declaration names ONE module file;
 *   - use-channel (`crate::...` / `self::...` / `super::...` leaves):
 *     the anchor is the file's own module directory. The self anchor
 *     A(F) follows the same stem rule; the crate anchor R(F) is the
 *     nearest ancestor directory named `src` or `tests` (with the
 *     src/bin own-crate exception: files directly inside `<src>/bin`
 *     are their own crates); the super anchor is dirname(A(F)), and
 *     `super::` from a file whose self anchor IS the crate anchor
 *     stays unresolved (the crate has no parent module). Heads that are
 *     not crate/self/super are external crate names -- unresolved
 *     ALWAYS, no manifest is ever read (the go.mod decoy discipline).
 *   - the ITEM-READING POP LADDER at the anchor: probe the full path as
 *     a module chain (<anchor>/p1/.../pN.rs, then .../pN/mod.rs), then
 *     pop the last segment and repeat down to p1 -- the deepest module
 *     file the path names is the edge target. The ladder never probes
 *     the anchor file itself (K >= 1).
 *   - never-guess guards: empty/`.`/`..` segments, absolute paths and
 *     everything outside the analyzed set stay unresolved. */

/* Rust path segments: split on "::"; every segment must be non-empty
 * and free of `.`/`/` escapes (Rust paths cannot legally contain them;
 * a malformed record is dropped, never re-mapped). Writes the segments
 * as a slash-joined relative path into out; returns 0 on any guard. */
static int rust_spec_segments(const char *spec, char *out, size_t cap, size_t *n_seg) {
    if (spec[0] == '\0' || spec[0] == '/')
        return 0;
    size_t o = 0, i = 0, segs = 0;
    while (1) {
        size_t start = i;
        while (spec[i] != '\0' && !(spec[i] == ':' && spec[i + 1] == ':'))
            i++;
        size_t seg = i - start;
        if (seg == 0)
            return 0; /* empty segment: "crate::", "a::::b", trailing :: */
        if (seg == 1 && spec[start] == '.')
            return 0; /* "." segment */
        if (seg == 2 && spec[start] == '.' && spec[start + 1] == '.')
            return 0; /* ".." segment: no path escape, ever */
        for (size_t k = start; k < i; k++)
            if (spec[k] == '/')
                return 0; /* embedded slash: not a Rust path as written */
        if (o + seg + 1 >= cap)
            return 0;
        if (o > 0)
            out[o++] = '/';
        memcpy(out + o, spec + start, seg);
        o += seg;
        out[o] = '\0';
        segs++;
        if (spec[i] == '\0') {
            *n_seg = segs;
            return o > 0;
        }
        i += 2; /* skip the "::"; next iteration reads the next segment */
    }
}

/* The self anchor A(F): dirname(F) when the file IS a module root
 * (lib.rs / main.rs / mod.rs -- the directory IS the module), else
 * dirname(F)/stem(F) (the stem rule: src/util.rs declares `mod helper;`
 * -> src/util/helper.rs). Returns an arena copy of the anchor dir. */
static const char *rust_self_anchor(const oc_resolve_ctx *cx, oc_arena *a, const char *from_path) {
    const char *base = strrchr(from_path, '/');
    base = base ? base + 1 : from_path;
    int is_mod_root =
        strcmp(base, "lib.rs") == 0 || strcmp(base, "main.rs") == 0 || strcmp(base, "mod.rs") == 0;
    /* a .rs file directly inside a PACKAGE-LEVEL tests/ directory is a
     * crate root of its own (cargo compiles each as an integration-test
     * crate): its `mod support;` probes tests/support.rs, not
     * tests/<stem>/support.rs. Package-level = NO `src` segment sits
     * anywhere in the tests dir's own directory chain -- a tests
     * directory inside the src tree (e.g. tokio's src/runtime/tests/,
     * the runtime::tests tree declared by src/runtime/tests/mod.rs) is
     * an INTERNAL module directory, and its files keep the stem rule. */
    if (!is_mod_root) {
        const char *slash = strrchr(from_path, '/');
        if (slash) {
            const char *d = from_path;
            for (const char *q = from_path; q < slash; q++)
                if (*q == '/')
                    d = q + 1;
            if (slash - d == 5 && memcmp(d, "tests", 5) == 0) {
                /* scan every segment of dirname(F) (the tests dir's
                 * directory chain, segments before the "tests" name)
                 * for "src" */
                int inside_src = 0;
                const char *seg = from_path;
                const char *q = from_path;
                while (q < d) {
                    if (q[0] == '/') {
                        if ((size_t)(q - seg) == 3 && memcmp(seg, "src", 3) == 0)
                            inside_src = 1;
                        seg = q + 1;
                    }
                    q++;
                }
                if (!inside_src)
                    is_mod_root = 1;
            }
        }
    }
    (void)cx;
    char dir[1024];
    oc_path_dirname(from_path, dir, sizeof(dir));
    if (is_mod_root)
        return oc_arena_strdup(a, dir);
    /* stem: strip the trailing ".rs" */
    size_t bl = strlen(base);
    char stem[512];
    size_t sl = bl > 3 ? bl - 3 : 0;
    if (sl == 0 || sl >= sizeof(stem))
        return NULL;
    memcpy(stem, base, sl);
    stem[sl] = '\0';
    char anchor[1200];
    if (!oc_path_join(anchor, sizeof(anchor), dir, stem))
        return NULL;
    oc_path_normalize(anchor);
    return oc_arena_strdup(a, anchor);
}

/* The crate anchor R(F): the nearest ancestor directory named `src` or
 * `tests` (walking up from dirname(F)); when the hit is `src` and
 * dirname(F) is `<src>/bin`, the bin directory (src/bin/<name>.rs are their
 * own crates -- cargo semantics, the fixture pins that lib and bin are
 * different crates). No src/tests ancestor: dirname(F) (a loose tree is
 * not a cargo crate; the probes simply fail, nothing fabricates). */
static const char *rust_crate_anchor(oc_arena *a, const char *from_path) {
    char dir[1024];
    oc_path_dirname(from_path, dir, sizeof(dir));
    char up[1024];
    snprintf(up, sizeof(up), "%s", dir);
    for (int depth = 0; depth < 64; depth++) {
        const char *base = strrchr(up, '/');
        base = base ? base + 1 : up;
        if (strcmp(base, "src") == 0 || strcmp(base, "tests") == 0) {
            /* the src/bin own-crate exception: a file whose directory is
             * exactly <src>/bin anchors the crate at <src>/bin (cargo
             * compiles src/bin/<name>.rs as their own crates) */
            if (strcmp(base, "src") == 0) {
                size_t dl = strlen(dir);
                if (dl >= 4 && strcmp(dir + dl - 4, "/bin") == 0) {
                    char bin_parent[1024];
                    oc_path_up(dir, 1, bin_parent, sizeof(bin_parent));
                    if (strcmp(bin_parent, up) == 0)
                        return oc_arena_strdup(a, dir);
                }
            }
            return oc_arena_strdup(a, up);
        }
        char next[1024];
        oc_path_up(up, 1, next, sizeof(next));
        if (strcmp(next, up) == 0)
            break; /* filesystem top: no src/tests ancestor */
        snprintf(up, sizeof(up), "%s", next);
    }
    return oc_arena_strdup(a, dir);
}

/* Probe <anchor>/<rel>.rs then <anchor>/<rel>/mod.rs; first graph hit
 * wins (arena copy of the graph path). */
static const char *rust_probe_pair(const oc_resolve_ctx *cx, oc_arena *a, const char *anchor,
                                   const char *rel) {
    char cand[1250];
    if (snprintf(cand, sizeof(cand), "%s/%s.rs", anchor, rel) < (int)sizeof(cand)) {
        oc_path_normalize(cand);
        if (is_file_node_idx(cx, cand))
            return oc_arena_strdup(a, cand);
    }
    if (snprintf(cand, sizeof(cand), "%s/%s/mod.rs", anchor, rel) < (int)sizeof(cand)) {
        oc_path_normalize(cand);
        if (is_file_node_idx(cx, cand))
            return oc_arena_strdup(a, cand);
    }
    return NULL;
}

/* The item-reading pop ladder: probe the full chain, pop the last
 * segment, repeat down to ONE segment (K >= 1: the anchor file itself
 * is never probed). */
static const char *rust_probe_ladder(const oc_resolve_ctx *cx, oc_arena *a, const char *anchor,
                                     const char *slashed, size_t n_seg) {
    char rel[1024];
    snprintf(rel, sizeof(rel), "%s", slashed);
    while (n_seg >= 1) {
        const char *hit = rust_probe_pair(cx, a, anchor, rel);
        if (hit)
            return hit;
        if (n_seg == 1)
            return NULL; /* down to the last segment: no anchor self-probe */
        char *cut = strrchr(rel, '/');
        if (!cut)
            return NULL;
        *cut = '\0';
        n_seg--;
    }
    return NULL;
}

const char *oc_resolve_rust_use_path(const oc_resolve_ctx *cx, const char *from_path,
                                     const char *spec, oc_arena *a) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    /* head classification: the specifier keeps its own head keyword */
    const char *rest = NULL;
    char head[8];
    if (strncmp(spec, "crate::", 7) == 0) {
        snprintf(head, sizeof(head), "crate");
        rest = spec + 7;
    } else if (strncmp(spec, "self::", 6) == 0) {
        snprintf(head, sizeof(head), "self");
        rest = spec + 6;
    } else if (strncmp(spec, "super::", 7) == 0) {
        snprintf(head, sizeof(head), "super");
        rest = spec + 7;
    } else {
        return NULL; /* external crate name: always unresolved, never read
                      * from any manifest (the go.mod decoy discipline) */
    }
    char slashed[1024];
    size_t n_seg = 0;
    if (!rust_spec_segments(rest, slashed, sizeof(slashed), &n_seg))
        return NULL;
    const char *anchor = NULL;
    if (strcmp(head, "crate") == 0) {
        anchor = rust_crate_anchor(a, from_path);
    } else if (strcmp(head, "self") == 0) {
        anchor = rust_self_anchor(cx, a, from_path);
    } else {
        /* super::: the parent module's anchor = dirname(A(F)); when the
         * self anchor IS the crate anchor the crate has no parent */
        const char *self_a = rust_self_anchor(cx, a, from_path);
        if (!self_a)
            return NULL;
        const char *crate_a = rust_crate_anchor(a, from_path);
        if (crate_a && strcmp(self_a, crate_a) == 0)
            return NULL; /* the file IS the crate root: no parent module */
        char parent[1024];
        oc_path_dirname(self_a, parent, sizeof(parent));
        anchor = oc_arena_strdup(a, parent);
        if (!anchor || anchor[0] == '\0')
            anchor = oc_arena_strdup(a, ".");
    }
    if (!anchor)
        return NULL;
    return rust_probe_ladder(cx, a, anchor, slashed, n_seg);
}

const char *oc_resolve_rust_mod_decl(const oc_resolve_ctx *cx, const char *from_path,
                                     const char *spec, oc_arena *a) {
    if (!cx || !spec || strncmp(spec, "mod ", 4) != 0)
        return NULL;
    const char *name = spec + 4;
    if (name[0] == '\0')
        return NULL;
    for (const char *q = name; *q; q++)
        if (*q == '/' || *q == '.' || *q == ':')
            return NULL; /* not an identifier: never guessed */
    char rel[512];
    if (snprintf(rel, sizeof(rel), "%s", name) >= (int)sizeof(rel))
        return NULL;
    const char *anchor = rust_self_anchor(cx, a, from_path);
    if (!anchor)
        return NULL;
    return rust_probe_pair(cx, a, anchor, rel);
}

/* ------------------------------------------------------ Ruby (SPRINT-16) --- */

/* One load-root probe: join <root>/<payload>.rb, normalize, and hit the
 * graph's file index. The probe can only ever return files inside the
 * analyzed set, so nothing outside is ever fabricated. Returns an arena
 * copy of the graph path, or NULL. */
static const char *ruby_probe_rb(const oc_resolve_ctx *cx, oc_arena *a, const char *root,
                                 const char *payload) {
    char cand[1250];
    if (snprintf(cand, sizeof(cand), "%s/%s.rb", root, payload) >= (int)sizeof(cand))
        return NULL;
    oc_path_normalize(cand);
    if (!is_file_node_idx(cx, cand))
        return NULL;
    return oc_arena_strdup(a, cand);
}

/* Payload guards shared by both channels: a require payload must be a
 * relative, non-empty, slash-separated path as written. Backslashes are
 * not Ruby path separators; absolute payloads name machine-specific
 * files; both stay unresolved honestly. */
static int ruby_payload_ok(const char *payload) {
    if (!payload || payload[0] == '\0')
        return 0;
    if (payload[0] == '/')
        return 0; /* absolute: machine-specific, never probed */
    for (const char *q = payload; *q; q++)
        if (*q == '\\' || (unsigned char)*q < 0x20)
            return 0;
    return 1;
}

const char *oc_resolve_ruby_require(const oc_resolve_ctx *cx, const char *from_path,
                                    const char *spec, oc_arena *a) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    /* channel tag (the Rust "mod " precedent): the specifier carries its
     * own channel, `require_relative <payload>` or `require <payload>` */
    const char *payload = NULL;
    int relative = 0;
    if (strncmp(spec, "require_relative ", 17) == 0) {
        payload = spec + 17;
        relative = 1;
    } else if (strncmp(spec, "require ", 8) == 0) {
        payload = spec + 8;
    } else {
        return NULL;
    }
    if (!ruby_payload_ok(payload))
        return NULL;
    if (relative) {
        /* Ruby's own rule: relative to the REQUIRING file's directory,
         * `.rb` appended. `..` traversal is legal Ruby and not an escape
         * hazard: the probe hits only the analyzed file set, so a path
         * that leaves the analysis root resolves to nothing. The join is
         * anchored at the ANALYSIS ROOT (the from_path is scan-root
         * relative) so the lexical normalizer can pop `..` across the
         * file's own directory; a root-less scan keeps the plain
         * relative form. */
        char dir[1024];
        oc_path_dirname(from_path, dir, sizeof(dir));
        const char *root = analysis_root_for(cx, from_path);
        char search[1200];
        if (root && strcmp(root, ".") != 0 && strncmp(dir, root, strlen(root)) != 0) {
            if (snprintf(search, sizeof(search), "%s/%s", root, dir) >= (int)sizeof(search))
                return NULL;
        } else {
            snprintf(search, sizeof(search), "%s", dir);
        }
        return ruby_probe_rb(cx, a, search, payload);
    }
    /* Bare require: $LOAD_PATH is unknowable from a static scan. The two
     * documented load-root conventions under the analysis root, in order
     * (the v1-go dotted-path discipline; no manifest is ever read). */
    const char *root = analysis_root_for(cx, from_path);
    const char *hit = ruby_probe_rb(cx, a, root, payload);
    if (hit)
        return hit;
    char libroot[1100];
    if (snprintf(libroot, sizeof(libroot), "%s/lib", root) >= (int)sizeof(libroot))
        return NULL;
    return ruby_probe_rb(cx, a, libroot, payload);
}

/* ------------------------------------------------------ PHP (SPRINT-19) --- */

/* Loads the composer.json PSR-4/PSR-0 mapping of one analysis root into
 * the context (lazy, keyed by root string). Absent/unreadable/unparseable
 * config = an EMPTY table (fail-open, the tsconfig discipline): use
 * declarations then stay unresolved -- recorded, never guessed.
 * Array-valued mappings and empty strings record nothing (never
 * guessed; the bounded reader keeps composer's string-mapping form). */
static void php_composer_load(oc_resolve_ctx *cx, oc_arena *a, const char *root) {
    if (cx->composer.loaded && cx->composer.root && strcmp(cx->composer.root, root) == 0)
        return;
    cx->composer.loaded = 1;
    cx->composer.root = oc_arena_strdup(a, root);
    cx->composer.items = NULL;
    cx->composer.n = 0;
    char cfg[1100];
    if (snprintf(cfg, sizeof(cfg), "%s/composer.json", root) >= (int)sizeof(cfg))
        return;
    char *data = NULL;
    size_t len = 0;
    oc_status tmp;
    oc_status_clear(&tmp);
    if (oc_read_file(cfg, &data, &len, &tmp) != OC_OK) {
        free(data);
        return; /* no config here (or unreadable: treated as absent) */
    }
    oc_status_clear(&tmp);
    oc_json *rootj = oc_json_parse(a, data, len, &tmp);
    free(data);
    if (!rootj || rootj->type != OC_JSON_OBJ)
        return; /* broken config: fail-open, never guessed */
    const oc_json *autoload = oc_json_get(rootj, "autoload");
    if (!autoload || autoload->type != OC_JSON_OBJ)
        return;
    static const char *const SECTIONS[] = {"psr-4", "psr-0"};
    for (size_t s = 0; s < 2; s++) {
        const oc_json *sec = oc_json_get(autoload, SECTIONS[s]);
        if (!sec || sec->type != OC_JSON_OBJ)
            continue;
        for (size_t i = 0; i < sec->n_fields; i++) {
            const oc_json *v = sec->vals[i];
            if (!v || v->type != OC_JSON_STR || !v->str[0])
                continue; /* array mappings / empties: nothing recorded */
            const char *key = sec->keys[i];
            size_t kl = key ? strlen(key) : 0;
            while (kl > 0 && key[kl - 1] == '\\')
                kl--; /* the map key's trailing separators are composer
                       * convention, not part of the prefix */
            if (kl == 0 || kl >= 512)
                continue;
            char norm[512];
            memcpy(norm, key, kl);
            norm[kl] = '\0';
            struct {
                const char *prefix;
                const char *dir;
                int psr4;
            } *grown = (void *)oc_arena_alloc(a, (cx->composer.n + 1) * sizeof(*grown));
            if (!grown)
                return;
            if (cx->composer.n)
                memcpy(grown, cx->composer.items, cx->composer.n * sizeof(*grown));
            grown[cx->composer.n].prefix = oc_arena_strdup(a, norm);
            grown[cx->composer.n].dir = v->str;
            grown[cx->composer.n].psr4 = (s == 0);
            cx->composer.items = (void *)grown;
            cx->composer.n++;
        }
    }
}

/* Probes one joined candidate through the graph's file index (the probe
 * can only ever hit files inside the analyzed set, so nothing outside
 * is ever fabricated). Returns an arena copy, or NULL. Shared by the
 * PHP and C resolvers. */
static const char *probe_path_index(oc_resolve_ctx *cx, oc_arena *a, const char *joined) {
    char cand[1250];
    if (snprintf(cand, sizeof(cand), "%s", joined) >= (int)sizeof(cand))
        return NULL;
    oc_path_normalize(cand);
    if (!is_file_node_idx(cx, cand))
        return NULL;
    return oc_arena_strdup(a, cand);
}

/* One composer prefix resolution: the LONGEST matching prefix wins
 * (composer's own raw prefix rule, trailing separators normalized);
 * PSR-4 strips the prefix and appends the remainder, PSR-0 appends the
 * FULL name path -- both backslash->slash, ".php" appended, anchored at
 * the analysis root (the composer.json mapping targets are root
 * relative). Probes the PSR-4 candidate first when the winning prefix
 * is PSR-4, else PSR-0 first -- deterministic in both orders. */
static const char *php_prefix_resolve(oc_resolve_ctx *cx, oc_arena *a, const char *root,
                                      const char *name) {
    size_t best_len = 0;
    int best_psr4 = 0;
    const char *best_dir = NULL;
    for (size_t i = 0; i < cx->composer.n; i++) {
        const char *pfx = cx->composer.items[i].prefix;
        size_t pl = strlen(pfx);
        if (strncmp(name, pfx, pl) != 0)
            continue;
        int psr4 = cx->composer.items[i].psr4;
        if (pl > best_len || (pl == best_len && best_dir && psr4 && !best_psr4)) {
            best_len = pl;
            best_dir = cx->composer.items[i].dir;
            best_psr4 = psr4;
        }
    }
    if (!best_dir)
        return NULL; /* matches NO composer prefix: external, unresolved */
    const char *remainder = name + best_len;
    if (remainder[0] == '\\')
        remainder++;
    const char *rel = remainder[0] ? remainder : name; /* empty remainder: the
                                                        * prefix names the class */
    /* slash form of the name part to append under the mapping dir */
    char rel_sl[900];
    size_t w = 0;
    for (const char *q = best_psr4 ? rel : name; *q && w + 1 < sizeof(rel_sl); q++)
        rel_sl[w++] = (*q == '\\') ? '/' : *q;
    rel_sl[w] = '\0';
    if (w == 0)
        return NULL;
    char cand[1250];
    if (snprintf(cand, sizeof(cand), "%s/%s/%s.php", root, best_dir, rel_sl) >= (int)sizeof(cand))
        return NULL;
    return probe_path_index(cx, a, cand);
}

const char *oc_resolve_php_use(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                               oc_arena *a) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    /* a fully-qualified use (`\App\Store\Product`) names the same class
     * as its relative form: one leading separator is PHP's own absolute
     * mark and is dropped before the prefix match */
    if (spec[0] == '\\')
        spec++;
    if (spec[0] == '\0' || strchr(spec, ' '))
        return NULL; /* not a bare qualified name: never guessed */
    for (const char *q = spec; *q; q++)
        if ((unsigned char)*q < 0x20)
            return NULL;
    const char *root = analysis_root_for(cx, from_path);
    php_composer_load(cx, a, root);
    if (cx->composer.n == 0)
        return NULL; /* no config: unresolved, honestly */
    return php_prefix_resolve(cx, a, root, spec);
}

const char *oc_resolve_php_include(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                                   oc_arena *a) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    /* channel tag (the Rust `mod ` precedent): the specifier carries its
     * own keyword, `require p` / `require_once p` / `include p` /
     * `include_once p` */
    const char *payload = NULL;
    static const char *const TAGS[] = {"require ", "require_once ", "include ", "include_once "};
    for (size_t i = 0; i < 4; i++) {
        size_t tl = strlen(TAGS[i]);
        if (strncmp(spec, TAGS[i], tl) == 0) {
            payload = spec + tl;
            break;
        }
    }
    if (!payload)
        return NULL;
    /* payload guards shared with the Ruby channel: relative, non-empty,
     * slash-separated path as written (backslashes are not PHP relative
     * separators; absolute payloads name machine-specific files; both
     * stay unresolved honestly) */
    if (payload[0] == '\0' || payload[0] == '/')
        return NULL;
    for (const char *q = payload; *q; q++)
        if (*q == '\\' || (unsigned char)*q < 0x20)
            return NULL;
    /* relative to the REQUIRING file's directory (the contract; the join
     * is anchored at the ANALYSIS ROOT so the lexical normalizer can pop
     * `..` across the file's own directory -- the Ruby require_relative
     * discipline; a root-less scan keeps the plain relative form) */
    char dir[1024];
    oc_path_dirname(from_path, dir, sizeof(dir));
    const char *root = analysis_root_for(cx, from_path);
    char search[1200];
    if (root && strcmp(root, ".") != 0 && strncmp(dir, root, strlen(root)) != 0) {
        if (snprintf(search, sizeof(search), "%s/%s", root, dir) >= (int)sizeof(search))
            return NULL;
    } else {
        snprintf(search, sizeof(search), "%s", dir);
    }
    char cand[1250];
    if (snprintf(cand, sizeof(cand), "%s/%s", search, payload) >= (int)sizeof(cand))
        return NULL;
    return probe_path_index(cx, a, cand);
}

/* C (resolver v1-c, SPRINT-20, docs/LANGUAGES.md C section 5): the one
 * file an include directive names. The specifier carries its own channel
 * tag (the Rust `mod ` precedent): `include <p>` is the quoted form --
 * resolved against the INCLUDING file's directory (the C preprocessor's
 * own rule: the directory of the file containing the directive), the
 * join anchored at the analysis root so the lexical normalizer can pop
 * `..` across directories (the PHP include-channel discipline); `system
 * <p>` is the angle-bracket form and NEVER resolves -- system headers
 * live outside the analyzed set by definition, recorded-and-unresolved
 * (the Go module-boundary precedent; the dependency signal stays
 * visible without fabricating a manifest tier). Absolute payloads and
 * backslash separators stay unresolved (the parser already refuses to
 * record them; the guard is repeated here, defense in depth). The
 * probes hit only the analyzed file set. */
const char *oc_resolve_c_include(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                                 oc_arena *a) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    const char *payload = NULL;
    if (strncmp(spec, "include ", 8) == 0)
        payload = spec + 8;
    else if (strncmp(spec, "system ", 7) == 0)
        return NULL; /* system headers stay external, never guessed */
    else
        return NULL; /* not an include record: never guessed */
    if (payload[0] == '\0' || payload[0] == '/')
        return NULL;
    for (const char *q = payload; *q; q++)
        if (*q == '\\' || (unsigned char)*q < 0x20)
            return NULL;
    /* relative to the INCLUDING file's directory (the contract; the
     * join is anchored at the ANALYSIS ROOT so the lexical normalizer
     * can pop `..` across the file's own directory -- the
     * PHP require/include discipline; a root-less scan keeps the
     * plain relative form) */
    char dir[1024];
    oc_path_dirname(from_path, dir, sizeof(dir));
    const char *root = analysis_root_for(cx, from_path);
    char search[1200];
    if (root && strcmp(root, ".") != 0 && strncmp(dir, root, strlen(root)) != 0) {
        if (snprintf(search, sizeof(search), "%s/%s", root, dir) >= (int)sizeof(search))
            return NULL;
    } else {
        snprintf(search, sizeof(search), "%s", dir);
    }
    char cand[1250];
    if (snprintf(cand, sizeof(cand), "%s/%s", search, payload) >= (int)sizeof(cand))
        return NULL;
    return probe_path_index(cx, a, cand);
}

static oc_node_id index_find(const oc_resolve_ctx *cx, const char *path) {
    size_t lo = 0, hi = cx->index.n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(cx->index.path[mid], path);
        if (c == 0)
            return cx->index.id[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return OC_NODE_INVALID;
}

/* ------------------------------------------------ Kotlin (SPRINT-22) ---
 * Resolver v1-kotlin: the declared-fact resolver. Kotlin's package
 * model is DECOUPLED from the file system (no package==directory rule
 * exists, none is invented), so resolution keys on the DECLARED parse
 * facts recorded as hints: the per-file package_header clause (the Go
 * package_clause channel) and the per-file top-level declaration names
 * (the C# namespace-hint channel, fed by the exports loop). A specifier
 * resolves to the analyzed file(s) whose DECLARED package and
 * DECLARED top-level names prove the claim:
 *   `a.b.*`   -> every file whose declared package is `a.b`
 *                (the Java wildcard fan-out discipline);
 *   `a.b.C`   -> every file whose declared package is `a.b` AND that
 *                declares a top-level `C` (several files may carry the
 *                same name in one package -- the union fans out, set
 *                semantics, lexicographic order);
 *   `a.b.C.f` -> BOTH readings against the declared facts: package
 *                `a.b` declaring `C` (the object-member reading) and
 *                package `a.b.C` declaring `f` (the top-level reading)
 *                -- every recorded package that is a dotted prefix of
 *                the specifier claims its next segment.
 * Zero matches and NULL hints are both honest non-resolutions. Nothing
 * is ever probed against directories, build files or manifests. */

void oc_resolve_ctx_set_hints(oc_resolve_ctx *cx, const oc_hint_vec *hints) {
    if (!cx)
        return;
    cx->hints = hints;
    cx->kt.built = 0;
    cx->swift.built = 0;
}

static int kt_ent_cmp(const void *pa, const void *pb) {
    const kt_ent *x = pa, *y = pb;
    int c = strcmp(x->pkg, y->pkg);
    if (c != 0)
        return c;
    return strcmp(x->path, y->path);
}

/* Builds the declared-fact index lazily: one entry per hint item that
 * carries a Kotlin fact (a package clause or at least one top-level
 * symbol), sorted by (package, path). Hint items without either fact
 * never match and are dropped. */
static void kt_index_build(oc_resolve_ctx *cx, oc_arena *a) {
    if (cx->kt.built)
        return;
    cx->kt.built = 1;
    cx->kt.items = NULL;
    cx->kt.n = 0;
    if (!cx->hints)
        return;
    size_t cap = cx->hints->n ? cx->hints->n : 1;
    kt_ent *tmp = (kt_ent *)oc_arena_alloc(a, cap * sizeof(kt_ent));
    if (!tmp)
        return; /* OOM: the index stays empty -- records stay unresolved */
    size_t w = 0;
    for (size_t i = 0; i < cx->hints->n; i++) {
        const oc_file_hint *h = &cx->hints->items[i];
        if (!h->package_name && h->n_symbols == 0)
            continue; /* no declared fact: cannot match, drop */
        oc_node_id id = index_find(cx, h->path);
        if (id == OC_NODE_INVALID)
            continue; /* not an analyzed file node */
        tmp[w].pkg = h->package_name ? h->package_name : "";
        tmp[w].path = h->path;
        tmp[w].id = id;
        tmp[w].syms = h->symbols;
        tmp[w].n_syms = h->n_symbols;
        w++;
    }
    qsort(tmp, w, sizeof(kt_ent), kt_ent_cmp);
    cx->kt.items = tmp;
    cx->kt.n = w;
}

/* The [lo, hi) range of one package in the sorted index (0/0 when
 * absent). The index is small; the binary searches keep it O(log n). */
static void kt_pkg_range(const oc_resolve_ctx *cx, const char *pkg, size_t *lo, size_t *hi) {
    size_t l = 0, r = cx->kt.n;
    while (l < r) {
        size_t mid = (l + r) / 2;
        if (strcmp(cx->kt.items[mid].pkg, pkg) < 0)
            l = mid + 1;
        else
            r = mid;
    }
    size_t start = l;
    r = cx->kt.n;
    while (l < r) {
        size_t mid = (l + r) / 2;
        if (strcmp(cx->kt.items[mid].pkg, pkg) <= 0)
            l = mid + 1;
        else
            r = mid;
    }
    *lo = start;
    *hi = l;
}

/* True when `pkg` (a dotted package clause) is a dotted prefix of the
 * dotted specifier with at least one segment remaining; *sym_start /
 * *sym_len get the first remaining segment. The empty package (the
 * default package) claims the specifier's FIRST segment. */
static int kt_prefix_sym(const char *pkg, const char *spec, const char **sym_start,
                         size_t *sym_len) {
    size_t plen = strlen(pkg);
    if (plen > 0) {
        if (strncmp(spec, pkg, plen) != 0)
            return 0;
        if (spec[plen] != '.')
            return 0;
        spec += plen + 1;
    }
    if (*spec == '\0')
        return 0;
    const char *dot = strchr(spec, '.');
    *sym_len = dot ? (size_t)(dot - spec) : strlen(spec);
    *sym_start = spec;
    return 1;
}

long oc_resolve_kotlin_import(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                              oc_arena *a, const char ***out_targets, size_t *out_n) {
    *out_targets = NULL;
    *out_n = 0;
    if (!cx || !spec || spec[0] == '\0')
        return 0;
    kt_index_build(cx, a);
    if (cx->kt.n == 0)
        return 0;
    if (from_path && index_find(cx, from_path) == OC_NODE_INVALID)
        return 0; /* the importing file is not in this analysis */

    const char **tgs = NULL;
    size_t n_tg = 0, cap_tg = 0;
#define KT_TG_PUSH(p)                                                                            \
    do {                                                                                          \
        if (n_tg == cap_tg) {                                                                     \
            size_t ncap = cap_tg ? cap_tg * 2 : 16;                                               \
            const char **ng = (const char **)oc_arena_alloc(a, ncap * sizeof(const char *));      \
            if (!ng)                                                                              \
                return -1;                                                                        \
            for (size_t ti = 0; ti < n_tg; ti++)                                                  \
                ng[ti] = tgs[ti];                                                                 \
            tgs = ng;                                                                             \
            cap_tg = ncap;                                                                        \
        }                                                                                         \
        tgs[n_tg++] = (p);                                                                        \
    } while (0)

    size_t sl = strlen(spec);
    if (sl >= 2 && strcmp(spec + sl - 2, ".*") == 0) {
        /* the wildcard package claim: exact declared-package match */
        char *pkg = (char *)oc_arena_alloc(a, sl - 1);
        if (!pkg)
            return -1;
        memcpy(pkg, spec, sl - 2);
        pkg[sl - 2] = '\0';
        size_t lo, hi;
        kt_pkg_range(cx, pkg, &lo, &hi);
        for (size_t i = lo; i < hi; i++)
            KT_TG_PUSH(cx->kt.items[i].path);
    } else {
        /* the declared-prefix walk: every recorded package that is a
         * dotted prefix of the specifier claims its next segment; the
         * index is sorted by package, so each package range is visited
         * once */
        size_t i = 0;
        while (i < cx->kt.n) {
            const char *pkg = cx->kt.items[i].pkg;
            size_t lo, hi;
            kt_pkg_range(cx, pkg, &lo, &hi);
            const char *sym_start = NULL;
            size_t sym_len = 0;
            if (kt_prefix_sym(pkg, spec, &sym_start, &sym_len)) {
                for (size_t j = lo; j < hi; j++) {
                    const kt_ent *ent = &cx->kt.items[j];
                    for (size_t s = 0; s < ent->n_syms; s++) {
                        if (strlen(ent->syms[s]) == sym_len &&
                            memcmp(ent->syms[s], sym_start, sym_len) == 0) {
                            KT_TG_PUSH(ent->path);
                            break; /* one push per file (the set semantics) */
                        }
                    }
                }
            }
            i = hi > i ? hi : i + 1;
        }
    }
#undef KT_TG_PUSH
    if (n_tg == 0)
        return 0;
    /* deterministic order: sort by path (insertion sort -- tiny and
     * mostly sorted), then dedupe (several package prefixes may claim
     * the same file; the imports relation is a set) */
    for (size_t i = 1; i < n_tg; i++) {
        const char *key = tgs[i];
        size_t j = i;
        while (j > 0 && strcmp(tgs[j - 1], key) > 0) {
            tgs[j] = tgs[j - 1];
            j--;
        }
        tgs[j] = key;
    }
    size_t w = 1;
    for (size_t i = 1; i < n_tg; i++) {
        if (strcmp(tgs[w - 1], tgs[i]) != 0)
            tgs[w++] = tgs[i];
    }
    *out_targets = tgs;
    *out_n = w;
    return (long)w;
}

/* ------------------------------------------------------------ swift ---
 * Resolver v1-swift: the declared-fact resolver, the MODULE spelling.
 * Swift's import specifier names a compilation TARGET; no source file
 * carries its own module name (the Kotlin package_header channel has
 * no Swift analog and none is invented), so the declared facts are the
 * MANIFEST-declared targets: every analyzed Package.swift contributes
 * (target name, declared `path:`) pairs through the target-hint
 * channel. The module of each analyzed Swift file is the declared
 * target whose path covers the file -- an explicit `path:` argument as
 * written, else the SPM DOCUMENTED default `Sources/<name>` (the same
 * class of build-system rule the javac package==directory mapping is;
 * nothing is probed beyond what the manifest declares). Files outside
 * every declared target stay module-less and are never import targets;
 * the manifest file itself carries no module. An import specifier `M`
 * (the first segment -- the import shape already normalized it)
 * resolves to every analyzed file whose declared module is `M`: the
 * wholesale fan-out, set semantics, lexicographic order. */

static int swift_ent_cmp(const void *pa, const void *pb) {
    const kt_ent *x = pa, *y = pb;
    int c = strcmp(x->pkg, y->pkg);
    if (c != 0)
        return c;
    return strcmp(x->path, y->path);
}

/* True when `path` is `dir` itself or lives anywhere under it (the
 * prefix is a whole path-component boundary, never a raw strncmp). */
static int swift_path_under(const char *path, const char *dir) {
    size_t dl = strlen(dir);
    if (dl == 0)
        return 1;
    if (strncmp(path, dir, dl) != 0)
        return 0;
    return path[dl] == '/' || path[dl] == '\0';
}

/* Builds the declared-fact index lazily: pass 1 collects the
 * (manifest, target, declared path) triples from the hints; pass 2
 * assigns each analyzed Swift hint its declared module and keeps the
 * ones that carry at least one top-level declaration name. Sorted by
 * (module, path). */
static void swift_pkg_range(const kt_ent *items, size_t n, const char *pkg, size_t *lo,
                            size_t *hi);
static void swift_index_build(oc_resolve_ctx *cx, oc_arena *a) {
    if (cx->swift.built)
        return;
    cx->swift.built = 1;
    cx->swift.items = NULL;
    cx->swift.n = 0;
    if (!cx->hints)
        return;
    /* pass 1: the manifests (a manifest hint with targets but no path
     * entry means the extraction recorded the name only) */
    struct {
        const char *dir;  /* the manifest's directory */
        const char *name; /* the target name = the module */
    } tg[256];
    size_t n_tg = 0;
    for (size_t i = 0; i < cx->hints->n && n_tg < 256; i++) {
        const oc_file_hint *h = &cx->hints->items[i];
        if (h->n_swift_targets == 0 || !h->swift_targets)
            continue;
        /* the manifest's directory: everything before the last '/' */
        const char *slash = strrchr(h->path, '/');
        size_t dl = slash ? (size_t)(slash - h->path) : 0;
        for (size_t t = 0; t < h->n_swift_targets && n_tg < 256; t++) {
            const char *dp = h->swift_target_paths ? h->swift_target_paths[t] : NULL;
            char effbuf[1250];
            if (!dp || dp[0] == '\0') {
                /* the SPM documented default: Sources/<name> */
                if (dl > 0)
                    snprintf(effbuf, sizeof(effbuf), "%.*s/Sources/%s", (int)dl, h->path,
                             h->swift_targets[t]);
                else
                    snprintf(effbuf, sizeof(effbuf), "Sources/%s", h->swift_targets[t]);
            } else if (dl > 0) {
                snprintf(effbuf, sizeof(effbuf), "%.*s/%s", (int)dl, h->path, dp);
            } else {
                snprintf(effbuf, sizeof(effbuf), "%s", dp);
            }
            char *eff = oc_arena_strdup(a, effbuf);
            if (!eff)
                return; /* OOM: the index stays empty -- unresolved */
            tg[n_tg].dir = eff;
            tg[n_tg].name = h->swift_targets[t];
            n_tg++;
        }
    }
    if (n_tg == 0)
        return;
    /* pass 2: assign modules, keep files with at least one declared
     * top-level name (an empty file can never prove a claim) */
    kt_ent *tmp = (kt_ent *)oc_arena_alloc(a, cx->hints->n * sizeof(kt_ent));
    if (!tmp)
        return;
    size_t w = 0;
    for (size_t i = 0; i < cx->hints->n; i++) {
        const oc_file_hint *h = &cx->hints->items[i];
        if (h->n_swift_targets > 0)
            continue; /* the manifest itself: never an import target */
        if (h->n_symbols == 0)
            continue; /* nothing declared: cannot match a named claim */
        const char *mod = NULL;
        for (size_t t = 0; t < n_tg && !mod; t++)
            if (swift_path_under(h->path, tg[t].dir))
                mod = tg[t].name;
        if (!mod)
            continue; /* module-less: never an import target */
        oc_node_id id = index_find(cx, h->path);
        if (id == OC_NODE_INVALID)
            continue; /* not an analyzed file node */
        tmp[w].pkg = mod;
        tmp[w].path = h->path;
        tmp[w].id = id;
        tmp[w].syms = h->symbols;
        tmp[w].n_syms = h->n_symbols;
        w++;
    }
    qsort(tmp, w, sizeof(kt_ent), swift_ent_cmp);
    cx->swift.items = tmp;
    cx->swift.n = w;
}

/* The [lo, hi) range of one module in the sorted swift index (0/0 when
 * absent) -- the same binary search the Kotlin index uses, over the
 * Swift index. */
static void swift_pkg_range(const kt_ent *items, size_t n, const char *pkg, size_t *lo,
                            size_t *hi) {
    size_t l = 0, r = n;
    while (l < r) {
        size_t mid = (l + r) / 2;
        if (strcmp(items[mid].pkg, pkg) < 0)
            l = mid + 1;
        else
            r = mid;
    }
    size_t start = l;
    r = n;
    while (l < r) {
        size_t mid = (l + r) / 2;
        if (strcmp(items[mid].pkg, pkg) <= 0)
            l = mid + 1;
        else
            r = mid;
    }
    *lo = start;
    *hi = l;
}

long oc_resolve_swift_import(oc_resolve_ctx *cx, const char *from_path, const char *spec,
                             oc_arena *a, const char ***out_targets, size_t *out_n) {
    *out_targets = NULL;
    *out_n = 0;
    if (!cx || !spec || spec[0] == '\0')
        return 0;
    swift_index_build(cx, a);
    if (cx->swift.n == 0)
        return 0;
    if (from_path && index_find(cx, from_path) == OC_NODE_INVALID)
        return 0; /* the importing file is not in this analysis */
    /* the whole-module claim: exact declared-module match, wholesale
     * fan-out (the module's files, set semantics, lexicographic order
     * -- the index is sorted, so one range is the answer) */
    const char **tgs = NULL;
    size_t n_tg = 0, cap_tg = 0;
    size_t lo, hi;
    swift_pkg_range(cx->swift.items, cx->swift.n, spec, &lo, &hi);
    for (size_t i = lo; i < hi; i++) {
        if (n_tg == cap_tg) {
            size_t ncap = cap_tg ? cap_tg * 2 : 16;
            const char **ng = (const char **)oc_arena_alloc(a, ncap * sizeof(const char *));
            if (!ng)
                return -1;
            for (size_t ti = 0; ti < n_tg; ti++)
                ng[ti] = tgs[ti];
            tgs = ng;
            cap_tg = ncap;
        }
        tgs[n_tg++] = cx->swift.items[i].path;
    }
    if (n_tg == 0)
        return 0;
    *out_targets = tgs;
    *out_n = n_tg;
    return (long)n_tg;
}

const char *oc_resolve_specifier(oc_resolve_ctx *cx, oc_arena *a, const char *from_path,
                                 const char *spec, oc_lang_family family) {
    if (!cx || !spec || spec[0] == '\0')
        return NULL;
    if (family == OC_LANG_JS)
        return resolve_js(cx, a, from_path, spec);
    if (family == OC_LANG_PY)
        return resolve_py(cx, a, from_path, spec);
    if (family == OC_LANG_GO) {
        /* Resolver v1-go (SPRINT-10, docs/LANGUAGES.md §Go.5): a dotted
         * path resolves to the package directory it names under an
         * analysis root -- whole-package semantics. This single-target
         * view returns the package's lexicographically-first discovered
         * file; the edge pass and the surface build enumerate the whole
         * package (fan-out) instead of using this view. Guards keep
         * relative/absolute/escape forms and everything outside the
         * analyzed set (stdlib, remote modules) unresolved -- never
         * guessed, and go.mod is never read. */
        return resolve_go_first(cx, a, spec);
    }
    if (family == OC_LANG_JAVA) {
        /* Resolver v1-java (SPRINT-11, docs/LANGUAGES.md §Java.5): the
         * single-target class-file view for single-type and static
         * qualifiers. Wildcard package claims (".*" suffix) are handled
         * by the edge pass and the surface build directly through
         * oc_resolve_java_pkg_dir -- a package claim is never collapsed
         * into one file. */
        size_t sl = strlen(spec);
        if (sl >= 2 && strcmp(spec + sl - 2, ".*") == 0)
            return NULL;
        return resolve_java_first(cx, a, from_path, spec);
    }
    if (family == OC_LANG_CSHARP) {
        /* Resolver v1-csharp (SPRINT-13, docs/LANGUAGES.md C# section):
         * the specifier carries the directive's own tokens. `static
         * A.B.C` is a TYPE claim (exact file probe); `X = A.B.C` is an
         * ALIAS claim (documented ordered probe: type file first, then
         * namespace directory); anything else is a NAMESPACE claim
         * (single-target view: first discovered .cs of the namespace
         * dir). Guards keep escape forms, non-identifier paths and
         * everything outside the analyzed set (SDK/framework usings)
         * unresolved -- never guessed; no project/MSBuild reading. */
        const char *target = spec;
        int is_alias = strstr(spec, " = ") != NULL;
        if (is_alias)
            target = strstr(spec, " = ") + 3;
        if (strncmp(target, "static ", 7) == 0) {
            if (is_alias)
                return NULL; /* `X = static T` is not legal C#: never guess */
            return oc_resolve_csharp_type_file(cx, target + 7, a);
        }
        if (is_alias)
            return resolve_csharp_alias(cx, a, target);
        return resolve_csharp_first(cx, a, spec);
    }
    if (family == OC_LANG_RUST) {
        /* Resolver v1-rust (SPRINT-15, docs/LANGUAGES.md Rust section 5):
         * the single-target view. The mod-tree channel (`mod name`
         * records) resolves to the ONE module file the declaration
         * names; use-tree leaves resolve through the anchor + pop-ladder
         * (see the RUST resolving block above). External crate heads
         * stay unresolved -- no manifest is ever read. */
        if (strncmp(spec, "mod ", 4) == 0)
            return oc_resolve_rust_mod_decl(cx, from_path, spec, a);
        return oc_resolve_rust_use_path(cx, from_path, spec, a);
    }
    if (family == OC_LANG_RUBY) {
        /* Resolver v1-ruby (SPRINT-16, docs/LANGUAGES.md Ruby section 5):
         * the single-target view. The specifier carries its own channel
         * tag (`require_relative p` / `require p`); the bare-require
         * channel probes the two documented load-root conventions under
         * the analysis root. $LOAD_PATH is never read from any manifest. */
        return oc_resolve_ruby_require(cx, from_path, spec, a);
    }
    if (family == OC_LANG_PHP) {
        /* Resolver v1-php (SPRINT-19, docs/LANGUAGES.md PHP section 5):
         * the single-target view. The specifier carries its own channel:
         * a bare qualified name (no space) is a `use` declaration and
         * resolves through the composer.json PSR-4/PSR-0 mapping read
         * from the ANALYSIS ROOT (longest-prefix rule,
         * backslash->slash, anchored at the root); a use whose leading
         * prefix matches NO composer prefix stays unresolved (external,
         * the Go module-boundary precedent -- nothing is guessed from
         * include_path or runtime autoloading). The require/include
         * channels resolve their one static payload against the
         * requiring file's directory. */
        if (strchr(spec, ' '))
            return oc_resolve_php_include(cx, from_path, spec, a);
        return oc_resolve_php_use(cx, from_path, spec, a);
    }
    if (family == OC_LANG_C || family == OC_LANG_CPP) {
        /* Resolver v1-c (SPRINT-20, docs/LANGUAGES.md C section 5) and
         * v1-cpp (SPRINT-21, docs/LANGUAGES.md C++ section 5): the
         * single-target view, shared implementation. The specifier
         * carries its own channel (`include p` / `system p`); the quoted
         * channel resolves against the including file's directory, the
         * system channel never resolves. No manifest is ever read.
         * C++ uses the preprocessor's include model unchanged. */
        return oc_resolve_c_include(cx, from_path, spec, a);
    }
    return NULL;
}

/* Edge honesty (SPRINT-13 C#): one file can name the same target
 * through SEVERAL using records (plain + static + alias forms). The
 * graph's imports relation is a SET for C# -- a duplicate (from,to)
 * pair is never added twice, so the edge count reflects distinct file
 * relationships (pinned by the acceptance gates). Scoped to the C#
 * branch: Java keeps the committed per-record edge semantics (each
 * import record resolves honestly; the store corpus pins
 * single-type + static + wildcard as separate edges), and the
 * JS/TS/Python/Go record shapes cannot produce duplicate targets
 * within one file. */
static int family_edge_exists(const oc_graph *g, oc_node_id from, oc_node_id to) {
    for (size_t i = 0; i < g->n_edges; i++) {
        if (g->edges[i].kind == OC_EDGE_IMPORTS && g->edges[i].from == from && g->edges[i].to == to)
            return 1;
    }
    return 0;
}

long oc_imports_resolve_ex_hints(oc_graph *g, const oc_import_vec *in, oc_arena *a,
                                 const char *const *roots, size_t n_roots,
                                 const oc_hint_vec *hints) {
    long added = 0;
    oc_resolve_ctx *cx = oc_resolve_ctx_new(a, g, roots, n_roots);
    if (!cx)
        return -1;
    oc_resolve_ctx_set_hints(cx, hints);
    for (size_t i = 0; i < in->n; i++) {
        const oc_import_rec *r = &in->items[i];
        oc_node_id from = index_find(cx, r->from_path);
        if (from == OC_NODE_INVALID)
            continue;
        if (r->family == OC_LANG_KOTLIN || r->family == OC_LANG_SCALA) {
            /* Resolver v1-kotlin (SPRINT-22) and v1-scala (SPRINT-24,
             * docs/LANGUAGES.md Scala section 5): the declared-fact
             * resolver, SHARED implementation -- Scala's package model is
             * decoupled from the file system exactly like Kotlin's (no
             * package==directory rule exists, none is invented), and the
             * specifier shapes carry the same contract (`a.b.*` wildcard
             * fan-out, `a.b.C` declared-symbol match, `a.b.C.f` both
             * readings). Every specifier resolves against the recorded
             * package clauses (the Go package_clause hint channel) and
             * top-level declaration names (the C# namespace-hint
             * channel, fed by the exports loop) of the analyzed set; the
             * target set fans out over every file the declared facts
             * prove (set semantics, lexicographic order); self-edges are
             * skipped defensively. Without hints (the plain _ex path)
             * every specifier stays unresolved -- the honest
             * degradation. */
            const char **targets = NULL;
            size_t n_targets = 0;
            long rc = oc_resolve_kotlin_import(cx, r->from_path, r->specifier, a, &targets,
                                               &n_targets);
            if (rc < 0)
                return -1;
            for (size_t t = 0; t < n_targets; t++) {
                oc_node_id to = index_find(cx, targets[t]);
                if (to == OC_NODE_INVALID || from == to)
                    continue; /* self-import is not a useful edge */
                if (family_edge_exists(g, from, to))
                    continue; /* imports relation is a set for Kotlin */
                if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                    added++;
                else
                    return -1;
            }
            continue;
        }
        if (r->family == OC_LANG_SWIFT) {
            /* Resolver v1-swift (SPRINT-23, docs/LANGUAGES.md Swift
             * section 5): the declared-fact resolver, the module
             * spelling -- every specifier resolves against the
             * manifest-declared target facts only (never directories
             * beyond the DECLARED target paths, no build-system
             * evaluation). The target set fans out over every file the
             * declared facts prove (set semantics, lexicographic
             * order); self-edges are skipped defensively. Without
             * hints (the plain _ex path) every Swift specifier stays
             * unresolved -- the honest degradation. */
            const char **targets = NULL;
            size_t n_targets = 0;
            long rc = oc_resolve_swift_import(cx, r->from_path, r->specifier, a, &targets,
                                              &n_targets);
            if (rc < 0)
                return -1;
            for (size_t t = 0; t < n_targets; t++) {
                oc_node_id to = index_find(cx, targets[t]);
                if (to == OC_NODE_INVALID || from == to)
                    continue; /* self-import is not a useful edge */
                if (family_edge_exists(g, from, to))
                    continue; /* imports relation is a set for Swift */
                if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                    added++;
                else
                    return -1;
            }
            continue;
        }
        if (r->family == OC_LANG_JAVA) {
            /* Resolver v1-java (SPRINT-11, docs/LANGUAGES.md §Java.5):
             * single-type and static qualifiers resolve to the ONE class
             * file they name (a/b/C.java under an analysis root); a
             * wildcard `a.b.*` is a package claim that fans out to every
             * .java file directly inside the package directory -- a Java
             * wildcard import covers the whole package, and the fan-out
             * edges honestly record that (no test-file exclusion: the
             * class-file model compiles nothing selectively). Package
             * internal self-edges are skipped defensively. */
            size_t sl = strlen(r->specifier);
            if (sl >= 2 && strcmp(r->specifier + sl - 2, ".*") == 0) {
                const char *dir = oc_resolve_java_pkg_dir(cx, r->specifier, a);
                if (!dir)
                    continue;
                java_pkg_iter it;
                if (!java_pkg_iter_init(&it, cx, dir))
                    continue;
                oc_node_id to;
                while ((to = java_pkg_iter_next(&it, NULL)) != OC_NODE_INVALID) {
                    if (to == from)
                        continue;
                    if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                        added++;
                    else
                        return -1;
                }
                continue;
            }
            const char *target = resolve_java_first(cx, a, r->from_path, r->specifier);
            if (!target)
                continue;
            oc_node_id to = index_find(cx, target);
            if (to == OC_NODE_INVALID || from == to)
                continue; /* self-import is not a useful edge */
            if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                added++;
            else
                return -1;
            continue;
        }
        if (r->family == OC_LANG_CSHARP) {
            /* Resolver v1-csharp (SPRINT-13): the specifier carries the
             * directive's own tokens. A TYPE claim (`static A.B.C`) is
             * one edge to the exact file; a NAMESPACE claim (`A.B.C`)
             * fans out to every .cs file directly inside the namespace
             * directory (the whole-package discipline of v1-go/v1-java;
             * docs/LANGUAGES.md C# section); an ALIAS claim
             * (`X = A.B.C`) probes in the documented order (type file
             * first, namespace directory second) and produces one edge
             * or a fan-out accordingly. Non-.cs files and
             * subdirectories are skipped; the imports relation is a
             * set for C# (duplicates collapse). */
            const char *spec_target = r->specifier;
            int is_alias = strstr(r->specifier, " = ") != NULL;
            if (is_alias)
                spec_target = strstr(r->specifier, " = ") + 3;
            if (strncmp(spec_target, "static ", 7) == 0) {
                if (is_alias)
                    continue; /* not legal C#: never guess */
                const char *target = oc_resolve_csharp_type_file(cx, spec_target + 7, a);
                if (!target)
                    continue;
                oc_node_id to = index_find(cx, target);
                if (to == OC_NODE_INVALID || from == to)
                    continue;
                if (family_edge_exists(g, from, to))
                    continue; /* imports relation is a set for C# */
                if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                    added++;
                else
                    return -1;
                continue;
            }
            /* namespace claim, or the namespace reading of an alias:
             * whole-namespace fan-out through the direct-children
             * iterator */
            const char *dir = is_alias ? (oc_resolve_csharp_type_file(cx, spec_target, a)
                                              ? NULL /* the alias resolved as a TYPE file:
                                                        handled below, never fan out */
                                              : oc_resolve_csharp_ns_dir(cx, spec_target, a))
                                       : oc_resolve_csharp_ns_dir(cx, spec_target, a);
            if (dir) {
                csharp_pkg_iter it;
                if (!csharp_pkg_iter_init(&it, cx, dir))
                    continue;
                oc_node_id to;
                while ((to = csharp_pkg_iter_next(&it, NULL)) != OC_NODE_INVALID) {
                    if (to == from)
                        continue;
                    if (family_edge_exists(g, from, to))
                        continue; /* imports relation is a set for C# */
                    if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                        added++;
                    else
                        return -1;
                }
                continue;
            }
            /* the alias resolved as a TYPE file (the ordered probe's
             * first hit): one edge to that exact file */
            if (!is_alias)
                continue; /* a plain namespace claim with no directory: unresolved */
            const char *target = oc_resolve_csharp_type_file(cx, spec_target, a);
            if (!target)
                continue;
            oc_node_id to = index_find(cx, target);
            if (to == OC_NODE_INVALID || from == to)
                continue;
            if (family_edge_exists(g, from, to))
                continue;
            if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                added++;
            else
                return -1;
            continue;
        }
        if (r->family == OC_LANG_GO) {
            /* Resolver v1-go (SPRINT-10): one resolved record fans out
             * to every non-test .go file directly inside the package
             * directory -- Go compiles whole packages, so each package
             * file is honestly "part of this analysis" via the import
             * (docs/LANGUAGES.md §Go.5). Test files never join an
             * import, and package-internal self-edges cannot occur in
             * Go; both are skipped defensively. */
            const char *dir = oc_resolve_go_pkg_dir(cx, r->specifier, a);
            if (!dir)
                continue;
            go_pkg_iter it;
            if (!go_pkg_iter_init(&it, cx, dir))
                continue;
            oc_node_id to;
            while ((to = go_pkg_iter_next(&it, NULL)) != OC_NODE_INVALID) {
                if (to == from)
                    continue;
                if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                    added++;
                else
                    return -1;
            }
            continue;
        }
        if (r->family == OC_LANG_RUST) {
            /* Resolver v1-rust (SPRINT-15, docs/LANGUAGES.md Rust
             * section 5): BOTH channels resolve to the ONE file they
             * name -- the mod-tree record to the module file the
             * declaration names, the use-tree leaf through the anchor +
             * pop-ladder probe. No fan-out: Rust modules are files (v1
             * file-per-module model), so the single edge IS the whole
             * relation. The imports relation is a SET for Rust (the
             * mod + use channels and brace-group leaves can name the
             * same target; duplicates collapse). Self-edges are skipped
             * defensively. */
            const char *target = strncmp(r->specifier, "mod ", 4) == 0
                                     ? oc_resolve_rust_mod_decl(cx, r->from_path, r->specifier, a)
                                     : oc_resolve_rust_use_path(cx, r->from_path, r->specifier, a);
            if (!target)
                continue;
            oc_node_id to = index_find(cx, target);
            if (to == OC_NODE_INVALID || from == to)
                continue; /* self-import is not a useful edge */
            if (family_edge_exists(g, from, to))
                continue; /* imports relation is a set for Rust */
            if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                added++;
            else
                return -1;
            continue;
        }
        if (r->family == OC_LANG_RUBY) {
            /* Resolver v1-ruby (SPRINT-16, docs/LANGUAGES.md Ruby
             * section 5): both require channels resolve to the ONE file
             * they name (require_relative against the requiring file's
             * directory; bare require through the documented load-root
             * conventions under the analysis root). No fan-out: a
             * require loads ONE file. The imports relation is a SET for
             * Ruby (the same target may be named through several
             * records; duplicates collapse). Self-edges are skipped
             * defensively. */
            const char *target = oc_resolve_ruby_require(cx, r->from_path, r->specifier, a);
            if (!target)
                continue;
            oc_node_id to = index_find(cx, target);
            if (to == OC_NODE_INVALID || from == to)
                continue; /* self-import is not a useful edge */
            if (family_edge_exists(g, from, to))
                continue; /* imports relation is a set for Ruby */
            if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                added++;
            else
                return -1;
            continue;
        }
        if (r->family == OC_LANG_PHP) {
            /* Resolver v1-php (SPRINT-19, docs/LANGUAGES.md PHP
             * section 5): both channels resolve to the ONE file they
             * name -- the use channel through the composer.json
             * PSR-4/PSR-0 mapping of the analysis root (the longest
             * prefix wins), the include channel against the requiring
             * file's directory. No fan-out: a use or include names ONE
             * file. The imports relation is a SET for PHP (a use
             * declaration and an include can name the same target;
             * duplicates collapse). Self-edges are skipped defensively. */
            const char *target = strchr(r->specifier, ' ')
                                     ? oc_resolve_php_include(cx, r->from_path, r->specifier, a)
                                     : oc_resolve_php_use(cx, r->from_path, r->specifier, a);
            if (!target)
                continue;
            oc_node_id to = index_find(cx, target);
            if (to == OC_NODE_INVALID || from == to)
                continue; /* self-import is not a useful edge */
            if (family_edge_exists(g, from, to))
                continue; /* imports relation is a set for PHP */
            if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                added++;
            else
                return -1;
            continue;
        }
        if (r->family == OC_LANG_C || r->family == OC_LANG_CPP) {
            /* Resolver v1-c (SPRINT-20) / v1-cpp (SPRINT-21,
             * docs/LANGUAGES.md C++ section 5): both channels resolve to
             * the ONE file they name -- the quoted include against the
             * including file's directory, the angle-bracket system include
             * NEVER (headers outside the analyzed set stay external by
             * contract). No fan-out: an include directive names ONE file.
             * The imports relation is a SET for both families (a header
             * can be included twice in one file; duplicates collapse).
             * Self-edges are skipped defensively. */
            const char *target = oc_resolve_c_include(cx, r->from_path, r->specifier, a);
            if (!target)
                continue;
            oc_node_id to = index_find(cx, target);
            if (to == OC_NODE_INVALID || from == to)
                continue; /* self-import is not a useful edge */
            if (family_edge_exists(g, from, to))
                continue; /* imports relation is a set for C */
            if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
                added++;
            else
                return -1;
            continue;
        }
        const char *target = oc_resolve_specifier(cx, a, r->from_path, r->specifier, r->family);
        if (!target)
            continue;
        oc_node_id to = index_find(cx, target);
        if (to == OC_NODE_INVALID || from == to)
            continue; /* self-import is not a useful edge */
        if (oc_graph_add_edge(g, from, to, OC_EDGE_IMPORTS))
            added++;
        else
            return -1;
    }
    return added;
}

long oc_imports_resolve_ex(oc_graph *g, const oc_import_vec *in, oc_arena *a,
                           const char *const *roots, size_t n_roots) {
    /* No hints: the Kotlin and Swift declared-fact resolvers have
     * nothing to key on and every Kotlin/Swift specifier stays
     * unresolved (the honest degradation, documented above). */
    return oc_imports_resolve_ex_hints(g, in, a, roots, n_roots, NULL);
}

long oc_imports_resolve(oc_graph *g, const oc_import_vec *in, oc_arena *a) {
    /* No analysis roots given: the tsconfig search is bounded by the
     * specifier paths themselves (stops at "."/"/"), never the FS. */
    return oc_imports_resolve_ex(g, in, a, NULL, 0);
}
