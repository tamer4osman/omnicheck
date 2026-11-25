/* Triage sidecar contract tests: ranks applied, guardrails hold, and
 * fail-open behavior on missing/broken providers. */
#include "oc/triage.h"
#include "testkit.h"

#include <stdlib.h>
#include <time.h>

#ifndef OC_TRIAGE_FIXTURES
#error "OC_TRIAGE_FIXTURES must point at tests/fixtures_triage"
#endif

static void push(oc_finding_vec *v, oc_arena *a, const char *rule, const char *file,
                 const char *symbol, const char *msg, const char *fp_text) {
    oc_finding_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.rule_id = rule;
    spec.rule_name = "test-rule";
    spec.severity = OC_SEV_WARN;
    spec.file = file;
    spec.line = 1;
    spec.col = 1;
    spec.end_line = 5;
    spec.symbol = symbol;
    spec.message = msg;
    spec.fp_text = fp_text;
    CHECK(oc_findings_push(v, a, &spec));
}

static void snapshot(const oc_finding_vec *v, size_t *n, oc_severity *max_sev, char msgs[][64]) {
    *n = v->n;
    *max_sev = oc_findings_max_severity(v);
    for (size_t i = 0; i < v->n && i < 8; i++)
        snprintf(msgs[i], 64, "%s", v->items[i].message);
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv) {
#ifndef OC_MOCK_SHIM
#error "OC_MOCK_SHIM must point at tools/mock_omnidecide.py"
#endif
    const char *mock_shim = argc >= 2 ? argv[1] : OC_MOCK_SHIM;
    CHECK(mock_shim && mock_shim[0]);

    oc_arena a;
    oc_arena_init(&a);

    oc_finding_vec v;
    oc_findings_init(&v);
    push(&v, &a, "OC001", "src/a.js", "bigFn", "Function 'bigFn' spans 81 lines", NULL);
    push(&v, &a, "OC002", "src/b.js", NULL, "TODO marker: rewrite loader", "todo rewrite loader");
    push(&v, &a, "OC002", "src/b.js", NULL, "FIXME marker: validate input", "fixme validate input");

    /* Baseline snapshot (guardrail reference). */
    size_t base_n;
    oc_severity base_sev;
    char base_msgs[8][64];
    snapshot(&v, &base_n, &base_sev, base_msgs);

    /* 1) No provider configured: fail-open, nothing changes. */
    unsetenv("OC_OMNIDECIDE_BIN");
    oc_triage_outcome out;
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&v, &out, &st) == OC_OK);
    CHECK(out.ranked == 0);
    CHECK_STR_CONTAINS(out.reason, "OC_OMNIDECIDE_BIN not set");
    CHECK(v.n == base_n);
    CHECK(oc_findings_max_severity(&v) == base_sev);
    CHECK(v.items[0].triaged == 0);

    /* 2) Working mock sidecar: ranks applied, findings untouched. */
    setenv("OC_OMNIDECIDE_BIN", mock_shim, 1);
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&v, &out, &st) == OC_OK);
    CHECK(out.ranked == 1);
    CHECK(out.ranked_count == base_n);
    CHECK_STR_EQ(out.model, "mock-omnidecide/0");
    CHECK(v.n == base_n);
    CHECK(oc_findings_max_severity(&v) == base_sev);
    for (size_t i = 0; i < v.n; i++) {
        CHECK(v.items[i].triaged == 1);
        CHECK(v.items[i].triage_rank >= 0);
        CHECK(v.items[i].triage_confidence > 0.0);
    }
    for (size_t i = 0; i < v.n; i++)
        CHECK_STR_EQ(v.items[i].message, base_msgs[i]);
    /* Ranks are a permutation of 0..n-1 (rank-only, no drops). */
    int seen_rank[8] = {0};
    for (size_t i = 0; i < v.n && i < 8; i++) {
        int r = v.items[i].triage_rank;
        CHECK(r >= 0 && r < 8 && seen_rank[r] == 0);
        if (r >= 0 && r < 8)
            seen_rank[r] = 1;
    }

    /* 3) Broken provider (/bin/false): fail-open again. */
    setenv("OC_OMNIDECIDE_BIN", "/bin/false", 1);
    for (size_t i = 0; i < v.n; i++) {
        v.items[i].triaged = 0;
        v.items[i].triage_rank = -1;
    }
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&v, &out, &st) == OC_OK);
    CHECK(out.ranked == 0);
    CHECK(v.n == base_n);
    CHECK(oc_findings_max_severity(&v) == base_sev);

    /* 4) Nonexistent binary path: fail-open. */
    setenv("OC_OMNIDECIDE_BIN", "/nonexistent/binary/xyz", 1);
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&v, &out, &st) == OC_OK);
    CHECK(out.ranked == 0);
    CHECK_STR_CONTAINS(out.reason, "failed to start");

    /* 5) Empty findings: trivial no-op. */
    oc_finding_vec empty;
    oc_findings_init(&empty);
    setenv("OC_OMNIDECIDE_BIN", mock_shim, 1);
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&empty, &out, &st) == OC_OK);
    CHECK(out.ranked == 0);
    CHECK_STR_CONTAINS(out.reason, "no findings");

    /* 6) Silent provider slower than the budget: must fail-open AT the
     * budget, not hang. Found by wiring the real OmniDecide kernel: the
     * original one-shot alarm fired once and the EINTR-continue loop
     * re-blocked unbounded (a 6 s silent provider waited 6 s; a
     * never-exiting one hung forever). Upper bounds are generous for CI
     * machines; the essential assertion is COMPLETION within a small
     * multiple of the budget. */
    setenv("OC_OMNIDECIDE_BIN", OC_TRIAGE_FIXTURES "/silent_slow.sh", 1);
    setenv("OC_OMNIDECIDE_TIMEOUT_S", "1", 1);
    for (size_t i = 0; i < v.n; i++) {
        v.items[i].triaged = 0;
        v.items[i].triage_rank = -1;
    }
    long long t0 = now_ms();
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&v, &out, &st) == OC_OK);
    long long dt = now_ms() - t0;
    CHECK(out.ranked == 0);
    CHECK_STR_CONTAINS(out.reason, "timed out");
    CHECK(dt >= 1000); /* budget actually spent */
    CHECK(dt < 8000);  /* bounded: budget + generous CI slack */
    CHECK(v.n == base_n);
    CHECK(oc_findings_max_severity(&v) == base_sev);

    /* 7) Never-exiting silent provider under the DEFAULT budget: the old
     * code hung forever here; now fail-open at ~3 s with the provider
     * killed. This is the regression test for the real-hang defect. */
    unsetenv("OC_OMNIDECIDE_TIMEOUT_S");
    setenv("OC_OMNIDECIDE_BIN", OC_TRIAGE_FIXTURES "/silent_hang.sh", 1);
    t0 = now_ms();
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&v, &out, &st) == OC_OK);
    dt = now_ms() - t0;
    CHECK(out.ranked == 0);
    CHECK_STR_CONTAINS(out.reason, "timed out");
    CHECK(dt >= 3000);
    CHECK(dt < 15000);
    CHECK(v.n == base_n);
    CHECK(oc_findings_max_severity(&v) == base_sev);
    for (size_t i = 0; i < v.n; i++)
        CHECK(v.items[i].triaged == 0);

    /* 8) Duplicate fingerprints and duplicate response ids. Found by
     * profiling the real provider against the pinned eslint corpus:
     * fingerprints deliberately exclude line numbers, so sibling
     * findings (same rule, file, anchor) used to share one identity and
     * the apply loop's first-match-break left every sibling but the
     * first unranked, while a repeated id overwrote earlier ranks and
     * inflated ranked_count with response entries. The fixture echoes
     * every item and repeats the first id. */
    oc_finding_vec dups;
    oc_findings_init(&dups);
    /* Two findings with an identical fingerprint: same rule, file,
     * anchor and fp_text -- the deliberate byte-identical-siblings
     * identity. Messages differ; the fingerprint does not fold them. */
    push(&dups, &a, "OC090", "src/dup.js", "dupFn", "first sibling finding", NULL);
    push(&dups, &a, "OC090", "src/dup.js", "dupFn", "second sibling finding", NULL);
    push(&dups, &a, "OC002", "src/solo.js", NULL, "TODO marker: solo", "todo solo");
    CHECK_STR_EQ(dups.items[0].fingerprint, dups.items[1].fingerprint);
    setenv("OC_OMNIDECIDE_BIN", OC_TRIAGE_FIXTURES "/dup_echo.py", 1);
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&dups, &out, &st) == OC_OK);
    CHECK(out.ranked == 1);
    CHECK_STR_EQ(out.model, "dup-mock/0");
    /* All three distinct findings annotated; the response carried four
     * entries (two sibling ids are the same fingerprint, first id is
     * repeated), so ranked_count must be 3, never 4. */
    CHECK(out.ranked_count == 3);
    CHECK(dups.items[0].triaged == 1);
    CHECK(dups.items[1].triaged == 1);
    /* Siblings share every rank entry: both end at the same values. */
    CHECK(dups.items[0].triage_rank == dups.items[1].triage_rank);
    CHECK(dups.items[0].triage_confidence == dups.items[1].triage_confidence);
    CHECK(dups.items[0].triage_rank >= 0);
    /* The repeated first id: last entry wins, deterministically. */
    CHECK(dups.items[0].triage_rank == 7);
    CHECK(dups.items[0].triage_confidence == 0.25);
    CHECK(dups.items[2].triaged == 1);
    CHECK(dups.items[2].triage_rank == 2);
    CHECK(dups.n == 3);
    CHECK(oc_findings_max_severity(&dups) == OC_SEV_WARN);
    oc_findings_destroy(&dups);

    oc_findings_destroy(&v);
    oc_findings_destroy(&empty);
    unsetenv("OC_OMNIDECIDE_BIN");
    unsetenv("OC_OMNIDECIDE_TIMEOUT_S");
    oc_arena_destroy(&a);
    TEST_SUMMARY();
}
