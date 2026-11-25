/* Sprint 1 (WBS 2.x) tests: TypeScript/TSX packs, import edges +
 * relative resolver, OC003 import cycles, OC004 complexity (CRAP-lite
 * scaffold). E2E against the real oc binary; resolver unit checks run
 * in-process against a synthetic graph. */
#include "oc/arena.h"
#include "oc/common.h"
#include "oc/graph.h"
#include "oc/imports.h"
#include "oc/json.h"
#include "oc/parser.h"
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
    if (WIFEXITED(rc))
        return WEXITSTATUS(rc);
    return -1;
}

static oc_json *load_json(oc_arena *a, const char *path) {
    char *data = NULL;
    size_t len = 0;
    if (oc_read_file(path, &data, &len, NULL) != OC_OK)
        return NULL;
    oc_json *v = oc_json_parse(a, data, len, NULL);
    free(data);
    return v;
}

static size_t count_edges(const oc_json *graph, const char *kind) {
    const oc_json *edges = oc_json_get(graph, "edges");
    size_t n = oc_json_size(edges), hits = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(oc_json_as_str(oc_json_get(edges->items[i], "kind"), ""), kind) == 0)
            hits++;
    }
    return hits;
}

/* Count findings with a given rule id in a check report. */
static size_t count_findings(const oc_json *report, const char *rule) {
    const oc_json *findings = oc_json_get(report, "findings");
    size_t n = oc_json_size(findings), hits = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(oc_json_as_str(oc_json_get(findings->items[i], "rule"), ""), rule) == 0)
            hits++;
    }
    return hits;
}

/* --- resolver unit checks against a synthetic graph ------------------- */
static void unit_resolver(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    oc_graph_add_file(&g, "proj/a.js");
    oc_graph_add_file(&g, "proj/b.js");
    oc_graph_add_file(&g, "proj/sub/c.js");
    oc_graph_add_file(&g, "proj/pkg/m.py");
    oc_graph_add_file(&g, "proj/pkg/sub/c.py");
    oc_graph_add_file(&g, "proj/pkg/__init__.py");

    oc_import_vec v;
    oc_imports_init(&v);
    CHECK(oc_imports_push(&v, &a, "proj/a.js", "./b", OC_LANG_JS));
    CHECK(oc_imports_push(&v, &a, "proj/a.js", "./sub/c", OC_LANG_JS));
    CHECK(oc_imports_push(&v, &a, "proj/a.js", "lodash", OC_LANG_JS));    /* bare: skip */
    CHECK(oc_imports_push(&v, &a, "proj/a.js", "/abs/path", OC_LANG_JS)); /* abs: skip */
    CHECK(oc_imports_push(&v, &a, "proj/a.js", "./missing", OC_LANG_JS)); /* no such file */
    CHECK(oc_imports_push(&v, &a, "proj/pkg/m.py", ".sub.c", OC_LANG_PY));
    CHECK(oc_imports_push(&v, &a, "proj/pkg/m.py", "..pkg", OC_LANG_PY));   /* __init__.py */
    CHECK(oc_imports_push(&v, &a, "proj/pkg/m.py", "os.path", OC_LANG_PY)); /* abs py: skip */

    long added = oc_imports_resolve(&g, &v, &a);
    CHECK(added == 4); /* ./b, ./sub/c, .sub.c, ..pkg -> __init__.py */
    CHECK(oc_imports_push(&v, &a, "proj/a.js", "./b", OC_LANG_JS));
    added = oc_imports_resolve(&g, &v, &a);
    CHECK(added == 5); /* re-resolves the whole vec: 4 + 1 duplicate, counted honestly */
    /* Edge from proj/a.js -> proj/b.js exists. */
    oc_node_id from = oc_graph_find_file(&g, "proj/a.js");
    oc_node_id to = oc_graph_find_file(&g, "proj/b.js");
    CHECK(from != OC_NODE_INVALID && to != OC_NODE_INVALID);
    int saw_edge = 0;
    for (size_t i = 0; i < g.n_edges; i++) {
        if (g.edges[i].kind == OC_EDGE_IMPORTS && g.edges[i].from == from && g.edges[i].to == to)
            saw_edge = 1;
    }
    CHECK(saw_edge);
    /* Python package edge m.py -> pkg/__init__.py exists. */
    oc_node_id init_id = oc_graph_find_file(&g, "proj/pkg/__init__.py");
    CHECK(init_id != OC_NODE_INVALID);
    saw_edge = 0;
    for (size_t i = 0; i < g.n_edges; i++) {
        if (g.edges[i].kind == OC_EDGE_IMPORTS && g.edges[i].to == init_id)
            saw_edge = 1;
    }
    CHECK(saw_edge);

    oc_imports_destroy(&v);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

int main(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char fixtures[512], packs[512];
    snprintf(fixtures, sizeof(fixtures), "%s/tests/fixtures", root);
    snprintf(packs, sizeof(packs), "%s/packs", root);

    char cmd[4096];
    oc_arena arena;
    oc_arena_init(&arena);

    unit_resolver();

    /* 1) TypeScript pack: long function + TODO found; dep.ts clean. */
    snprintf(cmd, sizeof(cmd), "%s check %s/ts --packs-dir %s > /tmp/oc_s1_ts.json 2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    oc_json *ts = load_json(&arena, "/tmp/oc_s1_ts.json");
    CHECK(ts != NULL);
    CHECK(count_findings(ts, "OC001") == 1);
    CHECK(count_findings(ts, "OC002") == 1);
    CHECK(oc_json_as_num(oc_json_get(oc_json_get(ts, "meta"), "files_analyzed"), -1) == 2);
    CHECK(oc_json_as_num(oc_json_get(oc_json_get(ts, "meta"), "files_with_parse_errors"), -1) == 0);

    /* 2) TSX pack: parses clean (grammar handles JSX). */
    snprintf(cmd, sizeof(cmd), "%s check %s/tsx --packs-dir %s > /tmp/oc_s1_tsx.json 2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *tsx = load_json(&arena, "/tmp/oc_s1_tsx.json");
    CHECK(tsx != NULL);
    CHECK(oc_json_as_num(oc_json_get(oc_json_get(tsx, "meta"), "files_analyzed"), -1) == 2);
    CHECK(count_findings(tsx, "OC001") == 0);

    /* 3) Import edges: relative requires resolve, bare specifiers do not. */
    snprintf(cmd, sizeof(cmd),
             "%s graph %s/js/imports_fixture.js %s/js/util.js %s/js/nested/extra.js "
             "--packs-dir %s > /tmp/oc_s1_edges.json 2>/dev/null",
             bin, fixtures, fixtures, fixtures, packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *edges = load_json(&arena, "/tmp/oc_s1_edges.json");
    CHECK(edges != NULL);
    CHECK(count_edges(edges, "imports") == 2);
    CHECK(count_edges(edges, "contains") >= 3);

    /* 4) TS import edge: ./dep resolves through the typescript grammar. */
    snprintf(cmd, sizeof(cmd), "%s graph %s/ts --packs-dir %s > /tmp/oc_s1_tsedge.json 2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *tsedge = load_json(&arena, "/tmp/oc_s1_tsedge.json");
    CHECK(tsedge != NULL);
    CHECK(count_edges(tsedge, "imports") == 1);

    /* 5) OC003 on the JS cycle a -> b -> c -> a: exactly one finding. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/js/cycles --packs-dir %s > /tmp/oc_s1_cycle.json 2>/dev/null", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    oc_json *cycle = load_json(&arena, "/tmp/oc_s1_cycle.json");
    CHECK(cycle != NULL);
    size_t n_cycle = count_findings(cycle, "OC003");
    CHECK(n_cycle == 1);
    if (n_cycle == 1)
        CHECK_STR_CONTAINS(
            oc_json_as_str(oc_json_get(oc_json_get(cycle, "findings")->items[0], "message"), ""),
            "Import cycle among 3 files");

    /* 6) OC003 on the Python relative-import cycle: exactly one finding. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/py/cycles --packs-dir %s > /tmp/oc_s1_pycycle.json 2>/dev/null", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    oc_json *pycycle = load_json(&arena, "/tmp/oc_s1_pycycle.json");
    CHECK(pycycle != NULL);
    CHECK(count_findings(pycycle, "OC003") == 1);

    /* 7) OC004 + graph complexity on the tangled Python function. */
    snprintf(cmd, sizeof(cmd),
             "%s graph %s/py/complex.py --packs-dir %s > /tmp/oc_s1_cx.json "
             "2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *cx = load_json(&arena, "/tmp/oc_s1_cx.json");
    CHECK(cx != NULL);
    const oc_json *nodes = oc_json_get(cx, "nodes");
    long tangled_comp = -1;
    for (size_t i = 0; i < oc_json_size(nodes); i++) {
        const oc_json *n = nodes->items[i];
        if (strcmp(oc_json_as_str(oc_json_get(n, "name"), ""), "tangled") == 0)
            tangled_comp = (long)oc_json_as_num(oc_json_get(n, "complexity"), -1);
    }
    CHECK(tangled_comp >= 20);
    snprintf(cmd, sizeof(cmd),
             "%s check %s/py/complex.py --packs-dir %s > /tmp/oc_s1_cxf.json "
             "2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    oc_json *cxf = load_json(&arena, "/tmp/oc_s1_cxf.json");
    CHECK(cxf != NULL);
    size_t n_cx = count_findings(cxf, "OC004");
    CHECK(n_cx == 1);
    if (n_cx == 1)
        CHECK_STR_CONTAINS(
            oc_json_as_str(oc_json_get(oc_json_get(cxf, "findings")->items[0], "message"), ""),
            "CRAP proxy");

    /* 8) Config override: raising the threshold silences OC004. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/py/complex.py --packs-dir %s --config OC004.max_complexity=50 "
             "> /tmp/oc_s1_cxcfg.json 2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 0);

    /* 9) Packs list grew to the seven registered packs (SPRINT-13: +csharp). */
    snprintf(cmd, sizeof(cmd), "%s packs --packs-dir %s > /tmp/oc_s1_packs.json", bin, packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *pl = load_json(&arena, "/tmp/oc_s1_packs.json");
    CHECK(pl != NULL);
    CHECK(oc_json_size(pl) == 15); /* sprint 9: +go; sprint 11: +java; sprint 13: +csharp; sprint
                                     15: +rust; sprint 16: +ruby; sprint 19: +php; sprint 20: +c;
                                     sprint 21: +cpp; sprint 22: +kotlin; sprint 23: +swift;
                                     sprint 24: +scala */

    /* 10) Determinism: TS cycle report is byte-identical on rerun. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/js/cycles --packs-dir %s > /tmp/oc_s1_cycle2.json "
             "2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    char *d1 = NULL, *d2 = NULL;
    size_t l1 = 0, l2 = 0;
    CHECK(oc_read_file("/tmp/oc_s1_cycle.json", &d1, &l1, NULL) == OC_OK);
    CHECK(oc_read_file("/tmp/oc_s1_cycle2.json", &d2, &l2, NULL) == OC_OK);
    CHECK(l1 == l2 && (l1 == 0 || memcmp(d1, d2, l1) == 0));
    free(d1);
    free(d2);

    oc_arena_destroy(&arena);
    TEST_SUMMARY();
}
