/* Sprint 10 (Track D wave 1, resolver sprint): Go resolver v1-go --
 * intra-repo dotted import paths resolve to the whole package
 * (docs/LANGUAGES.md §Go.5/§Go.6): root-order directory probing,
 * direct-children rule, fan-out edges, package-wide fail-open marking,
 * package_clause entry claiming, *_test.go convention, and the OC003 /
 * OC005 flip-on with the OC005 empty-verdict-set consequence. Also
 * pins the latent SPRINT-9 fix: grouped import ( ... ) blocks now
 * record their specifiers. go.mod is never read -- the decoy manifest
 * in the fixture corpus proves it end to end. */
#include "oc/common.h"
#include "oc/imports.h"
#include "oc/json.h"
#include "oc/pack.h"
#include "oc/surface.h"
#include "testkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the built oc binary"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1)
        return -1;
    if (rc == 0)
        return 0;
    return WEXITSTATUS(rc);
}

static int has_edge(const oc_graph *g, const char *from, const char *to) {
    oc_node_id f = oc_graph_find_file(g, from);
    oc_node_id t = oc_graph_find_file(g, to);
    if (f == OC_NODE_INVALID || t == OC_NODE_INVALID)
        return 0;
    for (size_t i = 0; i < g->n_edges; i++)
        if (g->edges[i].kind == OC_EDGE_IMPORTS && g->edges[i].from == f && g->edges[i].to == t)
            return 1;
    return 0;
}

/* --- unit: resolver v1-go positive semantics --------------------------- */
static void unit_go_resolver(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    /* Layout: store has two sources, one test file and a SUBDIRECTORY
     * (its own package), plus a GOPATH-style dotted-host tree rooted at
     * "golang.org" -- dots are literal directory names (Go's own
     * mapping, verbatim). */
    oc_graph_add_file(&g, "proj/store/store.go");
    oc_graph_add_file(&g, "proj/store/types.go");
    oc_graph_add_file(&g, "proj/store/store_test.go");
    oc_graph_add_file(&g, "proj/store/sub/inner.go");
    oc_graph_add_file(&g, "proj/main.go");
    oc_graph_add_file(&g, "proj/golang.org/x/net/html/atom.go");

    const char *roots[1] = {"proj"};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }

    /* Package directory: the directory the specifier names, VERBATIM
     * (dots included -- Go's own mapping). */
    const char *dir = oc_resolve_go_pkg_dir(cx, "store", &a);
    CHECK_STR_EQ(dir, "proj/store");
    /* Dotted host: literal directory names below the root. */
    dir = oc_resolve_go_pkg_dir(cx, "golang.org/x/net/html", &a);
    CHECK_STR_EQ(dir, "proj/golang.org/x/net/html");
    /* Subdirectory: its own package, resolvable as a path segment. */
    dir = oc_resolve_go_pkg_dir(cx, "store/sub", &a);
    CHECK_STR_EQ(dir, "proj/store/sub");
    /* A pure-dotted spec is a LITERAL directory name in Go's mapping
     * (no Python-style dot-to-slash conversion): "store.sub" names no
     * directory. */
    CHECK(oc_resolve_go_pkg_dir(cx, "store.sub", &a) == NULL);
    /* A file is not a directory: no package named "main" exists. */
    CHECK(oc_resolve_go_pkg_dir(cx, "main", &a) == NULL);
    /* Single-target view: lexicographically-first NON-TEST direct
     * child ("store.go" < "types.go"; store_test.go never joins). */
    const char *first = oc_resolve_specifier(cx, &a, "proj/main.go", "store", OC_LANG_GO);
    CHECK_STR_EQ(first, "proj/store/store.go");

    /* Fan-out: one record -> one edge per non-test direct-child .go
     * file; the subdirectory file and the package's own test file are
     * NOT targets (Go compiles an import without test files). */
    oc_import_vec v;
    oc_imports_init(&v);
    CHECK(oc_imports_push(&v, &a, "proj/main.go", "store", OC_LANG_GO));
    CHECK(oc_imports_resolve_ex(&g, &v, &a, roots, 1) == 2);
    CHECK(has_edge(&g, "proj/main.go", "proj/store/store.go"));
    CHECK(has_edge(&g, "proj/main.go", "proj/store/types.go"));
    CHECK(!has_edge(&g, "proj/main.go", "proj/store/store_test.go"));
    CHECK(!has_edge(&g, "proj/main.go", "proj/store/sub/inner.go"));
    CHECK(!has_edge(&g, "proj/main.go", "proj/main.go"));

    /* Root order: the first command-line root that contains the package
     * wins (deterministic, documented; same discipline as the Python
     * absolute-import resolver). */
    oc_graph g2;
    oc_graph_init(&g2, &a);
    oc_graph_add_file(&g2, "projA/store/one.go");
    oc_graph_add_file(&g2, "projB/store/two.go");
    const char *roots_ab[2] = {"projA", "projB"};
    oc_resolve_ctx *cx2 = oc_resolve_ctx_new(&a, &g2, roots_ab, 2);
    CHECK(cx2 != NULL);
    if (cx2) {
        CHECK_STR_EQ(oc_resolve_go_pkg_dir(cx2, "store", &a), "projA/store");
        const char *roots_ba[2] = {"projB", "projA"};
        oc_resolve_ctx *cx3 = oc_resolve_ctx_new(&a, &g2, roots_ba, 2);
        CHECK(cx3 != NULL);
        if (cx3)
            CHECK_STR_EQ(oc_resolve_go_pkg_dir(cx3, "store", &a), "projB/store");
    }
    oc_graph_destroy(&g2);

    oc_imports_destroy(&v);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: guards (the never-guess half) ------------------------------- */
static void unit_go_guards(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_graph_add_file(&g, "proj/store/store.go");
    const char *roots[1] = {"proj"};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }
    /* Relative, absolute, escape and malformed forms never resolve --
     * Go import paths are none of these, and the resolver never
     * normalizes its way out of an analysis root. Dots are literal
     * characters under the verbatim mapping, so "sto..re" is not a
     * guard case -- it simply names no directory (checked below). */
    CHECK(oc_resolve_go_pkg_dir(cx, "", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, ".", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, "..", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, "./store", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, "/store", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, "store/../..", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, "store/.", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, "store//x", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, "store/", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, "sto..re", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, NULL, &a) == NULL);
    /* Stdlib and remote paths exist in no analyzed directory. */
    CHECK(oc_resolve_go_pkg_dir(cx, "fmt", &a) == NULL);
    CHECK(oc_resolve_go_pkg_dir(cx, "github.com/x/y", &a) == NULL);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the OC005 empty-verdict-set construction --------------------- */
static void unit_go_marking(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    /* store has an exported symbol and is imported by main; lonely is
     * never imported. Whole-package fail-open marking marks store's
     * export used; lonely's file fails OC005's precondition (in-degree
     * 0), so no verdict can arise for Go at package granularity. */
    oc_graph_add_file(&g, "proj/store/store.go");
    oc_graph_add_file(&g, "proj/main.go");
    oc_graph_add_file(&g, "proj/lonely/lonely.go");

    oc_export_vec exports;
    oc_surface_exports_init(&exports);
    CHECK(oc_surface_export_push(&exports, &a, "proj/store/store.go", "Exported", 2, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/lonely/lonely.go", "Lonely", 2, 0));
    oc_bind_vec binds;
    oc_surface_binds_init(&binds);
    CHECK(oc_surface_bind_push(&binds, &a, "proj/main.go", "store", "st", OC_LANG_GO));
    oc_hint_vec hints;
    oc_surface_hints_init(&hints);

    const char *roots[1] = {"proj"};
    oc_status st;
    oc_status_clear(&st);
    oc_surface *s = oc_surface_build(&a, &g, &binds, &exports, &hints, roots, 1, NULL, &st);
    CHECK(s != NULL);
    if (s) {
        CHECK(s->binds_resolved == 1);
        CHECK(exports.n == 2);
        CHECK(exports.items[0].used == 1); /* package import = fail-open */
        CHECK(exports.items[1].used == 0); /* never imported: no marking */
    }

    oc_surface_hints_destroy(&hints);
    oc_surface_binds_destroy(&binds);
    oc_surface_exports_destroy(&exports);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- fixtures through the oc binary ------------------------------------ */
static int files_equal(const char *p1, const char *p2) {
    char *d1 = NULL, *d2 = NULL;
    size_t l1 = 0, l2 = 0;
    if (oc_read_file(p1, &d1, &l1, NULL) != OC_OK)
        return 0;
    if (oc_read_file(p2, &d2, &l2, NULL) != OC_OK) {
        free(d1);
        return 0;
    }
    int eq = l1 == l2 && (l1 == 0 || memcmp(d1, d2, l1) == 0);
    free(d1);
    free(d2);
    return eq;
}

static void fixture_checks(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char fix[512], packs[512], out1[256], out2[256], cmd[2048];
    snprintf(fix, sizeof(fix), "%s/tests/fixtures/go_resolver", root);
    snprintf(packs, sizeof(packs), "%s/packs", root);
    snprintf(out1, sizeof(out1), "/tmp/oc_s10_out1.json");
    snprintf(out2, sizeof(out2), "/tmp/oc_s10_out2.json");

    /* 1) Graph: exactly six imports edges -- the store fan-out (3), the
     * aliased util import (1), the a/b cycle (2). The decoy module
     * prefix ("mycorp/store"), stdlib ("fmt", "testing") and the remote
     * path stay unresolved: go.mod is never read, never guessed. */
    snprintf(cmd, sizeof(cmd), "%s graph %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out1);
    CHECK(run_cmd(cmd) == 0);
    oc_arena arena;
    oc_arena_init(&arena);
    char *data = NULL;
    size_t len = 0;
    oc_json *graph = NULL;
    if (oc_read_file(out1, &data, &len, NULL) == OC_OK) {
        graph = oc_json_parse(&arena, data, len, NULL);
        free(data);
    }
    CHECK(graph != NULL);
    if (graph) {
        /* id -> path for file nodes (edges reference node ids). */
        const oc_json *nodes = oc_json_get(graph, "nodes");
        char *path_by_id[256] = {0};
        size_t file_nodes = 0;
        for (size_t i = 0; nodes && i < oc_json_size(nodes); i++) {
            const oc_json *n = nodes->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(n, "kind"), ""), "file") != 0)
                continue;
            file_nodes++;
            size_t id = (size_t)oc_json_as_num(oc_json_get(n, "id"), -1);
            if (id < 256)
                path_by_id[id] = (char *)oc_json_as_str(oc_json_get(n, "path"), "");
        }
        CHECK(file_nodes == 10);
        const oc_json *edges = oc_json_get(graph, "edges");
        size_t imports_edges = 0;
        int saw_main_store = 0, saw_main_types = 0, saw_main_test = 0, saw_main_util = 0,
            saw_main_dotted = 0, saw_ab = 0, saw_ba = 0, stray = 0;
        for (size_t i = 0; edges && i < oc_json_size(edges); i++) {
            const oc_json *e = edges->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(e, "kind"), ""), "imports") != 0)
                continue;
            imports_edges++;
            size_t f = (size_t)oc_json_as_num(oc_json_get(e, "from"), -1);
            size_t t = (size_t)oc_json_as_num(oc_json_get(e, "to"), -1);
            const char *fp = f < 256 ? path_by_id[f] : "";
            const char *tp = t < 256 ? path_by_id[t] : "";
            if (strstr(fp, "cmd/main.go") && strstr(tp, "store/store.go"))
                saw_main_store = 1;
            if (strstr(fp, "cmd/main.go") && strstr(tp, "store/types.go"))
                saw_main_types = 1;
            if (strstr(fp, "cmd/main.go") && strstr(tp, "store/store_test.go"))
                saw_main_test = 1;
            if (strstr(fp, "cmd/main.go") && strstr(tp, "util/util.go"))
                saw_main_util = 1;
            if (strstr(fp, "cmd/main.go") && strstr(tp, "example.org/app/store/store.go"))
                saw_main_dotted = 1;
            if (strstr(fp, "/a/a.go") && strstr(tp, "/b/b.go"))
                saw_ab = 1;
            if (strstr(fp, "/b/b.go") && strstr(tp, "/a/a.go"))
                saw_ba = 1;
            if (!strstr(fp, "cmd/main.go") && !strstr(fp, "/a/a.go") && !strstr(fp, "/b/b.go"))
                stray = 1; /* no edge from anywhere else (decoy, stdlib, remote) */
        }
        CHECK(imports_edges == 6);
        CHECK(saw_main_store);
        CHECK(saw_main_types);
        CHECK(!saw_main_test); /* test files never join an import (docs/LANGUAGES.md §Go.5) */
        CHECK(saw_main_util);
        CHECK(saw_main_dotted);
        CHECK(saw_ab);
        CHECK(saw_ba);
        CHECK(!stray);
    }

    /* 2) Check: OC003 fires once on the a/b cycle; OC006 fires exactly
     * on the lonely package (serve.go and the dotted-host main.go are
     * claimed by the parsed package_clause, store_test.go by the
     * *_test.go convention); OC005 is silent by construction
     * (package-wide fail-open marking); nothing else fires here. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out1);
    CHECK(run_cmd(cmd) == 1); /* findings present (OC003 warn), no parse errors */
    oc_json *report = NULL;
    if (oc_read_file(out1, &data, &len, NULL) == OC_OK) {
        report = oc_json_parse(&arena, data, len, NULL);
        free(data);
    }
    CHECK(report != NULL);
    if (report) {
        const oc_json *findings = oc_json_get(report, "findings");
        CHECK(findings != NULL);
        int oc003 = 0, oc005 = 0, oc006 = 0, oc006_lonely = 0, oc006_serve = 0, oc010 = 0,
            other = 0;
        for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
            const oc_json *f = findings->items[i];
            const char *rule = oc_json_as_str(oc_json_get(f, "rule"), "");
            const char *file = oc_json_as_str(oc_json_get(f, "file"), "");
            if (strcmp(rule, "OC003") == 0) {
                oc003++;
                CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(f, "message"), ""),
                                   "Import cycle among 2 files");
            } else if (strcmp(rule, "OC005") == 0) {
                oc005++;
            } else if (strcmp(rule, "OC006") == 0) {
                oc006++;
                if (strstr(file, "lonely/lonely.go"))
                    oc006_lonely = 1;
                if (strstr(file, "cmd/serve.go"))
                    oc006_serve = 1;
            } else if (strcmp(rule, "OC010") == 0) {
                /* SPRINT-11: the supply tier legitimately reads the decoy
                 * go.mod as a manifest (OC010 licence check on the module
                 * root; the fixtures carry no licence file). The RESOLVER
                 * must still ignore it -- the edge assertions below pin
                 * that, unchanged. */
                oc010++;
                CHECK(strstr(file, "go.mod") != NULL);
            } else {
                other++;
            }
        }
        CHECK(oc003 == 1);
        CHECK(oc005 == 0); /* the asserted empty verdict set (docs/LANGUAGES.md §Go.5) */
        CHECK(oc006 == 1);
        CHECK(oc006_lonely);
        CHECK(!oc006_serve); /* package_clause claiming proved by absence */
        CHECK(oc010 == 1);   /* decoy go.mod parsed by the supply tier, SPRINT-11 */
        CHECK(other == 0);
        const oc_json *meta = oc_json_get(report, "meta");
        CHECK(oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1) == 10);
        CHECK(oc_json_as_num(oc_json_get(meta, "files_with_parse_errors"), -1) == 0);
    }

    /* 3) Determinism: byte-identical rerun. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out2);
    CHECK(run_cmd(cmd) == 1);
    CHECK(files_equal(out1, out2));

    oc_arena_destroy(&arena);
}

int main(void) {
    unit_go_resolver();
    unit_go_guards();
    unit_go_marking();
    fixture_checks();
    TEST_SUMMARY();
}
