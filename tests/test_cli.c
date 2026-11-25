/* End-to-end CLI tests: run the real `oc` binary against test fixtures
 * and verify the report contract, determinism, and exit codes. */
#include "oc/arena.h"
#include "oc/common.h"
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
    if (WIFEXITED(rc))
        return WEXITSTATUS(rc);
    return -1;
}

/* Loads a file and parses it as JSON (buffer freed before returning). */
static oc_json *load_json(oc_arena *a, const char *path) {
    char *data = NULL;
    size_t len = 0;
    if (oc_read_file(path, &data, &len, NULL) != OC_OK)
        return NULL;
    oc_json *v = oc_json_parse(a, data, len, NULL);
    free(data);
    return v;
}

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

int main(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char fixtures[512], packs[512], out1[256], out2[256];
    char clean[1024];
    snprintf(fixtures, sizeof(fixtures), "%s/tests/fixtures", root);
    snprintf(packs, sizeof(packs), "%s/packs", root);
    snprintf(out1, sizeof(out1), "/tmp/oc_cli_test_out1.json");
    snprintf(out2, sizeof(out2), "/tmp/oc_cli_test_out2.json");

    char cmd[2048];
    oc_arena arena;
    oc_arena_init(&arena);

    /* 1) Full run: findings exist, exit 1 at default warn threshold. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s --packs-dir %s --config OC001.max_lines=5 "
             "> %s 2>/tmp/oc_cli_test_err1",
             bin, fixtures, packs, out1);
    CHECK(run_cmd(cmd) == 1);

    oc_json *report = load_json(&arena, out1);
    CHECK(report != NULL);
    if (report) {
        const oc_json *meta = oc_json_get(report, "meta");
        CHECK(meta != NULL);
        CHECK_STR_EQ(oc_json_as_str(oc_json_get(meta, "tool"), "?"), "omnicheck");
        CHECK_STR_EQ(oc_json_as_str(oc_json_get(meta, "schema"), "?"), "omnicheck.report/v0");
        CHECK_STR_EQ(oc_json_as_str(oc_json_get(meta, "backend"), "?"), "tree-sitter");
        CHECK(oc_json_as_num(oc_json_get(meta, "finding_count"), 0) >= 4);
        /* Sprint 5: the fixtures tree now also carries supply manifests
         * and coverage JSON files (not pack-matchable), so the count of
         * unknown-type entries is >= 1 rather than exactly 1. */
        CHECK(oc_json_as_num(oc_json_get(meta, "files_skipped_unknown_type"), -1) >= 1);

        const oc_json *findings = oc_json_get(report, "findings");
        CHECK(findings != NULL);
        size_t n = oc_json_size(findings);
        CHECK(n >= 4);
        if (n >= 4) {
            for (size_t i = 1; i < n; i++) {
                const char *f0 = oc_json_as_str(oc_json_get(findings->items[i - 1], "file"), "");
                const char *f1 = oc_json_as_str(oc_json_get(findings->items[i], "file"), "");
                CHECK(strcmp(f0, f1) <= 0);
            }
            const char *fp0 = oc_json_as_str(oc_json_get(findings->items[0], "fingerprint"), "");
            CHECK(strlen(fp0) == 16);
            const oc_json *triage = oc_json_get(report, "triage");
            CHECK(triage != NULL);
            CHECK(oc_json_as_bool(oc_json_get(triage, "ranked"), 1) == 0);
            CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(triage, "guardrails"), ""), "rank-only");
        }
    }

    /* 2) Byte-identical rerun (determinism contract). */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s --config OC001.max_lines=5 > %s", bin,
             fixtures, packs, out2);
    CHECK(run_cmd(cmd) == 1);
    CHECK(files_equal(out1, out2));

    /* 3) Clean subtree: exit 0 with zero findings. */
    snprintf(clean, sizeof(clean), "%s/js/clean_only", fixtures);
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s", bin, clean, packs);
    CHECK(run_cmd(cmd) == 0);

    /* 4) --fail-on info: info findings (OC002) now trip exit 1. */
    snprintf(cmd, sizeof(cmd), "%s check %s/js --packs-dir %s --fail-on info", bin, fixtures,
             packs);
    CHECK(run_cmd(cmd) == 1);

    /* 5) --triage with the mock sidecar: ranked=true, findings intact. */
    snprintf(cmd, sizeof(cmd),
             "OC_OMNIDECIDE_BIN=%s/tools/mock_omnidecide.py "
             "%s check %s --packs-dir %s --triage --config OC001.max_lines=5 "
             "> /tmp/oc_cli_test_triage.json",
             root, bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    oc_json *tr = load_json(&arena, "/tmp/oc_cli_test_triage.json");
    CHECK(tr != NULL);
    const oc_json *triage = oc_json_get(tr, "triage");
    CHECK(triage != NULL);
    CHECK(oc_json_as_bool(oc_json_get(triage, "ranked"), 0) == 1);
    CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(triage, "model"), "?"), "mock");
    const oc_json *tfindings = oc_json_get(tr, "findings");
    size_t tn = oc_json_size(tfindings);
    CHECK(tn >= 4);
    size_t triaged_count = 0;
    for (size_t i = 0; i < tn; i++) {
        if (oc_json_get(tfindings->items[i], "triage_rank") != NULL)
            triaged_count++;
    }
    CHECK(triaged_count == tn);

    /* 6) Version + usage errors. */
    snprintf(cmd, sizeof(cmd), "%s --version", bin);
    CHECK(run_cmd(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "%s check /nonexistent-root --packs-dir %s", bin, packs);
    CHECK(run_cmd(cmd) == 2);
    snprintf(cmd, sizeof(cmd), "%s frobnicate", bin);
    CHECK(run_cmd(cmd) == 2);

    /* 7) graph subcommand emits a parseable graph. */
    snprintf(cmd, sizeof(cmd), "%s graph %s/js --packs-dir %s > /tmp/oc_cli_test_graph.json", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *graph = load_json(&arena, "/tmp/oc_cli_test_graph.json");
    CHECK(graph != NULL);
    CHECK(oc_json_size(oc_json_get(graph, "nodes")) >= 2);
    CHECK(oc_json_size(oc_json_get(graph, "edges")) >= 1);

    /* 8) packs subcommand. */
    snprintf(cmd, sizeof(cmd), "%s packs --packs-dir %s > /tmp/oc_cli_test_packs.json", bin, packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *pl = load_json(&arena, "/tmp/oc_cli_test_packs.json");
    CHECK(pl != NULL);
    CHECK(oc_json_size(pl) == 15); /* sprint 1: +typescript, +tsx; sprint 9: +go; sprint 11: +java;
                                      sprint 13: +csharp; sprint 15: +rust; sprint 16: +ruby;
                                      sprint 19: +php; sprint 20: +c; sprint 21: +cpp;
                                      sprint 22: +kotlin; sprint 23: +swift; sprint 24: +scala */

    oc_arena_destroy(&arena);
    TEST_SUMMARY();
}
