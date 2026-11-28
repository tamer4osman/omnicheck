/* test_reportdiff.c -- T3 introduced/resolved/persistent gate.
 *
 * Drives the REAL oc diff command over synthetic omnicheck.report/v0
 * documents and proves:
 *   1. classification by fingerprint (the kernel never decides identity):
 *      a finding that merely moved is persistent; a new fingerprint is
 *      introduced; a vanished fingerprint is resolved; multiset counts
 *      (a duplicated fingerprint counts per occurrence);
 *   2. exit codes: 1 when introduced/resolved exist, 0 when identical,
 *      2 on malformed/foreign inputs;
 *   3. --semantic: introduced entries carry advisory {rank, confidence};
 *      two semantic runs are byte-identical; the exit code never changes
 *      because of advisory data;
 *   4. schema gate: foreign documents fail loud (exit 2).
 */
#include "oc/common.h"
#include "oc/json.h"
#include "oc/str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the oc CLI binary"
#endif
#ifndef OC_TEST_TMPDIR
#define OC_TEST_TMPDIR "."
#endif

static int failures = 0;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static int run_diff(const char *extra, const char *b, const char *c,
                    const char *out_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "'%s' diff --baseline '%s' --current '%s' %s > '%s' 2>/dev/null",
             OC_OC_BIN, b, c, extra ? extra : "", out_path);
    int rc = system(cmd);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static void write_report(const char *path, const char *findings_json) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        check(0, "report fixture writable");
        return;
    }
    fprintf(f,
            "{\"meta\":{\"tool\":\"omnicheck\",\"version\":\"0.3.0\","
            "\"schema\":\"omnicheck.report/v0\"},\"findings\":[%s]}\n",
            findings_json);
    fclose(f);
}

static void write_finding(FILE *f, const char *fp, const char *rule,
                          const char *file, long line, const char *msg,
                          int first) {
    fprintf(f,
            "%s{\"fingerprint\":\"%s\",\"rule\":\"%s\",\"file\":\"%s\","
            "\"line\":%ld,\"message\":\"%s\"}",
            first ? "" : ",", fp, rule, file, line, msg);
}

static oc_json *load(oc_arena *a, const char *path) {
    char *data = NULL;
    size_t len = 0;
    oc_status st;
    oc_status_clear(&st);
    if (oc_read_file(path, &data, &len, &st) != OC_OK)
        return NULL;
    oc_status pst;
    oc_status_clear(&pst);
    oc_json *root = oc_json_parse(a, data, len, &pst);
    free(data);
    return root;
}

int main(void) {
    char b1[512], c1[512], d1[512], d2[512], s1[512], s2[512], bad[512];
    snprintf(b1, sizeof(b1), "%s/t3_base.json", OC_TEST_TMPDIR);
    snprintf(c1, sizeof(c1), "%s/t3_cur.json", OC_TEST_TMPDIR);
    snprintf(d1, sizeof(d1), "%s/t3_diff.json", OC_TEST_TMPDIR);
    snprintf(d2, sizeof(d2), "%s/t3_diff2.json", OC_TEST_TMPDIR);
    snprintf(s1, sizeof(s1), "%s/t3_sem1.json", OC_TEST_TMPDIR);
    snprintf(s2, sizeof(s2), "%s/t3_sem2.json", OC_TEST_TMPDIR);
    snprintf(bad, sizeof(bad), "%s/t3_bad.json", OC_TEST_TMPDIR);

    /* Baseline: A (line 10), A-dup (line 20), B (line 30), C (line 40).
     * Current:  A (line 99 -> moved), A-dup (line 20 -> persistent),
     *           B (line 30 -> persistent), D (new -> introduced).
     * => introduced: D; resolved: C; persistent: 3; moved: A. */
    FILE *f = fopen(b1, "wb");
    if (!f) {
        check(0, "baseline fixture writable");
        return 1;
    }
    fprintf(f,
            "{\"meta\":{\"tool\":\"omnicheck\",\"version\":\"0.3.0\","
            "\"schema\":\"omnicheck.report/v0\"},\"findings\":[");
    write_finding(f, "fp-a", "OC001", "a.c", 10, "long function", 1);
    write_finding(f, "fp-a", "OC001", "a.c", 20, "long function", 0);
    write_finding(f, "fp-b", "OC002", "b.c", 30, "todo comment", 0);
    write_finding(f, "fp-c", "OC003", "c.c", 40, "unused export", 0);
    fprintf(f, "]}\n");
    fclose(f);

    f = fopen(c1, "wb");
    if (!f) {
        check(0, "current fixture writable");
        return 1;
    }
    fprintf(f,
            "{\"meta\":{\"tool\":\"omnicheck\",\"version\":\"0.3.0\","
            "\"schema\":\"omnicheck.report/v0\"},\"findings\":[");
    write_finding(f, "fp-a", "OC001", "a.c", 99, "long function", 1);
    write_finding(f, "fp-a", "OC001", "a.c", 20, "long function", 0);
    write_finding(f, "fp-b", "OC002", "b.c", 30, "todo comment", 0);
    write_finding(f, "fp-d", "OC004", "d.c", 5, "hardcoded secret", 0);
    fprintf(f, "]}\n");
    fclose(f);

    /* ---- 1. classification + exit 1 ---- */
    int rc = run_diff("", b1, c1, d1);
    check(rc == 1, "diff exits 1 when semantic changes exist");
    {
        oc_arena a;
        oc_arena_init(&a);
        oc_json *r = load(&a, d1);
        check(r != NULL, "diff document parses");
        if (r) {
            const oc_json *intro = oc_json_get(r, "introduced");
            const oc_json *resolved = oc_json_get(r, "resolved");
            const oc_json *moved = oc_json_get(r, "moved");
            const oc_json *counts = oc_json_get(r, "counts");
            check(intro && resolved && moved && counts,
                  "diff document carries all sections");
            if (intro)
                check(intro->n_items == 1,
                      "one introduced fingerprint (D)");
            if (resolved)
                check(resolved->n_items == 1,
                      "one resolved fingerprint (C)");
            if (counts) {
                check(oc_json_as_num(oc_json_get(counts, "introduced"), -1) == 1,
                      "counts.introduced == 1");
                check(oc_json_as_num(oc_json_get(counts, "resolved"), -1) == 1,
                      "counts.resolved == 1");
                check(oc_json_as_num(oc_json_get(counts, "persistent"), -1) == 3,
                      "counts.persistent == 3 (multiset intersection)");
                check(oc_json_as_num(oc_json_get(counts, "moved"), -1) == 1,
                      "counts.moved == 1 (A moved)");
            }
            if (moved && moved->n_items == 1) {
                const oc_json *m0 = moved->items[0];
                check(oc_json_as_num(oc_json_get(m0, "baseline_line"), -1) == 10 &&
                          oc_json_as_num(oc_json_get(m0, "current_line"), -1) == 99,
                      "moved carries baseline_line/current_line");
            }
            if (intro && intro->n_items == 1) {
                check(oc_json_get(intro->items[0], "advisory") == NULL,
                      "without --semantic no advisory is emitted");
            }
        }
        oc_arena_destroy(&a);
    }

    /* ---- 2a. identical reports: exit 0 ---- */
    rc = run_diff("", b1, b1, d2);
    check(rc == 0, "diff exits 0 on identical reports");

    /* ---- 3. --semantic: advisory scores, deterministic, gate-stable ---- */
    rc = run_diff("--semantic", b1, c1, s1);
    check(rc == 1, "--semantic does not change the exit gate");
    if (rc == 1) {
        oc_arena a;
        oc_arena_init(&a);
        oc_json *r = load(&a, s1);
        check(r != NULL, "semantic diff document parses");
        if (r) {
            const oc_json *sem = oc_json_get(r, "semantic");
            check(sem != NULL && sem->type == OC_JSON_BOOL && sem->boolean,
                      "semantic flag recorded in the document");
            const oc_json *intro = oc_json_get(r, "introduced");
            check(intro && intro->n_items == 1, "one introduced entry");
            if (intro && intro->n_items == 1) {
                const oc_json *adv = oc_json_get(intro->items[0], "advisory");
                check(adv != NULL, "introduced carries advisory scores");
                if (adv) {
                    check(oc_json_as_num(oc_json_get(adv, "rank"), -1) >= 0,
                          "advisory rank present");
                    double c = oc_json_as_num(
                        oc_json_get(adv, "confidence"), -1.0);
                    check(c >= 0.0 && c <= 1.0,
                          "advisory confidence in [0,1]");
                }
            }
        }
        oc_arena_destroy(&a);
    }
    if (run_diff("--semantic", b1, c1, s2) == 1) {
        char *d1d = NULL, *d2d = NULL;
        size_t n1 = 0, n2 = 0;
        oc_status st;
        oc_status_clear(&st);
        if (oc_read_file(s1, &d1d, &n1, &st) == OC_OK &&
            oc_read_file(s2, &d2d, &n2, &st) == OC_OK) {
            check(n1 == n2 && memcmp(d1d, d2d, n1) == 0,
                  "two semantic runs are byte-identical");
        } else {
            check(0, "semantic reports readable for determinism");
        }
        free(d1d);
        free(d2d);
    }

    /* ---- 4. schema gate: foreign document fails loud ---- */
    f = fopen(bad, "wb");
    if (f) {
        fprintf(f, "{\"hello\":\"world\"}\n");
        fclose(f);
    }
    rc = run_diff("", bad, c1, d2);
    check(rc == 2, "foreign schema fails loud (exit 2)");
    rc = run_diff("", b1, "/nonexistent/report.json", d2);
    check(rc == 2, "unreadable current fails loud (exit 2)");

    if (failures == 0) {
        printf("test_reportdiff: all checks passed\n");
        return 0;
    }
    printf("test_reportdiff: %d check(s) FAILED\n", failures);
    return 1;
}
