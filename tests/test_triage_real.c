/* Real-provider triage tests (vendored OmniDecide-C kernel, decision L8).
 *
 * Everything here exercises the REAL provider binary
 * (oc-omnidecide-provider, linked against the vendored kernel) through the
 * same seams production uses: the protocol-v0 spawn, and `oc check` with
 * OC_OMNIDECIDE_BIN set. The mock-based contract tests live in
 * test_triage.c and stay provider-agnostic; this suite proves the vendored
 * kernel end to end:
 *
 *   A) direct protocol request: ranks all items, sibling duplicate texts
 *      resolve to distinct ids, calibrated confidence in [0,1], response
 *      byte-identical on rerun;
 *   B) e2e `oc check` with the provider: findings untouched (same ids in
 *      the same order, same exit code -- rank-only), annotations present
 *      (provider id, model, ranked_count, guardrail string), and the whole
 *      report byte-identical on rerun;
 *
 * Fail-open hostile cases (missing/slow/hanging providers) are the mock
 * suite's jurisdiction and are NOT re-tested here.
 */
#include "oc/triage.h"
#include "testkit.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
#error "OC_TRIAGE_CORPUS must point at the pinned scan corpus"
#endif

#define RESP_CAP 65536

/* Spawn the provider, push ONE protocol-v0 request, slurp the ONE response
 * line (until EOF), reap the child. SIGPIPE is ignored for the interaction
 * (the seam's own discipline) so an early provider exit cannot kill us. */
static int run_provider(const char *provider, const char *req, char *resp, size_t cap) {
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0)
        return -1;
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        dup2(in_pipe[0], 0);
        dup2(out_pipe[1], 1);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execl(provider, provider, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    sigset_t pipe_sig, old;
    sigemptyset(&pipe_sig);
    sigaddset(&pipe_sig, SIGPIPE);
    sigprocmask(SIG_BLOCK, &pipe_sig, &old);
    size_t req_len = strlen(req);
    const char *p = req;
    while (req_len > 0) {
        ssize_t w = write(in_pipe[1], p, req_len);
        if (w <= 0) {
            if (errno == EINTR)
                continue;
            break; /* provider died early: read side will see EOF */
        }
        p += w;
        req_len -= (size_t)w;
    }
    close(in_pipe[1]);
    size_t got = 0;
    for (;;) {
        if (got + 1 >= cap)
            break;
        ssize_t r = read(out_pipe[0], resp + got, cap - 1 - got);
        if (r <= 0) {
            if (r < 0 && errno == EINTR)
                continue;
            break;
        }
        got += (size_t)r;
    }
    close(out_pipe[0]);
    sigprocmask(SIG_SETMASK, &old, NULL);
    int st = 0;
    waitpid(pid, &st, 0);
    resp[got] = '\0';
    return (got > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

/* Run `oc check <corpus>` redirecting the JSON report to out_path.
 *   mode 0: --triage + provider env  -> the real-ranked run
 *   mode 1: --triage, provider unset -> real fail-open through the CLI
 *   mode 2: provider env, NO --triage -> documents the double opt-in
 * Returns the CLI exit code (via system()). */
static int run_oc_check(const char *out_path, int mode) {
    char cmd[5 * 4096];
    if (mode == 0)
        setenv("OC_OMNIDECIDE_BIN", OC_PROVIDER_BIN, 1);
    else
        unsetenv("OC_OMNIDECIDE_BIN");
    snprintf(cmd, sizeof(cmd), "'%s' check '%s' --packs-dir '%s/packs' %s> '%s' 2>/dev/null",
             OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, mode == 2 ? "" : "--triage ", out_path);
    int rc = system(cmd);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static char *read_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[len] = '\0';
    if (out_len)
        *out_len = len;
    return buf;
}

/* Ordered value-sequence extractor for `"<key>":"<value>"` fields, in
 * report order. Used with key "fingerprint" on reports: rank-only means
 * the fingerprint sequence is IDENTICAL with and without triage. Part A
 * uses key "id" on provider responses. Returns the count (up to max). */
static size_t extract_ids(const char *json, const char *key, char ids[][128], size_t max) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    size_t n = 0;
    const char *p = json;
    while (n < max && (p = strstr(p, needle)) != NULL) {
        p += strlen(needle);
        const char *end = strchr(p, '"');
        if (!end)
            break;
        size_t len = (size_t)(end - p);
        if (len >= 128)
            len = 127;
        memcpy(ids[n], p, len);
        ids[n][len] = '\0';
        n++;
    }
    return n;
}

/* Count how many times `rank` (0-based) appears as a rank value. */
static int count_rank(const char *resp, long rank) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"rank\":%ld", rank);
    int n = 0;
    const char *p = resp;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

int main(void) {
    alarm(180); /* gate safety: never hang a cell */
    signal(SIGPIPE, SIG_IGN);
    unsetenv("OC_OMNIDECIDE_BIN");
    unsetenv("OC_OMNIDECIDE_TIMEOUT_S");

    const char *provider = OC_PROVIDER_BIN;
    const char *tmp = OC_TEST_TMPDIR;

    /* ---- A) direct protocol: ranks, siblings, confidence, determinism ---- */
    static const char *const texts[] = {
        "Function 'bigFn' spans 81 lines",
        "TODO marker: rewrite loader",
        "FIXME marker: validate input",
        "TODO marker: rewrite loader", /* duplicate text, distinct id */
        "Potential secret: API key literal in source",
    };
    char req[2048];
    strcpy(req, "{\"op\":\"rank\",\"items\":[");
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
        char item[256];
        snprintf(item, sizeof(item), "%s{\"id\":\"fp-%zu\",\"text\":\"%s\"}", i ? "," : "", i,
                 texts[i]);
        strcat(req, item);
    }
    strcat(req, "]}");

    char resp_a[RESP_CAP], resp_b[RESP_CAP];
    CHECK(run_provider(provider, req, resp_a, sizeof(resp_a)) == 0);
    CHECK(run_provider(provider, req, resp_b, sizeof(resp_b)) == 0);
    CHECK_STR_EQ(resp_a, resp_b); /* kernel determinism over the wire */
    CHECK_STR_CONTAINS(resp_a, "\"model\":\"omnidecide-c/");

    char ids[8][128];
    size_t n_ids = extract_ids(resp_a, "id", ids, 8);
    CHECK(n_ids == 5); /* every item ranked: the duplicate-text sibling too */
    for (size_t i = 0; i < 5; i++) {
        char needle[64];
        snprintf(needle, sizeof(needle), "\"fp-%zu\"", i);
        CHECK_STR_CONTAINS(resp_a, needle);
        CHECK(count_rank(resp_a, (long)i) == 1); /* ranks are 0..4, once each */
    }
    /* calibrated confidence: present, parseable, inside [0,1] */
    const char *c = resp_a;
    int conf_n = 0;
    while ((c = strstr(c, "\"confidence\":")) != NULL) {
        double v = strtod(c + strlen("\"confidence\":"), NULL);
        CHECK(v >= 0.0 && v <= 1.0);
        conf_n++;
        c += strlen("\"confidence\":");
    }
    CHECK(conf_n == 5);

    /* ---- B) e2e `oc check`: rank-only annotation, fail-open, opt-in,
     *         byte determinism ---- */
    char path_ranked[512], path_ranked2[512], path_plain[512], path_noopt[512];
    snprintf(path_ranked, sizeof(path_ranked), "%s/triage_real_ranked.json", tmp);
    snprintf(path_ranked2, sizeof(path_ranked2), "%s/triage_real_ranked2.json", tmp);
    snprintf(path_plain, sizeof(path_plain), "%s/triage_real_plain.json", tmp);
    snprintf(path_noopt, sizeof(path_noopt), "%s/triage_real_noopt.json", tmp);

    int rc_ranked = run_oc_check(path_ranked, 0);
    int rc_plain = run_oc_check(path_plain, 1);
    int rc_noopt = run_oc_check(path_noopt, 2);
    CHECK(rc_ranked == rc_plain); /* verdicts unchanged: same exit code */
    CHECK(rc_plain == rc_noopt);  /* triage state never changes the verdict */
    CHECK(rc_ranked == 1);        /* corpus has findings; scan itself clean */

    long len_ranked = 0, len_plain = 0, len_ranked2 = 0;
    char *rep_ranked = read_file(path_ranked, &len_ranked);
    char *rep_plain = read_file(path_plain, &len_plain);
    char *rep_noopt = read_file(path_noopt, NULL);
    CHECK(rep_ranked && rep_plain && rep_noopt);

    /* annotations present in the ranked report only */
    CHECK_STR_CONTAINS(rep_ranked, "\"triage\":{");
    CHECK_STR_CONTAINS(rep_ranked, OC_PROVIDER_BIN);
    CHECK_STR_CONTAINS(rep_ranked, "\"model\":\"omnidecide-c/");
    CHECK_STR_CONTAINS(rep_ranked, "\"ranked\":true");
    CHECK_STR_CONTAINS(rep_ranked, "rank-only; prose-inputs-only; confidence-exposed; fail-open");
    CHECK_STR_CONTAINS(rep_ranked, "\"triage_rank\":");
    CHECK_STR_CONTAINS(rep_ranked, "\"triage_confidence\":");
    CHECK(strstr(rep_plain, "\"triage_rank\":") == NULL);
    CHECK(strstr(rep_plain, "\"triage_confidence\":") == NULL);

    /* real fail-open through the CLI: complete, honest, unranked */
    CHECK_STR_CONTAINS(rep_plain, "\"ranked\":false");
    CHECK_STR_CONTAINS(rep_plain, "OC_OMNIDECIDE_BIN not set");
    /* double opt-in: provider env without --triage is NOT triage */
    CHECK_STR_CONTAINS(rep_noopt, "\"ranked\":false");
    CHECK_STR_CONTAINS(rep_noopt, "triage not requested (--triage)");
    CHECK(strstr(rep_noopt, OC_PROVIDER_BIN) == NULL);

    /* rank-only: identical finding sequence, identical count */
    char ids_ranked[64][128], ids_plain[64][128];
    size_t n_ranked = extract_ids(rep_ranked, "fingerprint", ids_ranked, 64);
    size_t n_plain = extract_ids(rep_plain, "fingerprint", ids_plain, 64);
    CHECK(n_ranked == n_plain);
    CHECK(n_ranked > 0);
    for (size_t i = 0; i < n_ranked && i < n_plain; i++)
        CHECK_STR_EQ(ids_ranked[i], ids_plain[i]);

    /* ranked_count is honest: equals the annotated finding count */
    const char *rc_field = strstr(rep_ranked, "\"ranked_count\":");
    CHECK(rc_field != NULL);
    long ranked_count = rc_field ? strtol(rc_field + strlen("\"ranked_count\":"), NULL, 10) : -1;
    CHECK(ranked_count == (long)n_ranked);

    /* byte determinism with the provider active */
    int rc_ranked2 = run_oc_check(path_ranked2, 0);
    CHECK(rc_ranked2 == rc_ranked);
    char *rep_ranked2 = read_file(path_ranked2, &len_ranked2);
    CHECK(rep_ranked2 && len_ranked == len_ranked2);
    CHECK(memcmp(rep_ranked, rep_ranked2, (size_t)len_ranked) == 0);

    free(rep_ranked);
    free(rep_plain);
    free(rep_noopt);
    free(rep_ranked2);
    TEST_SUMMARY();
}
