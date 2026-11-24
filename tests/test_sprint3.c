/* Sprint 3 (WBS 2.4 + 2.6) -- OC008 duplicate-code and the human report
 * format. E2E contracts run the real CLI binary; the fixture tree in
 * fixtures_sprint3/dupes encodes: a 12-line cross-file block with
 * indentation variance, a 10-line intra-file block, and a 9-line block
 * below the default threshold. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "oc/common.h"
#include "oc/json.h"
#include "testkit.h"

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

static size_t count_findings(const oc_json *report, const char *rule) {
    const oc_json *findings = oc_json_get(report, "findings");
    size_t n = oc_json_size(findings), hits = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(oc_json_as_str(oc_json_get(findings->items[i], "rule"), ""), rule) == 0)
            hits++;
    }
    return hits;
}

static size_t json_uint(const oc_json *obj, const char *key) {
    const oc_json *v = oc_json_get(obj, key);
    return v ? (size_t)oc_json_as_num(v, -1.0) : (size_t)-1;
}

/* ------------------------------------------------------------------ main -- */

int main(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char fixtures[512], packs[512];
    snprintf(fixtures, sizeof(fixtures), "%s/tests/fixtures_sprint3/dupes", root);
    snprintf(packs, sizeof(packs), "%s/packs", root);

    char cmd[4096];
    oc_arena arena;
    oc_arena_init(&arena);

    /* 1) OC008 baseline: two groups, four findings, exact spans. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > /tmp/oc_s3_base.json 2>/dev/null", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 1); /* warn findings present */
    oc_json *base = load_json(&arena, "/tmp/oc_s3_base.json");
    CHECK(base != NULL);
    CHECK(count_findings(base, "OC008") == 4);
    CHECK(json_uint(oc_json_get(base, "meta"), "finding_count") >= 4);
    {
        const oc_json *meta = oc_json_get(base, "meta");
        const oc_json *dup = oc_json_get(meta, "dup");
        CHECK(dup != NULL);
        CHECK(json_uint(dup, "groups") == 2);
        CHECK(json_uint(dup, "groups_truncated") == 0);
        CHECK(json_uint(dup, "files_skipped_oversize") == 0);
    }
    {
        /* Span and message contracts: a.js/b.js share a 12-line block at
         * 6..17 despite different indentation; self.js carries both
         * occurrences of a 10-line block (6..15 and 20..29). */
        const oc_json *findings = oc_json_get(base, "findings");
        int saw_ab = 0, saw_self_first = 0, saw_self_second = 0;
        size_t n = oc_json_size(findings);
        for (size_t i = 0; i < n; i++) {
            const oc_json *f = findings->items[i];
            const char *file = oc_json_as_str(oc_json_get(f, "file"), "");
            const char *msg = oc_json_as_str(oc_json_get(f, "message"), "");
            if (strcmp(oc_json_as_str(oc_json_get(f, "rule"), ""), "OC008") != 0)
                continue;
            if (strstr(file, "/a.js")) {
                saw_ab = 1;
                CHECK(json_uint(f, "line") == 6);
                CHECK(json_uint(f, "end_line") == 17);
                CHECK(strstr(msg, "12-line block duplicated at 2 location(s)") != NULL);
                CHECK(strstr(msg, "b.js:6-17") != NULL);
            }
            if (strstr(file, "/self.js") && json_uint(f, "line") == 6) {
                saw_self_first = 1;
                CHECK(json_uint(f, "end_line") == 15);
                CHECK(strstr(msg, "self.js:20-29") != NULL);
            }
            if (strstr(file, "/self.js") && json_uint(f, "line") == 20) {
                saw_self_second = 1;
                CHECK(json_uint(f, "end_line") == 29);
                CHECK(strstr(msg, "self.js:6-15") != NULL);
            }
        }
        CHECK(saw_ab);
        CHECK(saw_self_first);
        CHECK(saw_self_second);
    }

    /* 2) Config boundary: min_block=13 silences everything (the shared
     *    blocks are 12 and 10 lines); min_block=9 exposes the third
     *    (a.js/short.js) group. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s --packs-dir %s --config OC008.min_block=13 > /tmp/oc_s3_13.json "
             "2>/dev/null",
             bin, fixtures, packs);
    /* Only OC006 info findings remain at min_block=13, so the run exits
     * 0 -- info severity is below the default fail-on threshold. */
    CHECK(run_cmd(cmd) == 0);
    oc_json *mb13 = load_json(&arena, "/tmp/oc_s3_13.json");
    CHECK(mb13 != NULL);
    CHECK(count_findings(mb13, "OC008") == 0);
    {
        const oc_json *dup = oc_json_get(oc_json_get(mb13, "meta"), "dup");
        CHECK(json_uint(dup, "groups") == 0);
    }

    snprintf(cmd, sizeof(cmd),
             "%s check %s --packs-dir %s --config OC008.min_block=9 > /tmp/oc_s3_9.json "
             "2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    oc_json *mb9 = load_json(&arena, "/tmp/oc_s3_9.json");
    CHECK(mb9 != NULL);
    CHECK(count_findings(mb9, "OC008") == 6);
    {
        const oc_json *dup = oc_json_get(oc_json_get(mb9, "meta"), "dup");
        CHECK(json_uint(dup, "groups") == 3);
    }

    /* 3) Determinism: identical runs are byte-identical, including the
     *    composed dup index. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > /tmp/oc_s3_rerun.json 2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    {
        char *a = NULL, *b = NULL;
        size_t la = 0, lb = 0;
        CHECK(oc_read_file("/tmp/oc_s3_base.json", &a, &la, NULL) == OC_OK);
        CHECK(oc_read_file("/tmp/oc_s3_rerun.json", &b, &lb, NULL) == OC_OK);
        CHECK(la == lb && memcmp(a, b, la) == 0);
        free(a);
        free(b);
    }

    /* 4) Human report: sections, counts, dup locations; deterministic. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s --packs-dir %s --format human > /tmp/oc_s3_human.txt 2>/dev/null", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    {
        char *data = NULL;
        size_t len = 0;
        CHECK(oc_read_file("/tmp/oc_s3_human.txt", &data, &len, NULL) == OC_OK);
        CHECK(strstr(data, "human report") != NULL);
        CHECK(strstr(data, "[OC008] duplicate-code (warn) -- 4 finding(s)") != NULL);
        CHECK(strstr(data, "dup scan: 2 group(s)") != NULL);
        CHECK(strstr(data, "b.js:6-17") != NULL);
        CHECK(strstr(data, "self.js:6-15") != NULL);
        CHECK(strstr(data, "findings: ") != NULL);
        free(data);
    }
    snprintf(cmd, sizeof(cmd),
             "%s check %s --packs-dir %s --format human > /tmp/oc_s3_human2.txt 2>/dev/null", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    {
        char *a = NULL, *b = NULL;
        size_t la = 0, lb = 0;
        CHECK(oc_read_file("/tmp/oc_s3_human.txt", &a, &la, NULL) == OC_OK);
        CHECK(oc_read_file("/tmp/oc_s3_human2.txt", &b, &lb, NULL) == OC_OK);
        CHECK(la == lb && memcmp(a, b, la) == 0);
        free(a);
        free(b);
    }

    /* 5) Explicit --format json is byte-identical to the default run. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s --packs-dir %s --format json > /tmp/oc_s3_explicit.json 2>/dev/null", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    {
        char *a = NULL, *b = NULL;
        size_t la = 0, lb = 0;
        CHECK(oc_read_file("/tmp/oc_s3_base.json", &a, &la, NULL) == OC_OK);
        CHECK(oc_read_file("/tmp/oc_s3_explicit.json", &b, &lb, NULL) == OC_OK);
        CHECK(la == lb && memcmp(a, b, la) == 0);
        free(a);
        free(b);
    }

    /* 6) Unknown format is a hard usage error (exit 2). */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s --format yaml 2>/dev/null", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 2);

    /* 7) Human report on broken fixtures: parse errors are a named
     *    section, exit code stays 2 (no silent partial analysis). */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/tests/fixtures_broken --packs-dir %s --format human "
             "> /tmp/oc_s3_broken.txt 2>/dev/null",
             bin, root, packs);
    CHECK(run_cmd(cmd) == 2);
    {
        char *data = NULL;
        size_t len = 0;
        CHECK(oc_read_file("/tmp/oc_s3_broken.txt", &data, &len, NULL) == OC_OK);
        CHECK(strstr(data, "parse errors:") != NULL);
        CHECK(strstr(data, "broken.js") != NULL);
        CHECK(strstr(data, "broken.py") != NULL);
        free(data);
    }

    /* 8) No-dup control stays OC008-silent (examples tree). */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/examples --packs-dir %s > /tmp/oc_s3_ex.json "
             "2>/dev/null",
             bin, root, packs);
    CHECK(run_cmd(cmd) == 1);
    {
        oc_json *ex = load_json(&arena, "/tmp/oc_s3_ex.json");
        CHECK(ex != NULL);
        CHECK(count_findings(ex, "OC008") == 0);
        const oc_json *dup = oc_json_get(oc_json_get(ex, "meta"), "dup");
        CHECK(json_uint(dup, "groups") == 0);
    }

    /* every arena in this test must die before exit: LSan's reachability
     * scan classifies stale-stack-reachable blocks nondeterministically,
     * so an undestroyed arena here failed the sanitize cell ~3/10 runs
     * (caught by the M2 gate-day dogfood matrix, 2026-11-19). */
    oc_arena_destroy(&arena);

    TEST_SUMMARY();
}
