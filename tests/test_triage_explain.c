/* test_triage_explain.c -- T1 rationale-attach gate.
 *
 * Drives the REAL oc CLI end to end (like test_triage_real) and proves:
 *   1. `oc check --triage-embedded --triage-explain` attaches the 9 kernel
 *      rationale channels to EVERY ranked finding (explained_count ==
 *      ranked_count == distinct findings carrying an "explain" object);
 *   2. rank arithmetic untouched: each finding's explain.probability
 *      equals its triage_confidence at 4 decimals, and the
 *      without-explain run has byte-identical ranks/confidences per
 *      fingerprint;
 *   3. determinism: two explain runs are byte-identical;
 *   4. fail-open with the sidecar: findings carry NO explain object and
 *      the triage meta names the limitation (sidecar protocol-v0 has no
 *      rationale channel).
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
#ifndef OC_PROVIDER_BIN
#error "OC_PROVIDER_BIN must point at oc-omnidecide-provider"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif
#ifndef OC_TRIAGE_CORPUS
#error "OC_TRIAGE_CORPUS must point at the scan corpus"
#endif
#ifndef OC_TEST_TMPDIR
#define OC_TEST_TMPDIR "."
#endif

static int failures = 0;
static long explained_total = 0;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static const char *EXPLAIN_KEYS[9] = {"probability",   "fused",
                                      "tf_cosine",     "jaccard",
                                      "trigram",       "whole",
                                      "best_chunk",    "contextual",
                                      "directional"};

/* Runs oc check with the given extra flags, writing stdout to out_path.
 * Returns the exit code. */
static int run_oc(const char *flags, const char *out_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "'%s' check '%s' --packs-dir '%s/packs' %s > '%s' 2>/dev/null",
             OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, flags, out_path);
    int rc = system(cmd);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static oc_json *load_report(oc_arena *a, const char *path) {
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
    char p_with[512], p_with2[512], p_without[512], p_side[512];
    snprintf(p_with, sizeof(p_with), "%s/t1_with.json", OC_TEST_TMPDIR);
    snprintf(p_with2, sizeof(p_with2), "%s/t1_with2.json", OC_TEST_TMPDIR);
    snprintf(p_without, sizeof(p_without), "%s/t1_without.json", OC_TEST_TMPDIR);
    snprintf(p_side, sizeof(p_side), "%s/t1_side.json", OC_TEST_TMPDIR);

    /* ---- 1. embedded + explain: every ranked finding explained ---- */
    int rc = run_oc("--triage-embedded --triage-explain", p_with);
    check(rc == 0 || rc == 1, "explain run completes with a report exit code");
    {
        oc_arena a;
        oc_arena_init(&a);
        oc_json *r = load_report(&a, p_with);
        check(r != NULL, "explain run report parses");
        if (r) {
            const oc_json *tri = oc_json_get(r, "triage");
            const oc_json *findings = oc_json_get(r, "findings");
            check(tri != NULL && findings != NULL,
                  "report carries triage meta and findings");
            if (tri && findings) {
                const char *prov =
                    oc_json_as_str(oc_json_get(tri, "provider"), "");
                check(strncmp(prov, "embedded:", 9) == 0,
                      "provider is the embedded kernel");
                check(oc_json_as_num(oc_json_get(tri, "explained_count"), -1) ==
                          oc_json_as_num(oc_json_get(tri, "ranked_count"), -2),
                      "explained_count == ranked_count");
                check(strcmp(oc_json_as_str(oc_json_get(tri, "explain_reason"),
                                            ""),
                             "ok") == 0,
                      "explain_reason is ok");
                long ranked = (long)oc_json_as_num(
                    oc_json_get(tri, "ranked_count"), -1);
                check(ranked > 0, "the corpus produces ranked findings");
                explained_total = ranked;

                long with_explain = 0;
                for (size_t i = 0; i < findings->n_items; i++) {
                    const oc_json *f = findings->items[i];
                    double rank = oc_json_as_num(
                        oc_json_get(f, "triage_rank"), -1.0);
                    const oc_json *ex = oc_json_get(f, "explain");
                    if (rank < 0) {
                        check(ex == NULL,
                              "unranked finding carries no explain object");
                        continue;
                    }
                    if (ex)
                        with_explain++;
                    check(ex != NULL,
                          "every ranked finding carries an explain object");
                    if (!ex)
                        continue;
                    for (int k = 0; k < 9; k++)
                        check(oc_json_get(ex, EXPLAIN_KEYS[k]) != NULL,
                              "explain carries the channel");
                    double prob = oc_json_as_num(
                        oc_json_get(ex, "probability"), -1.0);
                    double conf = oc_json_as_num(
                        oc_json_get(f, "triage_confidence"), -2.0);
                    char sa[32], sb[32];
                    snprintf(sa, sizeof(sa), "%.4f", prob);
                    snprintf(sb, sizeof(sb), "%.4f", conf);
                    check(strcmp(sa, sb) == 0,
                          "probability == triage_confidence at 4 decimals "
                          "(rank arithmetic untouched)");
                }
                check(with_explain == ranked,
                      "distinct findings carrying explain == ranked_count");
            }
        }
        oc_arena_destroy(&a);
    }

    /* ---- 3. determinism: two explain runs byte-identical ---- */
    if (run_oc("--triage-embedded --triage-explain", p_with2) >= 0) {
        char *d1 = NULL, *d2 = NULL;
        size_t n1 = 0, n2 = 0;
        oc_status st;
        oc_status_clear(&st);
        if (oc_read_file(p_with, &d1, &n1, &st) == OC_OK &&
            oc_read_file(p_with2, &d2, &n2, &st) == OC_OK) {
            check(n1 == n2 && memcmp(d1, d2, n1) == 0,
                  "two explain runs are byte-identical");
        } else {
            check(0, "explain reports readable for the determinism check");
        }
        free(d1);
        free(d2);
    } else {
        check(0, "second explain run executed");
    }

    /* ---- 2. rank arithmetic untouched vs the without-explain run ---- */
    if (run_oc("--triage-embedded", p_without) >= 0) {
        oc_arena a;
        oc_arena_init(&a);
        oc_json *rw = load_report(&a, p_with);
        oc_json *ro = load_report(&a, p_without);
        check(rw != NULL && ro != NULL, "both reports parse for rank compare");
        if (rw && ro) {
            const oc_json *fw = oc_json_get(rw, "findings");
            const oc_json *fo = oc_json_get(ro, "findings");
            check(fw != NULL && fo != NULL && fw->n_items == fo->n_items,
                  "finding counts match across runs");
            if (fw && fo && fw->n_items == fo->n_items) {
                for (size_t i = 0; i < fw->n_items; i++) {
                    const oc_json *a1 = fw->items[i];
                    const oc_json *b1 = fo->items[i];
                    check(strcmp(oc_json_as_str(oc_json_get(a1, "fingerprint"),
                                                ""),
                                 oc_json_as_str(oc_json_get(b1, "fingerprint"),
                                                "")) == 0,
                          "finding order stable across runs");
                    double r1 = oc_json_as_num(oc_json_get(a1, "triage_rank"),
                                               -1.0);
                    double r2 = oc_json_as_num(oc_json_get(b1, "triage_rank"),
                                               -1.0);
                    double c1 = oc_json_as_num(
                        oc_json_get(a1, "triage_confidence"), -1.0);
                    double c2 = oc_json_as_num(
                        oc_json_get(b1, "triage_confidence"), -1.0);
                    char sa[32], sb[32];
                    snprintf(sa, sizeof(sa), "%.4f|%.4f", r1, c1);
                    snprintf(sb, sizeof(sb), "%.4f|%.4f", r2, c2);
                    check(strcmp(sa, sb) == 0,
                          "rank+confidence identical with and without explain");
                    check(oc_json_get(b1, "explain") == NULL,
                          "without the flag no explain object is emitted");
                }
                const oc_json *to = oc_json_get(ro, "triage");
                check(to && oc_json_get(to, "explained_count") == NULL,
                      "without the flag the triage meta stays byte-stable");
            }
        }
        oc_arena_destroy(&a);
    } else {
        check(0, "without-explain run executed");
    }

    /* ---- 4. sidecar fail-open: named limitation, no explain fields ---- */
    {
        setenv("OC_OMNIDECIDE_BIN", OC_PROVIDER_BIN, 1);
        int src = run_oc("--triage --triage-explain", p_side);
        check(src == 0 || src == 1, "sidecar run completes");
        unsetenv("OC_OMNIDECIDE_BIN");
        oc_arena a;
        oc_arena_init(&a);
        oc_json *r = load_report(&a, p_side);
        check(r != NULL, "sidecar run report parses");
        if (r) {
            const oc_json *tri = oc_json_get(r, "triage");
            const oc_json *findings = oc_json_get(r, "findings");
            check(tri != NULL, "sidecar report carries triage meta");
            if (tri) {
                const char *reason = oc_json_as_str(
                    oc_json_get(tri, "explain_reason"), "");
                check(strstr(reason, "sidecar protocol-v0 carries no "
                                     "rationale") != NULL,
                      "sidecar limitation is named in explain_reason");
                check(oc_json_as_num(oc_json_get(tri, "explained_count"),
                                     -1) == 0,
                      "explained_count is 0 on the sidecar path");
            }
            if (findings) {
                for (size_t i = 0; i < findings->n_items; i++)
                    check(oc_json_get(findings->items[i], "explain") == NULL,
                          "sidecar findings carry no explain object");
            }
        }
        oc_arena_destroy(&a);
    }

    if (failures == 0) {
        printf("test_triage_explain: all checks passed "
               "(explained %ld/%ld findings on the acceptance corpus)\n",
               explained_total, explained_total);
        return 0;
    }
    printf("test_triage_explain: %d check(s) FAILED\n", failures);
    return 1;
}
