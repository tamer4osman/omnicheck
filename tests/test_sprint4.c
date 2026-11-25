/* Sprint 4 (M3 Beta scope): resolver v3 (Python absolute intra-repo
 * imports), CI gate templates (WBS 3.6). LSP/MCP servers have their own
 * test binaries. */
#include "oc/common.h"
#include "oc/imports.h"
#include "oc/json.h"
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

/* --- resolver v3 unit checks against a synthetic graph ---------------- */
static void unit_resolver_v3(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    /* Repo-root package. */
    oc_graph_add_file(&g, "proj/mypkg/__init__.py");
    oc_graph_add_file(&g, "proj/mypkg/core.py");
    oc_graph_add_file(&g, "proj/mypkg/cycle_a.py");
    /* Src layout: package root is proj/src, not the analysis root. */
    oc_graph_add_file(&g, "proj/src/mypkg/__init__.py");
    oc_graph_add_file(&g, "proj/src/mypkg/mod.py");
    oc_graph_add_file(&g, "proj/src/other.py");
    /* Package-less scripts dir. */
    oc_graph_add_file(&g, "proj/scripts/main.py");
    oc_graph_add_file(&g, "proj/scripts/util.py");

    const char *roots[1] = {"proj"};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }

    /* Package-root probing: deep.py lives in a package, so the root is
     * proj (nearest ancestor without __init__.py). */
    CHECK_STR_EQ(oc_resolve_specifier(cx, &a, "proj/mypkg/deep.py", "mypkg.core", OC_LANG_PY),
                 "proj/mypkg/core.py");
    CHECK_STR_EQ(oc_resolve_specifier(cx, &a, "proj/mypkg/deep.py", "mypkg.cycle_a", OC_LANG_PY),
                 "proj/mypkg/cycle_a.py");
    CHECK_STR_EQ(oc_resolve_specifier(cx, &a, "proj/mypkg/deep.py", "mypkg", OC_LANG_PY),
                 "proj/mypkg/__init__.py");
    /* Stdlib / site-packages names never match an intra-repo file. */
    CHECK(oc_resolve_specifier(cx, &a, "proj/mypkg/deep.py", "os.path", OC_LANG_PY) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/mypkg/deep.py", "json", OC_LANG_PY) == NULL);

    /* Src layout: the importing file's package root is proj/src, which
     * shadows the analysis root on the first probe. */
    CHECK_STR_EQ(oc_resolve_specifier(cx, &a, "proj/src/mypkg/mod.py", "mypkg", OC_LANG_PY),
                 "proj/src/mypkg/__init__.py");
    /* Package-root miss falls back to the analysis roots in order. */
    CHECK_STR_EQ(oc_resolve_specifier(cx, &a, "proj/src/other.py", "mypkg.core", OC_LANG_PY),
                 "proj/mypkg/core.py");
    /* Package-less scripts dir: the dir itself is the package root. */
    CHECK_STR_EQ(oc_resolve_specifier(cx, &a, "proj/scripts/main.py", "util", OC_LANG_PY),
                 "proj/scripts/util.py");
    /* Relative imports keep the v1 contract. */
    CHECK_STR_EQ(oc_resolve_specifier(cx, &a, "proj/mypkg/deep.py", ".core", OC_LANG_PY),
                 "proj/mypkg/core.py");
    /* Unknown absolute names stay unresolved -- never guessed. */
    CHECK(oc_resolve_specifier(cx, &a, "proj/scripts/main.py", "nope.missing", OC_LANG_PY) == NULL);

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- fixture checks through the oc binary ------------------------------ */
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

/* Counts IMPORTS edges from file `from_path` and reports whether any
 * edge touches a path containing `needle`. */
static void edge_stats(const oc_json *graph, const char *from_path, const char *needle,
                       long *out_edges_from, int *needle_touched) {
    *out_edges_from = 0;
    *needle_touched = 0;
    const oc_json *nodes = oc_json_get(graph, "nodes");
    const oc_json *edges = oc_json_get(graph, "edges");
    if (!nodes || !edges)
        return;
    /* file-node ids by path */
    long from_id = -1;
    for (size_t i = 0; i < oc_json_size(nodes); i++) {
        const oc_json *n = nodes->items[i];
        if (strcmp(oc_json_as_str(oc_json_get(n, "kind"), ""), "file") != 0)
            continue;
        const char *p = oc_json_as_str(oc_json_get(n, "path"), "");
        if (from_id < 0 && strcmp(p, from_path) == 0)
            from_id = (long)i;
        if (needle[0] && strstr(p, needle))
            *needle_touched = 1;
    }
    for (size_t i = 0; i < oc_json_size(edges); i++) {
        const oc_json *e = edges->items[i];
        if (strcmp(oc_json_as_str(oc_json_get(e, "kind"), ""), "imports") != 0)
            continue;
        long from = (long)oc_json_as_num(oc_json_get(e, "from"), -1);
        long to = (long)oc_json_as_num(oc_json_get(e, "to"), -1);
        const oc_json *n = nodes->items[to];
        const char *tp = n ? oc_json_as_str(oc_json_get(n, "path"), "") : "";
        if (needle[0] && (from == from_id || to == from_id) && strstr(tp, needle))
            *needle_touched = 1;
        if (from_id >= 0 && from == from_id)
            (*out_edges_from)++;
    }
}

static void fixture_checks(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char fix[512], packs[512], out1[256], out2[256], cmd[2048];
    snprintf(fix, sizeof(fix), "%s/tests/fixtures_sprint4/pyabs", root);
    snprintf(packs, sizeof(packs), "%s/packs", root);
    snprintf(out1, sizeof(out1), "/tmp/oc_s4_out1.json");
    snprintf(out2, sizeof(out2), "/tmp/oc_s4_out2.json");
    /* Graph node paths mirror the analysis root as given: the spawn below
     * passes an absolute root, so paths are absolute too. */
    char fp_top[600], fp_deep[600], fp_mod[600], fp_testx[600], fp_main[600], fp_shadow[600];
    snprintf(fp_top, sizeof(fp_top), "%s/tests/fixtures_sprint4/pyabs/top.py", root);
    snprintf(fp_deep, sizeof(fp_deep), "%s/tests/fixtures_sprint4/pyabs/mypkg/sub/deep.py", root);
    snprintf(fp_mod, sizeof(fp_mod), "%s/tests/fixtures_sprint4/pyabs/src/mypkg/mod.py", root);
    snprintf(fp_testx, sizeof(fp_testx), "%s/tests/fixtures_sprint4/pyabs/src/tests/test_x.py",
             root);
    snprintf(fp_main, sizeof(fp_main), "%s/tests/fixtures_sprint4/pyabs/loose/main.py", root);
    snprintf(fp_shadow, sizeof(fp_shadow), "%s/tests/fixtures_sprint4/pyabs/mypkg/shadow.py", root);

    /* 1) Graph: absolute imports resolve to the expected targets. */
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
        const oc_json *nodes = oc_json_get(graph, "nodes");
        CHECK(nodes != NULL);
        long edges = 0;
        int needle_hits = 0;
        int touched = 0;
        edge_stats(graph, fp_top, "", &edges, &touched);
        /* top.py: import mypkg.core -> 1 edge */
        CHECK(edges == 1);
        edge_stats(graph, fp_deep, "", &edges, &touched);
        /* deep.py: import mypkg.sub + from mypkg.core import helper -> 2 edges */
        CHECK(edges == 2);
        edge_stats(graph, fp_mod, "", &edges, &touched);
        CHECK(edges == 1); /* from mypkg.other import thing (src layout) */
        edge_stats(graph, fp_testx, "", &edges, &touched);
        CHECK(edges == 1); /* import top via analysis-root fallback */
        edge_stats(graph, fp_main, "", &edges, &touched);
        CHECK(edges == 1); /* sibling absolute import in a package-less dir */
        edge_stats(graph, fp_shadow, "json", &edges, &needle_hits);
        /* shadow.py's stdlib import stays unresolved: 0 edges from it. */
        CHECK(edges == 0);
        CHECK(needle_hits == 0);

        /* Exact targets: package-root walk and root fallback. */
        const oc_json *nodes_arr = oc_json_get(graph, "nodes");
        const oc_json *edges_arr = oc_json_get(graph, "edges");
        const char *from_paths[] = {fp_testx, fp_main, fp_mod};
        const char *to_suffixes[] = {"/pyabs/top.py", "/pyabs/loose/helper.py",
                                     "/pyabs/src/mypkg/other.py"};
        for (size_t k = 0; k < 3; k++) {
            int saw = 0;
            long fid = -1;
            for (size_t i = 0; i < oc_json_size(nodes_arr); i++) {
                const oc_json *n = nodes_arr->items[i];
                if (strcmp(oc_json_as_str(oc_json_get(n, "kind"), ""), "file") == 0 &&
                    strcmp(oc_json_as_str(oc_json_get(n, "path"), ""), from_paths[k]) == 0) {
                    fid = (long)i;
                    break;
                }
            }
            CHECK(fid >= 0);
            for (size_t i = 0; i < oc_json_size(edges_arr); i++) {
                const oc_json *e = edges_arr->items[i];
                long from = (long)oc_json_as_num(oc_json_get(e, "from"), -1);
                long to = (long)oc_json_as_num(oc_json_get(e, "to"), -1);
                if (from != fid ||
                    strcmp(oc_json_as_str(oc_json_get(e, "kind"), ""), "imports") != 0)
                    continue;
                const oc_json *tn = nodes_arr->items[to];
                const char *tp = tn ? oc_json_as_str(oc_json_get(tn, "path"), "") : "";
                if (strstr(tp, to_suffixes[k]))
                    saw = 1;
            }
            CHECK(saw);
        }
    }

    /* 2) Check: the absolute-import cycle fires OC003; referenced modules
     * are not flagged unreferenced (the resolver v3 headline). */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out1);
    CHECK(run_cmd(cmd) == 1); /* OC003 is error severity */
    oc_json *report = NULL;
    if (oc_read_file(out1, &data, &len, NULL) == OC_OK) {
        report = oc_json_parse(&arena, data, len, NULL);
        free(data);
    }
    CHECK(report != NULL);
    if (report) {
        const oc_json *findings = oc_json_get(report, "findings");
        CHECK(findings != NULL);
        int oc003 = 0, oc006_core = 0, oc006_other = 0, oc006_helper = 0, oc006_top = 0;
        for (size_t i = 0; i < oc_json_size(findings); i++) {
            const oc_json *f = findings->items[i];
            const char *rule = oc_json_as_str(oc_json_get(f, "rule"), "");
            const char *file = oc_json_as_str(oc_json_get(f, "file"), "");
            if (strcmp(rule, "OC003") == 0 && strstr(file, "cycle_a.py"))
                oc003 = 1;
            if (strcmp(rule, "OC006") == 0) {
                if (strstr(file, "mypkg/core.py"))
                    oc006_core = 1;
                if (strstr(file, "src/mypkg/other.py"))
                    oc006_other = 1;
                if (strstr(file, "loose/helper.py"))
                    oc006_helper = 1;
                if (strstr(file, "/pyabs/top.py"))
                    oc006_top = 1;
            }
        }
        CHECK(oc003);         /* absolute-import cycle detected */
        CHECK(!oc006_core);   /* referenced via absolute imports: not unreferenced */
        CHECK(!oc006_other);  /* referenced via the src package root */
        CHECK(!oc006_helper); /* referenced via the script-dir root */
        CHECK(!oc006_top);    /* referenced via the analysis-root fallback */
        CHECK(oc_json_as_num(oc_json_get(oc_json_get(report, "meta"), "files_with_parse_errors"),
                             -1) == 0);
    }

    /* 3) Determinism: byte-identical rerun. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out2);
    CHECK(run_cmd(cmd) == 1);
    CHECK(files_equal(out1, out2));

    oc_arena_destroy(&arena);
}

/* --- CI gate templates (WBS 3.6): structural checks -------------------- */
static void template_checks(void) {
    const char *root = OC_REPO_ROOT;
    char p[512];
    FILE *f;

    snprintf(p, sizeof(p), "%s/templates/github-action/action.yml", root);
    f = fopen(p, "rb");
    CHECK(f != NULL);
    if (f) {
        char buf[16384];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        CHECK_STR_CONTAINS(buf, "name: 'OmniCheck gate'");
        CHECK_STR_CONTAINS(buf, "using: 'composite'");
        CHECK_STR_CONTAINS(buf, "inputs.paths");
        CHECK_STR_CONTAINS(buf, "inputs.fail-on");
        CHECK_STR_CONTAINS(buf, "inputs.packs-dir");
        CHECK_STR_CONTAINS(buf, "inputs.config");
        CHECK_STR_CONTAINS(buf, "inputs.coverage");
        CHECK_STR_CONTAINS(buf, "inputs.triage");
        CHECK_STR_CONTAINS(buf, "inputs.oc-bin");
        CHECK_STR_CONTAINS(buf, "check $OC_PATHS");
        CHECK_STR_CONTAINS(buf, "scripts/vendor.sh");
        CHECK_STR_CONTAINS(buf, "cmake --preset default");
    }

    snprintf(p, sizeof(p), "%s/templates/ci/omnicheck-gate.yml", root);
    f = fopen(p, "rb");
    CHECK(f != NULL);
    if (f) {
        char buf[16384];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        CHECK_STR_CONTAINS(buf, "omnicheck-gate");
        CHECK_STR_CONTAINS(buf, "templates/github-action");
        CHECK_STR_CONTAINS(buf, "fail-on: 'warn'");
    }

    snprintf(p, sizeof(p), "%s/templates/ci/README.md", root);
    f = fopen(p, "rb");
    CHECK(f != NULL);
    if (f) {
        char buf[8192];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        CHECK_STR_CONTAINS(buf, "oc-bin");
        CHECK_STR_CONTAINS(buf, "exit");
    }
}

int main(void) {
    unit_resolver_v3();
    fixture_checks();
    template_checks();
    TEST_SUMMARY();
}
