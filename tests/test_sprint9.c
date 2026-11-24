/* Sprint 9 (Track D wave 1, spike scope): Go language adoption --
 * grammar + descriptor + pack + fixtures + acceptance gate.
 *
 * SPRINT-10 shipped resolver v1-go (docs/LANGUAGES.md §Go.5/§Go.6),
 * superseding the spike posture this file originally locked: intra-repo
 * dotted paths now resolve to whole-package fan-out edges, OC003/OC005
 * are enabled at pack level, and package_clause claiming landed. The
 * assertions here were updated to the contract that NOW holds; the
 * resolver's positive semantics are locked in detail by test_sprint10.c.
 * What stays locked here is the permanent never-guess half of the
 * contract: stdlib, remote and escape-form specifiers never resolve. */
#include "oc/common.h"
#include "oc/imports.h"
#include "oc/json.h"
#include "oc/pack.h"
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

/* --- unit: resolver v1-go never-guess invariants ----------------------- */
static void unit_go_posture(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    /* An intra-repo Go module layout. Since SPRINT-10 a dotted path
     * resolves when the package directory it names exists under an
     * analysis root (docs/LANGUAGES.md §Go.5) -- the positive cases are
     * test_sprint10.c's. This unit locks the permanent never-guess
     * invariants: stdlib, remote modules, escape forms and everything
     * outside the analyzed set stay unresolved. go.mod is never read. */
    oc_graph_add_file(&g, "proj/util/util.go");
    oc_graph_add_file(&g, "proj/util/dup_a.go");
    oc_graph_add_file(&g, "proj/main.go");

    const char *roots[1] = {"proj"};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }

    /* Stdlib: never resolves. */
    CHECK(oc_resolve_specifier(cx, &a, "proj/main.go", "fmt", OC_LANG_GO) == NULL);
    /* Remote module: never resolves. */
    CHECK(oc_resolve_specifier(cx, &a, "proj/main.go", "github.com/x/y", OC_LANG_GO) == NULL);
    /* Module-path-looking specifier whose directory does NOT exist in
     * the analyzed set: unresolved ("example.com/app" has no dir here). */
    CHECK(oc_resolve_specifier(cx, &a, "proj/main.go", "example.com/app/internal/util",
                               OC_LANG_GO) == NULL);
    /* Relative / absolute / escape forms: unresolved by the guards --
     * Go import paths are never relative, and the resolver never
     * normalizes its way out of an analysis root. */
    CHECK(oc_resolve_specifier(cx, &a, "proj/main.go", "./util", OC_LANG_GO) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/main.go", "/util", OC_LANG_GO) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/main.go", "util/../main", OC_LANG_GO) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/main.go", "util..x", OC_LANG_GO) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/main.go", "", OC_LANG_GO) == NULL);
    /* The positive case one level up: "util" names an existing package
     * directory, so it resolves to the package's first file
     * (lexicographic, deterministic). Full package semantics in
     * test_sprint10.c. */
    const char *hit = oc_resolve_specifier(cx, &a, "proj/main.go", "util", OC_LANG_GO);
    CHECK(hit != NULL);
    CHECK_STR_EQ(hit, "proj/util/dup_a.go");

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the go pack is pure data with the contracted shape ---------- */
static void unit_go_pack(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_pack_set ps;
    oc_packs_init(&ps, &a);

    char packs[512];
    snprintf(packs, sizeof(packs), "%s/packs", OC_REPO_ROOT);
    CHECK(oc_packs_load_dir(&ps, packs, NULL) == OC_OK);

    const oc_pack *go = oc_packs_find(&ps, "go");
    CHECK(go != NULL);
    if (go) {
        CHECK_STR_EQ(go->grammar, "go");
        CHECK_STR_EQ(go->display_name, "Go");
        CHECK(go->n_extensions == 1);
        CHECK_STR_EQ(go->extensions[0], ".go");
        CHECK(go->n_entry_points == 1);
        CHECK_STR_EQ(go->entry_points[0], "main.go");
        /* Exactly the eleven passes the contract enables -- no more, no
         * fewer (docs/LANGUAGES.md §Go.7; SPRINT-10 added OC003/OC005,
         * SPRINT-11 completed the set with OC007/OC009/OC010 -- the
         * coverage adapter and the go.mod manifest tier). Supersedes the
         * spike-era 8-pass shape recorded in git history. */
        CHECK(go->n_passes == 11);
        CHECK(oc_pack_has_pass(go, "OC001"));
        CHECK(oc_pack_has_pass(go, "OC002"));
        CHECK(oc_pack_has_pass(go, "OC003")); /* edges exist since resolver v1-go */
        CHECK(oc_pack_has_pass(go, "OC004"));
        CHECK(oc_pack_has_pass(go, "OC005")); /* bindings exist since resolver v1-go */
        CHECK(oc_pack_has_pass(go, "OC006"));
        CHECK(oc_pack_has_pass(go, "OC007")); /* go coverprofile adapter, SPRINT-11 */
        CHECK(oc_pack_has_pass(go, "OC008"));
        CHECK(oc_pack_has_pass(go, "OC009")); /* go.mod + OSV ecosystem Go, SPRINT-11 */
        CHECK(oc_pack_has_pass(go, "OC010")); /* go.mod module-root licence, SPRINT-11 */
        CHECK(oc_pack_has_pass(go, "OC011"));
    }

    oc_packs_destroy(&ps);
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
    snprintf(fix, sizeof(fix), "%s/tests/fixtures/go", root);
    snprintf(packs, sizeof(packs), "%s/packs", root);
    snprintf(out1, sizeof(out1), "/tmp/oc_s9_out1.json");
    snprintf(out2, sizeof(out2), "/tmp/oc_s9_out2.json");

    /* 1) Graph: zero imports edges on THIS corpus -- no specifier here
     * names a package directory inside the analyzed set (stdlib, a
     * module path whose directory does not exist, stdlib "strings").
     * Since SPRINT-10 that is a statement about the specifiers, not
     * about the resolver: positive resolution is pinned in
     * tests/fixtures/go_resolver by test_sprint10.c. */
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
        const oc_json *edges = oc_json_get(graph, "edges");
        CHECK(edges != NULL);
        size_t imports_edges = 0;
        for (size_t i = 0; edges && i < oc_json_size(edges); i++) {
            const oc_json *e = edges->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(e, "kind"), ""), "imports") == 0)
                imports_edges++;
        }
        CHECK(imports_edges == 0);
        /* All 5 fixture files parsed into the graph as file nodes. */
        const oc_json *nodes = oc_json_get(graph, "nodes");
        size_t file_nodes = 0;
        for (size_t i = 0; nodes && i < oc_json_size(nodes); i++) {
            const oc_json *n = nodes->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(n, "kind"), ""), "file") == 0)
                file_nodes++;
        }
        CHECK(file_nodes == 5);
    }

    /* 2) Check: every enabled rule fires, none of the disabled ones do,
     * and the entry-point rule claims main.go (no OC006 there). */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out1);
    CHECK(run_cmd(cmd) == 1); /* findings present, no parse errors (exit 2) */
    oc_json *report = NULL;
    if (oc_read_file(out1, &data, &len, NULL) == OC_OK) {
        report = oc_json_parse(&arena, data, len, NULL);
        free(data);
    }
    CHECK(report != NULL);
    if (report) {
        const oc_json *findings = oc_json_get(report, "findings");
        CHECK(findings != NULL);
        int oc001 = 0, oc002 = 0, oc004 = 0, oc006_main = 0, oc006_count = 0, oc008 = 0, oc011 = 0,
            disabled_rule = 0;
        for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
            const oc_json *f = findings->items[i];
            const char *rule = oc_json_as_str(oc_json_get(f, "rule"), "");
            const char *file = oc_json_as_str(oc_json_get(f, "file"), "");
            if (strcmp(rule, "OC001") == 0 && strstr(file, "util.go"))
                oc001 = 1;
            if (strcmp(rule, "OC002") == 0)
                oc002 = 1;
            if (strcmp(rule, "OC004") == 0 && strstr(file, "util.go"))
                oc004 = 1;
            if (strcmp(rule, "OC006") == 0) {
                oc006_count++;
                if (strstr(file, "main.go"))
                    oc006_main = 1;
            }
            if (strcmp(rule, "OC008") == 0)
                oc008++;
            if (strcmp(rule, "OC011") == 0)
                oc011++;
            /* Still-disabled passes must NEVER fire on Go files.
             * OC003/OC005 are enabled since SPRINT-10; on this corpus
             * they produce nothing (no resolving specifier; OC005's
             * precondition needs an imported file), which the explicit
             * zero-count checks below pin. */
            if (strcmp(rule, "OC007") == 0 || strcmp(rule, "OC009") == 0 ||
                strcmp(rule, "OC010") == 0)
                disabled_rule++;
            if (strcmp(rule, "OC003") == 0 || strcmp(rule, "OC005") == 0)
                disabled_rule++;
        }
        CHECK(oc001);            /* 66-line function over the 60-line threshold */
        CHECK(oc002);            /* TODO marker */
        CHECK(oc004);            /* complexity 25 over the 15 threshold */
        CHECK(oc006_count == 4); /* every non-entry fixture file */
        CHECK(!oc006_main);      /* entry-point rule ("main.go" + package main) claims it */
        CHECK(oc008 == 2);       /* one dup group, one finding per location */
        CHECK(oc011 == 1);       /* AKIA example key, exactly once */
        CHECK(disabled_rule == 0);
        const oc_json *meta = oc_json_get(report, "meta");
        CHECK(oc_json_as_num(oc_json_get(meta, "files_with_parse_errors"), -1) == 0);
        CHECK(oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1) == 5);
        /* The go pack is part of the run (report meta carries the pack
         * list; a pack that fails to load would shrink it). */
        const oc_json *mpacks = oc_json_get(meta, "packs");
        int has_go = 0;
        for (size_t i = 0; mpacks && i < oc_json_size(mpacks); i++) {
            if (strcmp(oc_json_as_str(mpacks->items[i], ""), "go") == 0)
                has_go = 1;
        }
        CHECK(has_go);
    }

    /* 3) Determinism: byte-identical rerun. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out2);
    CHECK(run_cmd(cmd) == 1);
    CHECK(files_equal(out1, out2));

    oc_arena_destroy(&arena);
}

int main(void) {
    unit_go_posture();
    unit_go_pack();
    fixture_checks();
    TEST_SUMMARY();
}
