#include "oc/surface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oc/imports.h"
#include "oc/tsconfig.h"
#include "pathutil.h"

/* ------------------------------------------------------------ vecs --- */

void oc_surface_exports_init(oc_export_vec *v) {
    memset(v, 0, sizeof(*v));
}

void oc_surface_exports_destroy(oc_export_vec *v) {
    free(v->items);
    memset(v, 0, sizeof(*v));
}

int oc_surface_export_push(oc_export_vec *v, oc_arena *a, const char *path, const char *name,
                           size_t line, int from_all) {
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 64;
        oc_export_rec *grown = (oc_export_rec *)realloc(v->items, cap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = cap;
    }
    oc_export_rec *r = &v->items[v->n++];
    r->path = oc_arena_strdup(a, path);
    r->name = oc_arena_strdup(a, name);
    r->line = line;
    r->from_all = from_all;
    r->used = 0;
    return r->path != NULL && r->name != NULL;
}

void oc_surface_binds_init(oc_bind_vec *v) {
    memset(v, 0, sizeof(*v));
}

void oc_surface_binds_destroy(oc_bind_vec *v) {
    free(v->items);
    memset(v, 0, sizeof(*v));
}

int oc_surface_bind_push(oc_bind_vec *v, oc_arena *a, const char *from_path, const char *specifier,
                         const char *name, oc_lang_family family) {
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 64;
        oc_bind_rec *grown = (oc_bind_rec *)realloc(v->items, cap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = cap;
    }
    oc_bind_rec *r = &v->items[v->n++];
    r->from_path = oc_arena_strdup(a, from_path);
    r->specifier = oc_arena_strdup(a, specifier);
    r->name = name ? oc_arena_strdup(a, name) : NULL;
    r->family = family;
    return r->from_path != NULL && r->specifier != NULL && (name == NULL || r->name != NULL);
}

void oc_surface_hints_init(oc_hint_vec *v) {
    memset(v, 0, sizeof(*v));
}

void oc_surface_hints_destroy(oc_hint_vec *v) {
    free(v->items);
    memset(v, 0, sizeof(*v));
}

int oc_surface_hint_push(oc_hint_vec *v, oc_arena *a, const char *path, int has_shebang) {
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 64;
        oc_file_hint *grown = (oc_file_hint *)realloc(v->items, cap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = cap;
    }
    oc_file_hint *h = &v->items[v->n++];
    h->path = oc_arena_strdup(a, path);
    h->has_shebang = has_shebang;
    h->has_main = 0;
    h->has_top_statements = 0;
    h->package_name = NULL;
    h->namespaces = NULL;
    h->n_namespaces = 0;
    h->symbols = NULL;
    h->n_symbols = 0;
    h->swift_targets = NULL;
    h->swift_target_paths = NULL;
    h->n_swift_targets = 0;
    return h->path != NULL;
}

int oc_surface_hint_push_package(oc_hint_vec *v, oc_arena *a, const char *path,
                                 const char *package_name) {
    for (size_t i = 0; i < v->n; i++) {
        if (strcmp(v->items[i].path, path) != 0)
            continue;
        v->items[i].package_name = oc_arena_strdup(a, package_name);
        return v->items[i].package_name != NULL;
    }
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 64;
        oc_file_hint *grown = (oc_file_hint *)realloc(v->items, cap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = cap;
    }
    oc_file_hint *h = &v->items[v->n++];
    h->path = oc_arena_strdup(a, path);
    h->has_shebang = 0;
    h->has_main = 0;
    h->has_top_statements = 0;
    h->package_name = oc_arena_strdup(a, package_name);
    h->namespaces = NULL;
    h->n_namespaces = 0;
    h->symbols = NULL;
    h->n_symbols = 0;
    h->swift_targets = NULL;
    h->swift_target_paths = NULL;
    h->n_swift_targets = 0;
    return h->path != NULL && h->package_name != NULL;
}

int oc_surface_hint_push_main(oc_hint_vec *v, oc_arena *a, const char *path) {
    for (size_t i = 0; i < v->n; i++) {
        if (strcmp(v->items[i].path, path) != 0)
            continue;
        v->items[i].has_main = 1;
        return 1;
    }
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 64;
        oc_file_hint *grown = (oc_file_hint *)realloc(v->items, cap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = cap;
    }
    oc_file_hint *h = &v->items[v->n++];
    h->path = oc_arena_strdup(a, path);
    h->has_shebang = 0;
    h->has_main = 1;
    h->has_top_statements = 0;
    h->package_name = NULL;
    h->namespaces = NULL;
    h->n_namespaces = 0;
    h->symbols = NULL;
    h->n_symbols = 0;
    h->swift_targets = NULL;
    h->swift_target_paths = NULL;
    h->n_swift_targets = 0;
    return h->path != NULL;
}

int oc_surface_hint_push_top(oc_hint_vec *v, oc_arena *a, const char *path) {
    for (size_t i = 0; i < v->n; i++) {
        if (strcmp(v->items[i].path, path) != 0)
            continue;
        v->items[i].has_top_statements = 1;
        return 1;
    }
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 64;
        oc_file_hint *grown = (oc_file_hint *)realloc(v->items, cap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = cap;
    }
    oc_file_hint *h = &v->items[v->n++];
    h->path = oc_arena_strdup(a, path);
    h->has_shebang = 0;
    h->has_main = 0;
    h->has_top_statements = 1;
    h->package_name = NULL;
    h->namespaces = NULL;
    h->n_namespaces = 0;
    h->symbols = NULL;
    h->n_symbols = 0;
    h->swift_targets = NULL;
    h->swift_target_paths = NULL;
    h->n_swift_targets = 0;
    return h->path != NULL;
}

int oc_surface_hint_push_namespace(oc_hint_vec *v, oc_arena *a, const char *path, const char *ns) {
    oc_file_hint *h = NULL;
    for (size_t i = 0; i < v->n; i++) {
        if (strcmp(v->items[i].path, path) != 0)
            continue;
        h = &v->items[i];
        break;
    }
    if (!h) {
        if (v->n == v->cap) {
            size_t cap = v->cap ? v->cap * 2 : 64;
            oc_file_hint *grown = (oc_file_hint *)realloc(v->items, cap * sizeof(*grown));
            if (!grown)
                return 0;
            v->items = grown;
            v->cap = cap;
        }
        h = &v->items[v->n++];
        h->path = oc_arena_strdup(a, path);
        h->has_shebang = 0;
        h->has_main = 0;
        h->has_top_statements = 0;
        h->package_name = NULL;
        h->namespaces = NULL;
        h->n_namespaces = 0;
        h->symbols = NULL;
        h->n_symbols = 0;
        h->swift_targets = NULL;
        h->swift_target_paths = NULL;
        h->n_swift_targets = 0;
        if (!h->path)
            return 0;
    }
    /* duplicates within one file collapse (two identical declarations) */
    for (size_t i = 0; i < h->n_namespaces; i++) {
        if (strcmp(h->namespaces[i], ns) == 0)
            return 1;
    }
    /* arena grow: a fresh array each time, the old one stays (bounded by
     * the namespaces a single file declares -- tiny) */
    char **grown = (char **)oc_arena_alloc(a, (h->n_namespaces + 1) * sizeof(char *));
    if (!grown)
        return 0;
    for (size_t i = 0; i < h->n_namespaces; i++)
        grown[i] = h->namespaces[i];
    grown[h->n_namespaces] = oc_arena_strdup(a, ns);
    if (!grown[h->n_namespaces])
        return 0;
    h->namespaces = grown;
    h->n_namespaces++;
    return 1;
}

int oc_surface_hint_push_symbol(oc_hint_vec *v, oc_arena *a, const char *path, const char *name) {
    oc_file_hint *h = NULL;
    for (size_t i = 0; i < v->n; i++) {
        if (strcmp(v->items[i].path, path) != 0)
            continue;
        h = &v->items[i];
        break;
    }
    if (!h) {
        if (v->n == v->cap) {
            size_t cap = v->cap ? v->cap * 2 : 64;
            oc_file_hint *grown = (oc_file_hint *)realloc(v->items, cap * sizeof(*grown));
            if (!grown)
                return 0;
            v->items = grown;
            v->cap = cap;
        }
        h = &v->items[v->n++];
        h->path = oc_arena_strdup(a, path);
        h->has_shebang = 0;
        h->has_main = 0;
        h->has_top_statements = 0;
        h->package_name = NULL;
        h->namespaces = NULL;
        h->n_namespaces = 0;
        h->symbols = NULL;
        h->n_symbols = 0;
        h->swift_targets = NULL;
        h->swift_target_paths = NULL;
        h->n_swift_targets = 0;
        if (!h->path)
            return 0;
    }
    /* duplicates within one file collapse (an overload set exports one
     * name once -- the import claims the NAME, not a signature) */
    for (size_t i = 0; i < h->n_symbols; i++) {
        if (strcmp(h->symbols[i], name) == 0)
            return 1;
    }
    /* arena grow: a fresh array each time, the old one stays (bounded by
     * the top-level declarations a single file makes -- tiny) */
    char **grown = (char **)oc_arena_alloc(a, (h->n_symbols + 1) * sizeof(char *));
    if (!grown)
        return 0;
    for (size_t i = 0; i < h->n_symbols; i++)
        grown[i] = h->symbols[i];
    grown[h->n_symbols] = oc_arena_strdup(a, name);
    if (!grown[h->n_symbols])
        return 0;
    h->symbols = grown;
    h->n_symbols++;
    return 1;
}

int oc_surface_hint_push_swift_target(oc_hint_vec *v, oc_arena *a, const char *path,
                                      const char *name, const char *declared_path) {
    if (!v || !a || !path || !name || name[0] == '\0')
        return 0;
    oc_file_hint *h = NULL;
    for (size_t i = 0; i < v->n; i++) {
        if (strcmp(v->items[i].path, path) != 0)
            continue;
        h = &v->items[i];
        break;
    }
    if (!h) {
        if (v->n == v->cap) {
            size_t cap = v->cap ? v->cap * 2 : 64;
            oc_file_hint *grown = (oc_file_hint *)realloc(v->items, cap * sizeof(*grown));
            if (!grown)
                return 0;
            v->items = grown;
            v->cap = cap;
        }
        h = &v->items[v->n++];
        h->path = oc_arena_strdup(a, path);
        h->has_shebang = 0;
        h->has_main = 0;
        h->has_top_statements = 0;
        h->package_name = NULL;
        h->namespaces = NULL;
        h->n_namespaces = 0;
        h->symbols = NULL;
        h->n_symbols = 0;
        h->swift_targets = NULL;
        h->swift_target_paths = NULL;
        h->n_swift_targets = 0;
        if (!h->path)
            return 0;
    }
    /* duplicates within one manifest collapse (SPM rejects a duplicate
     * target name at build time -- the analyzer reports what a second
     * one would claim once, the set semantics) */
    for (size_t i = 0; i < h->n_swift_targets; i++) {
        if (strcmp(h->swift_targets[i], name) == 0)
            return 1;
    }
    char **gn = (char **)oc_arena_alloc(a, (h->n_swift_targets + 1) * sizeof(char *));
    char **gp = (char **)oc_arena_alloc(a, (h->n_swift_targets + 1) * sizeof(char *));
    if (!gn || !gp)
        return 0;
    for (size_t i = 0; i < h->n_swift_targets; i++) {
        gn[i] = h->swift_targets[i];
        gp[i] = h->swift_target_paths[i];
    }
    gn[h->n_swift_targets] = oc_arena_strdup(a, name);
    gp[h->n_swift_targets] = declared_path ? oc_arena_strdup(a, declared_path) : NULL;
    if (!gn[h->n_swift_targets])
        return 0;
    h->swift_targets = gn;
    h->swift_target_paths = gp;
    h->n_swift_targets++;
    return 1;
}

/* ---------------------------------------------------------- globs ----- */

/* Entry-point glob matcher (pack entry_points). Supported syntax:
 *   "**"  any number of path segments, including none
 *   "*"   any characters within ONE segment (never '/')
 *   anything else matches literally within a segment.
 * Deterministic and pure; recursion is bounded by the number of literal
 * segments in the pattern (pack-declared data, never user-sized). */

/* Single-segment match with at most one '*' (head/tail literal match). */
static int seg_match(const char *pat, size_t plen, const char *s, size_t slen) {
    const char *star = memchr(pat, '*', plen);
    if (!star)
        return plen == slen && (plen == 0 || memcmp(pat, s, slen) == 0);
    size_t head = (size_t)(star - pat);
    size_t tail = plen - head - 1;
    if (slen < head + tail)
        return 0;
    if (head > 0 && memcmp(pat, s, head) != 0)
        return 0;
    return tail == 0 || memcmp(star + 1, s + slen - tail, tail) == 0;
}

static int glob_match_impl(const char *pat, const char *s) {
    while (1) {
        if (*pat == '\0')
            return *s == '\0';
        if (pat[0] == '*' && pat[1] == '*') {
            pat += 2;
            if (*pat == '/')
                pat++;
            if (*pat == '\0')
                return 1; /* trailing ** swallows the rest */
            if (glob_match_impl(pat, s))
                return 1; /* ** consumed zero segments */
            for (const char *q = s; *q; q++) {
                if (*q == '/' && glob_match_impl(pat, q + 1))
                    return 1;
            }
            return 0;
        }
        const char *pslash = strchr(pat, '/');
        size_t plen = pslash ? (size_t)(pslash - pat) : strlen(pat);
        const char *sslash = strchr(s, '/');
        size_t slen = sslash ? (size_t)(sslash - s) : strlen(s);
        if (!seg_match(pat, plen, s, slen))
            return 0;
        if (pslash) {
            if (!sslash)
                return 0; /* pattern has more segments than the path */
            pat = pslash + 1;
            s = sslash + 1;
        } else {
            return sslash == NULL; /* pattern exhausted: path must be too */
        }
    }
}

int oc_glob_match_entry(const char *pattern, const char *path) {
    if (pattern[0] == '\0')
        return 0;
    if (pattern[0] == '*' && pattern[1] == '*')
        return glob_match_impl(pattern, path);
    /* Pack-declared bare names ("main.py") match the basename too. */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (glob_match_impl(pattern, path))
        return 1;
    return seg_match(pattern, strlen(pattern), base, strlen(base));
}

/* Test-file conventions: files the test runner executes directly, so a
 * zero in-degree is expected and never OC006 material. */
int oc_path_is_test_convention(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    static const char *const SUFFIXES[] = {".test.js",  ".test.ts",  ".test.tsx", ".test.jsx",
                                           ".test.mjs", ".test.cjs", ".spec.js",  ".spec.ts",
                                           ".spec.tsx", ".spec.jsx", ".spec.mjs", ".spec.cjs"};
    for (size_t i = 0; i < sizeof(SUFFIXES) / sizeof(SUFFIXES[0]); i++) {
        size_t bl = strlen(base), sl = strlen(SUFFIXES[i]);
        if (bl > sl && strcmp(base + bl - sl, SUFFIXES[i]) == 0)
            return 1;
    }
    /* Go: `go test` runs *_test.go files directly (SPRINT-10,
     * docs/LANGUAGES.md §Go.6) -- language-blind runner convention,
     * like conftest.py. */
    size_t bl = strlen(base);
    if (bl > 8 && strcmp(base + bl - 8, "_test.go") == 0)
        return 1;
    /* Ruby (SPRINT-16, docs/LANGUAGES.md Ruby section 6): the
     * runner-collected NAME conventions -- test_*.rb (minitest),
     * *_test.rb (minitest) and *_spec.rb (RSpec) are runner data: a zero
     * in-degree is expected and is never OC006 material. Same
     * language-blind runner-naming discipline as _test.go and
     * Test*.java. Deliberately NARROW: name-based only, NO directory
     * claiming (the Sprint 2 over-broad-directory lesson -- test/support
     * helper files legitimately reference each other and must keep their
     * honest in-degree). */
    if (bl > 3 && strcmp(base + bl - 3, ".rb") == 0) {
        size_t stem = bl - 3;
        if (stem > 5 && memcmp(base + stem - 5, "_test", 5) == 0)
            return 1; /* *_test.rb */
        if (stem > 5 && memcmp(base + stem - 5, "_spec", 5) == 0)
            return 1; /* *_spec.rb */
        if (stem >= 5 && memcmp(base, "test_", 5) == 0)
            return 1; /* test_*.rb (stem >= 5 keeps a bare "test_" name out) */
    }
    /* Rust (SPRINT-15, docs/LANGUAGES.md Rust section 6): a .rs file
     * DIRECTLY inside a tests/ directory is runner data -- cargo
     * compiles each as its own integration-test crate, so a zero
     * in-degree is expected and is never OC006 material. Deliberately
     * narrow: only <...>/tests/x.rs (the Sprint 2 over-broad-directory
     * lesson; tests/sub/mod.rs support modules are NOT auto-claimed). */
    {
        size_t pl = strlen(path);
        if (pl > 6 && strcmp(path + pl - 3, ".rs") == 0) {
            const char *slash = strrchr(path, '/');
            if (slash) {
                /* the dirname's BASENAME must be exactly "tests" */
                const char *d = path;
                for (const char *q = path; q < slash; q++)
                    if (*q == '/')
                        d = q + 1;
                if (slash - d == 5 && memcmp(d, "tests", 5) == 0)
                    return 1;
            }
        }
    }
    /* Java: the Maven Surefire default includes (runner-collected test
     * classes, SPRINT-11, docs/LANGUAGES.md §Java.6): Test*.java,
     * *Test.java, *Tests.java, *TestCase.java. Same language-blind
     * runner-convention discipline as _test.go; a zero in-degree test
     * class is expected and never OC006 material. The suffix match is
     * byte-exact (memcmp of exactly the suffix length): a plain strcmp
     * against a mid-string pointer compares the REST of the name
     * ("Test.java" vs "Test" never matches) -- a bug the gson corpus
     * gate caught live; the resolver fixture alone passed for the wrong
     * reason (its StoreTest had in-degree). */
    if (bl > 5 && strcmp(base + bl - 5, ".java") == 0) {
        static const char *const JAVA_TEST_SUFFIXES[] = {"Test", "Tests", "TestCase"};
        size_t stem = bl - 5; /* length without ".java" */
        for (size_t i = 0; i < 3; i++) {
            size_t sl = strlen(JAVA_TEST_SUFFIXES[i]);
            if (stem > sl && memcmp(base + stem - sl, JAVA_TEST_SUFFIXES[i], sl) == 0)
                return 1;
        }
        if (stem >= 4 && memcmp(base, "Test", 4) == 0)
            return 1; /* Test*.java */
    }
    static const char *const PY_NAMES[] = {"conftest.py"};
    for (size_t i = 0; i < sizeof(PY_NAMES) / sizeof(PY_NAMES[0]); i++) {
        if (strcmp(base, PY_NAMES[i]) == 0)
            return 1;
    }
    if (bl > 8 && strncmp(base, "test_", 5) == 0 && strcmp(base + bl - 3, ".py") == 0)
        return 1;
    if (bl > 8 && strncmp(base + bl - 8, "_test.py", 8) == 0)
        return 1;
    /* Jest/vitest runner-collected directory. Deliberately NOT matching
     * bare "tests/" dirs: Sprint 2's own fixtures live under tests/ and
     * over-broad directory matching classified whole repos as entry
     * points, silencing OC006 (caught by test_sprint2 before landing). */
    if (strstr(path, "/__tests__/") == path || strstr(path, "/__tests__/"))
        return 1;
    return 0;
}

/* ----------------------------------------------------------- build ---- */

oc_surface *oc_surface_build(oc_arena *a, oc_graph *g, const oc_bind_vec *binds,
                             oc_export_vec *exports, const oc_hint_vec *hints,
                             const char *const *roots, size_t n_roots, const oc_pack_set *ps,
                             oc_status *st) {
    oc_surface *s = (oc_surface *)oc_arena_alloc(a, sizeof(*s));
    if (!s) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building surface");
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->g = g;
    s->exports = *exports; /* the vec struct: items pointer + counts; the
                            * used flags below are marked in place, so the
                            * pass-side view sees the same entries */

    /* Bind resolution + usage marking (fail-open for wildcards). */
    oc_resolve_ctx *cx = oc_resolve_ctx_new(a, g, roots, n_roots);
    if (!cx) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building resolve context");
        return NULL;
    }
    /* the declared-fact parse hints (Kotlin resolver v1-kotlin,
     * SPRINT-22): the same channel the edge pass used */
    oc_resolve_ctx_set_hints(cx, hints);
    for (size_t i = 0; i < binds->n; i++) {
        const oc_bind_rec *b = &binds->items[i];
        if (b->family == OC_LANG_JAVA) {
            /* Java (resolver v1-java, SPRINT-11): a wildcard import
             * `a.b.*` is a package claim -- it marks the whole package
             * used, fail-open by contract (docs/LANGUAGES.md §Java.5/
             * §Java.6); it never binds a name. Only files DIRECTLY
             * inside the package directory are marked: subdirectories
             * are separate packages. Single-type and static imports
             * fall through to the generic path below: they resolve to
             * the ONE class file and match the bound class name. */
            size_t sl = strlen(b->specifier);
            if (sl >= 2 && strcmp(b->specifier + sl - 2, ".*") == 0) {
                const char *dir = oc_resolve_java_pkg_dir(cx, b->specifier, a);
                if (!dir)
                    continue;
                s->binds_resolved++;
                size_t dl = strlen(dir);
                for (size_t j = 0; j < exports->n; j++) {
                    oc_export_rec *e = &exports->items[j];
                    if (strncmp(e->path, dir, dl) != 0 || e->path[dl] != '/')
                        continue;
                    if (strchr(e->path + dl + 1, '/') != NULL)
                        continue; /* subdirectory: separate package */
                    e->used = 1;  /* package claim = fail-open wildcard */
                }
                continue;
            }
        }
        if (b->family == OC_LANG_GO) {
            /* Go (resolver v1-go, SPRINT-10): a package import marks the
             * WHOLE package used -- fail-open by contract
             * (docs/LANGUAGES.md §Go.5/§Go.6); the recorded alias never
             * narrows it. Only files DIRECTLY inside the package
             * directory are marked: subdirectories are separate
             * packages. Consequence (asserted by the acceptance gate):
             * at whole-package granularity OC005's verdict set for Go
             * is empty by construction -- the honest outcome, versus
             * guessing name-level usage. */
            const char *dir = oc_resolve_go_pkg_dir(cx, b->specifier, a);
            if (!dir)
                continue;
            s->binds_resolved++;
            size_t dl = strlen(dir);
            for (size_t j = 0; j < exports->n; j++) {
                oc_export_rec *e = &exports->items[j];
                if (strncmp(e->path, dir, dl) != 0 || e->path[dl] != '/')
                    continue;
                if (strchr(e->path + dl + 1, '/') != NULL)
                    continue; /* subdirectory: separate package */
                e->used = 1;  /* package import = fail-open wildcard */
            }
            continue;
        }
        if (b->family == OC_LANG_KOTLIN || b->family == OC_LANG_SCALA) {
            /* Kotlin (resolver v1-kotlin, SPRINT-22, the C# branch
             * shape) and Scala (resolver v1-scala, SPRINT-24, the SHARED
             * declared-fact implementation): the declared-fact targets
             * mark per-target. A wildcard claim (NULL name) marks each
             * target file's exports used wholesale -- fail-open by
             * contract, the Java wildcard discipline. A named claim (the
             * original last segment) marks only the matching export of
             * each target file: several files may carry the name, the
             * over-mark direction is bounded by the declared facts
             * (docs/LANGUAGES.md Kotlin section 5/6, Scala section 5/6). */
            const char **targets = NULL;
            size_t n_targets = 0;
            if (oc_resolve_kotlin_import(cx, b->from_path, b->specifier, a, &targets, &n_targets) <
                0)
                continue;
            if (n_targets == 0)
                continue;
            s->binds_resolved++;
            for (size_t t = 0; t < n_targets; t++) {
                for (size_t j = 0; j < exports->n; j++) {
                    oc_export_rec *e = &exports->items[j];
                    if (strcmp(e->path, targets[t]) != 0)
                        continue;
                    if (b->name == NULL || strcmp(e->name, b->name) == 0)
                        e->used = 1;
                }
            }
            continue;
        }
        if (b->family == OC_LANG_SWIFT) {
            /* Swift (resolver v1-swift, SPRINT-23, the Kotlin branch
             * shape): the declared-fact targets mark per-target. A
             * whole-module / submodule claim (NULL name) marks each
             * target file's exports used wholesale -- importing a
             * module loads ALL of it, fail-open by contract, the Kotlin
             * wildcard discipline. A kind-scoped member claim (the
             * last path segment) marks only the matching export of each
             * target file: several files may carry the name, the
             * over-mark direction is bounded by the declared facts
             * (docs/LANGUAGES.md Swift section 5/6). */
            const char **targets = NULL;
            size_t n_targets = 0;
            if (oc_resolve_swift_import(cx, b->from_path, b->specifier, a, &targets, &n_targets) <
                0)
                continue;
            if (n_targets == 0)
                continue;
            s->binds_resolved++;
            for (size_t t = 0; t < n_targets; t++) {
                for (size_t j = 0; j < exports->n; j++) {
                    oc_export_rec *e = &exports->items[j];
                    if (strcmp(e->path, targets[t]) != 0)
                        continue;
                    if (b->name == NULL || strcmp(e->name, b->name) == 0)
                        e->used = 1;
                }
            }
            continue;
        }
        if (b->family == OC_LANG_JAVA && b->name == NULL &&
            strcmp(b->specifier + strlen(b->specifier) - 2, ".*") == 0) {
            /* Java wildcard (resolver v1-java, SPRINT-12): `a.b.*` names
             * a PACKAGE -- marks every public top-level type of the
             * package's direct-child .java files used, fail-open by
             * contract (docs/LANGUAGES.md Java section, the same
             * discipline as `export * from`). */
            const char *dir = oc_resolve_java_pkg_dir(cx, b->specifier, a);
            if (!dir)
                continue;
            s->binds_resolved++;
            size_t dl = strlen(dir);
            for (size_t j = 0; j < exports->n; j++) {
                oc_export_rec *e = &exports->items[j];
                if (strncmp(e->path, dir, dl) != 0 || e->path[dl] != '/')
                    continue;
                if (strchr(e->path + dl + 1, '/') != NULL)
                    continue; /* subdirectory: separate package */
                if (strlen(e->path + dl + 1) < 5 ||
                    strcmp(e->path + strlen(e->path) - 5, ".java") != 0)
                    continue; /* package-info/module-info and non-.java: not export-bearing */
                e->used = 1;  /* wildcard package claim: fail-open */
            }
            continue;
        }
        if (b->family == OC_LANG_CSHARP) {
            /* C# (resolver v1-csharp, SPRINT-13): the specifier carries
             * the directive's own tokens.
             *  - `static A.B.C` (name != NULL): a TYPE claim -- falls
             *    through to the generic file path below, which marks
             *    the export matching the binding name (the Java
             *    single-type discipline);
             *  - `A.B.C` (name == NULL): a NAMESPACE claim -- marks
             *    every public top-level type of the namespace's
             *    direct-child .cs files used, fail-open by contract
             *    (docs/LANGUAGES.md C# section, the same discipline as
             *    the Go package import and the Java wildcard);
             *  - `X = A.B.C` (contains " = "): an ALIAS claim -- the
             *    documented ordered probe (type file first, namespace
             *    directory second); a file hit marks the whole file
             *    used, a directory hit marks the namespace fail-open;
             *    the alias never narrows marking (the Go alias
             *    discipline). */
            const char *spec_target = b->specifier;
            int is_alias = strstr(b->specifier, " = ") != NULL;
            if (is_alias)
                spec_target = strstr(b->specifier, " = ") + 3;
            if (strncmp(spec_target, "static ", 7) == 0 && !is_alias) {
                /* static type claim: the generic path below resolves
                 * the file probe and marks the binding name */
                ; /* fall through */
            } else if (!b->name || is_alias) {
                const char *type_target =
                    is_alias ? oc_resolve_csharp_type_file(cx, spec_target, a) : NULL;
                const char *dir = NULL;
                if (type_target) {
                    /* alias resolved as a TYPE file: fail-open, the
                     * alias never narrows marking */
                    s->binds_resolved++;
                    for (size_t j = 0; j < exports->n; j++) {
                        if (strcmp(exports->items[j].path, type_target) == 0)
                            exports->items[j].used = 1;
                    }
                    continue;
                }
                dir = oc_resolve_csharp_ns_dir(cx, spec_target, a);
                if (!dir)
                    continue;
                s->binds_resolved++;
                size_t dl = strlen(dir);
                for (size_t j = 0; j < exports->n; j++) {
                    oc_export_rec *e = &exports->items[j];
                    if (strncmp(e->path, dir, dl) != 0 || e->path[dl] != '/')
                        continue;
                    if (strchr(e->path + dl + 1, '/') != NULL)
                        continue; /* subdirectory: separate namespace */
                    size_t pl2 = strlen(e->path);
                    if (pl2 < 3 || strcmp(e->path + pl2 - 3, ".cs") != 0)
                        continue; /* non-.cs files are not export-bearing */
                    e->used = 1;  /* namespace claim: fail-open wildcard */
                }
                continue;
            }
            /* static type claim (or an unexpected shape with a name):
             * the generic file path below handles it */
        }
        const char *target = oc_resolve_specifier(cx, a, b->from_path, b->specifier, b->family);
        if (!target)
            continue;
        s->binds_resolved++;
        for (size_t j = 0; j < exports->n; j++) {
            oc_export_rec *e = &exports->items[j];
            if (strcmp(e->path, target) != 0)
                continue;
            if (!b->name)
                e->used = 1; /* wildcard/require()/dynamic: all used */
            else if (strcmp(e->name, b->name) == 0)
                e->used = 1;
        }
    }

    /* Package/namespace-mate marking (SPRINT-14, fail-open): Java
     * same-package types need NO import directive and C# same-namespace
     * types need NO using directive -- the import-driven marking alone
     * under-reported and produced FALSE OC005 positives for types used
     * only by their mates. A file's exports are marked used when a MATE
     * exists among the ANALYZED files (graph file nodes -- excluded and
     * pack-less files were never parsed, so they are not mates): Java --
     * any other .java file in the same directory (same package by the
     * class-file model; same-directory = same-package is Java's own
     * rule); C# -- any other analyzed file declaring the same namespace
     * (the declared-namespace parse hints; never cross-checked against
     * the directory). Over-marking, never fabricating an unused verdict;
     * the OC005 verdict-set consequence is unchanged (the pass's own
     * precondition -- the file must itself be imported -- still bounds
     * the verdict set). */
    {
        typedef struct {
            const char *dir;
            size_t count;
        } mate_dir;
        typedef struct {
            const char *ns;
            size_t count;
        } mate_ns;
        size_t cap_dirs = 64, n_dirs = 0, cap_ns = 64, n_ns = 0;
        mate_dir *dirs = (mate_dir *)oc_arena_alloc(a, cap_dirs * sizeof(mate_dir));
        mate_ns *nss = (mate_ns *)oc_arena_alloc(a, cap_ns * sizeof(mate_ns));
        int mate_oom = dirs == NULL || nss == NULL;
        if (!mate_oom) {
            for (size_t i = 0; i < g->n_nodes; i++) {
                const oc_node *n = &g->nodes[i];
                if (n->kind != OC_NODE_FILE)
                    continue;
                size_t pl = strlen(n->path);
                if (pl < 5 || strcmp(n->path + pl - 5, ".java") != 0)
                    continue;
                char dir[1024];
                oc_path_dirname(n->path, dir, sizeof(dir));
                size_t k = 0;
                for (; k < n_dirs; k++)
                    if (strcmp(dirs[k].dir, dir) == 0)
                        break;
                if (k < n_dirs) {
                    dirs[k].count++;
                    continue;
                }
                if (n_dirs == cap_dirs) {
                    size_t ncap = cap_dirs * 2;
                    mate_dir *grown = (mate_dir *)oc_arena_alloc(a, ncap * sizeof(mate_dir));
                    if (!grown) {
                        mate_oom = 1;
                        break;
                    }
                    memcpy(grown, dirs, n_dirs * sizeof(mate_dir));
                    dirs = grown;
                    cap_dirs = ncap;
                }
                dirs[n_dirs].dir = oc_arena_strdup(a, dir);
                if (!dirs[n_dirs].dir) {
                    mate_oom = 1;
                    break;
                }
                dirs[n_dirs].count = 1;
                n_dirs++;
            }
            for (size_t i = 0; i < hints->n; i++) {
                const oc_file_hint *h = &hints->items[i];
                if (h->n_namespaces == 0)
                    continue;
                for (size_t k = 0; k < h->n_namespaces; k++) {
                    size_t m = 0;
                    for (; m < n_ns; m++)
                        if (strcmp(nss[m].ns, h->namespaces[k]) == 0)
                            break;
                    if (m < n_ns) {
                        nss[m].count++; /* one FILE per hint (deduped at push) */
                        continue;
                    }
                    if (n_ns == cap_ns) {
                        size_t ncap = cap_ns * 2;
                        mate_ns *grown = (mate_ns *)oc_arena_alloc(a, ncap * sizeof(mate_ns));
                        if (!grown) {
                            mate_oom = 1;
                            break;
                        }
                        memcpy(grown, nss, n_ns * sizeof(mate_ns));
                        nss = grown;
                        cap_ns = ncap;
                    }
                    nss[n_ns].ns = h->namespaces[k]; /* hint-owned string */
                    nss[n_ns].count = 1;
                    n_ns++;
                }
                if (mate_oom)
                    break;
            }
            for (size_t i = 0; i < exports->n && !mate_oom; i++) {
                oc_export_rec *e = &exports->items[i];
                if (e->used)
                    continue;
                size_t pl = strlen(e->path);
                char dir[1024];
                if (pl >= 5 && strcmp(e->path + pl - 5, ".java") == 0) {
                    oc_path_dirname(e->path, dir, sizeof(dir));
                    for (size_t k = 0; k < n_dirs; k++) {
                        if (strcmp(dirs[k].dir, dir) == 0 && dirs[k].count >= 2) {
                            e->used = 1; /* a package-mate exists: fail-open */
                            break;
                        }
                    }
                    continue;
                }
                if (pl < 3 || strcmp(e->path + pl - 3, ".cs") != 0)
                    continue;
                const oc_file_hint *h = NULL;
                for (size_t k = 0; k < hints->n; k++) {
                    if (strcmp(hints->items[k].path, e->path) == 0) {
                        h = &hints->items[k];
                        break;
                    }
                }
                if (!h)
                    continue;
                for (size_t k = 0; k < h->n_namespaces && !e->used; k++) {
                    for (size_t m = 0; m < n_ns; m++) {
                        if (strcmp(nss[m].ns, h->namespaces[k]) == 0 && nss[m].count >= 2) {
                            e->used = 1; /* a namespace-mate file exists */
                            break;
                        }
                    }
                }
            }
        }
        if (mate_oom) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory building mate marking");
            return NULL;
        }
    }

    /* Python __all__: when a file declares it, ONLY the listed names are
     * public surface (PEP 8); the other recorded candidates are demoted. */
    for (size_t i = 0; i < exports->n; i++) {
        if (exports->items[i].from_all)
            continue;
        /* demote if the file has __all__ */
        int has_all = 0;
        for (size_t j = 0; j < exports->n; j++) {
            if (exports->items[j].from_all &&
                strcmp(exports->items[j].path, exports->items[i].path) == 0) {
                has_all = 1;
                break;
            }
        }
        if (has_all)
            exports->items[i].from_all = -1; /* demoted marker */
    }
    /* Drop demoted entries in place (stable compaction). */
    {
        size_t w = 0;
        for (size_t i = 0; i < exports->n; i++) {
            if (exports->items[i].from_all != -1)
                exports->items[w++] = exports->items[i];
        }
        exports->n = w;
    }

    /* In-degree over OC_EDGE_IMPORTS. */
    s->in_degree = (size_t *)oc_arena_alloc(a, g->n_nodes * sizeof(size_t));
    s->entry = (unsigned char *)oc_arena_alloc(a, g->n_nodes * sizeof(unsigned char));
    if (!s->in_degree || !s->entry) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building surface arrays");
        return NULL;
    }
    memset(s->in_degree, 0, g->n_nodes * sizeof(size_t));
    memset(s->entry, 0, g->n_nodes * sizeof(unsigned char));
    for (size_t i = 0; i < g->n_edges; i++) {
        if (g->edges[i].kind == OC_EDGE_IMPORTS)
            s->in_degree[g->edges[i].to]++;
    }

    /* Entry classification per file node. */
    for (size_t i = 0; i < g->n_nodes; i++) {
        const oc_node *n = &g->nodes[i];
        if (n->kind != OC_NODE_FILE)
            continue;
        int entry = 0;
        /* 1. parse-time hints: shebang (any language), the parsed
         * package_clause (Go, SPRINT-10), the parsed public static
         * void main fact (Java, SPRINT-12), the parsed static-Main and
         * top-level-statements facts (C#, SPRINT-13): each is a cheap
         * parse fact -- a `package main` file / a main-method file /
         * a top-level-statements file is an entry point exactly like a
         * shebang script is (docs/LANGUAGES.md C# section). */
        for (size_t h = 0; h < hints->n; h++) {
            if (strcmp(hints->items[h].path, n->path) == 0) {
                if (hints->items[h].has_shebang)
                    entry = 1;
                if (hints->items[h].has_main)
                    entry = 1;
                if (hints->items[h].has_top_statements)
                    entry = 1;
                if (hints->items[h].package_name &&
                    strcmp(hints->items[h].package_name, "main") == 0)
                    entry = 1;
                break;
            }
        }
        /* 2. test conventions */
        if (!entry && oc_path_is_test_convention(n->path))
            entry = 1;
        /* 3. pack entry_points globs */
        if (!entry && ps) {
            const oc_pack *pack = oc_packs_match_path(ps, n->path);
            if (pack) {
                for (size_t p = 0; p < pack->n_entry_points && !entry; p++)
                    entry = oc_glob_match_entry(pack->entry_points[p], n->path);
            }
        }
        s->entry[i] = (unsigned char)entry;
    }
    oc_status_clear(st);
    return s;
}

int oc_surface_is_entry(const oc_surface *s, const char *path, oc_node_id file_id) {
    if (!s || file_id == OC_NODE_INVALID || file_id >= s->g->n_nodes)
        return 0;
    (void)path;
    return s->entry[file_id] != 0;
}
