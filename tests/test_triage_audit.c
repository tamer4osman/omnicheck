/* Audit-channel tests for the vendored OmniDecide-C provider (memo L1/L2/L6
 * adoption, docs/OMNIDECIDE_LEVERAGE.md).
 *
 * Everything here is post-hoc audit surface: it must be able to REFUSE
 * (fail closed) because it never feeds the pipeline, and the pipeline
 * bytes must never change because of it. Three sections:
 *
 *   A) explain auditor (oc-omnidecide-explain): the explained probabilities
 *      agree with the provider's ranked confidences (4-decimal, EXPLAIN.md
 *      section 4 exactness contract); the check mode exits 2 on tampered,
 *      duplicate, unknown-id, mangled-confidence or model-mismatched
 *      responses and on garbage input; an empty (fail-open) response
 *      audits as "nothing to check".
 *   B) run-artifacts channel (OC_OMNIDECIDE_ARTIFACTS_DIR, default OFF):
 *      stdout is byte-identical with the channel on and off (the artifact
 *      is documentation, never an input); the two pin files appear, the
 *      request pin is byte-exact; a broken artifacts path cannot break the
 *      provider; the e2e report is byte-identical too.
 *   C) replay: re-running the provider on the captured request pin
 *      re-derives the response byte-for-byte (the L6 audit trail).
 */
#include "testkit.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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
#ifndef OC_EXPLAIN_BIN
#error "OC_EXPLAIN_BIN must point at oc-omnidecide-explain"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif
#ifndef OC_TRIAGE_CORPUS
#error "OC_TRIAGE_CORPUS must point at the pinned scan corpus"
#endif
#ifndef OC_TEST_TMPDIR
#error "OC_TEST_TMPDIR must point at a scratch directory"
#endif

#define RESP_CAP 65536

/* Run a protocol-v0 binary with `req` on stdin. extra_env may be NULL or
 * ("VAR", "value"); the child's stdout is captured (until EOF), its
 * stderr goes to the parent's stderr only on request. Returns the exit
 * status via WEXITSTATUS, or -1 on spawn/pipe failure. */
static int run_protocol_bin(const char *bin, const char *req, const char *env_var,
                            const char *env_val, char *resp, size_t cap) {
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
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, 2);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (env_var)
            setenv(env_var, env_val ? env_val : "", 1);
        execl(bin, bin, (char *)NULL);
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
            break;
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
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* The auditor: request on stdin, optional response file as argv[1].
 * Output is discarded; the exit code is the contract (0 or 2). */
static int run_explain(const char *req, const char *resp_file) {
    int in_pipe[2];
    if (pipe(in_pipe) != 0)
        return -1;
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        dup2(in_pipe[0], 0);
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, 1);
        dup2(devnull, 2);
        close(in_pipe[0]);
        close(in_pipe[1]);
        if (resp_file)
            execl(OC_EXPLAIN_BIN, OC_EXPLAIN_BIN, resp_file, (char *)NULL);
        else
            execl(OC_EXPLAIN_BIN, OC_EXPLAIN_BIN, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]);
    size_t req_len = strlen(req);
    const char *p = req;
    while (req_len > 0) {
        ssize_t w = write(in_pipe[1], p, req_len);
        if (w <= 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        p += w;
        req_len -= (size_t)w;
    }
    close(in_pipe[1]);
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Run `oc check <corpus>`; with_triage selects the double opt-in; art_dir
 * (NULL = unset) drives the artifacts env through the spawned provider. */
static int run_oc_check(const char *out_path, int with_triage, const char *art_dir) {
    char cmd[5 * 4096];
    if (with_triage)
        setenv("OC_OMNIDECIDE_BIN", OC_PROVIDER_BIN, 1);
    else
        unsetenv("OC_OMNIDECIDE_BIN");
    if (art_dir)
        setenv("OC_OMNIDECIDE_ARTIFACTS_DIR", art_dir, 1);
    else
        unsetenv("OC_OMNIDECIDE_ARTIFACTS_DIR");
    snprintf(cmd, sizeof(cmd), "'%s' check '%s' --packs-dir '%s/packs' %s> '%s' 2>/dev/null",
             OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, with_triage ? "--triage " : "", out_path);
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

static void write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fwrite(data, 1, len, f) == len);
    CHECK(fclose(f) == 0);
}

/* First entry in the directory whose name starts with the prefix. */
static int dir_has_prefix(const char *dir, const char *prefix) {
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, prefix, strlen(prefix)) == 0) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

/* The artifacts channel keeps append-only history (one pin pair per run,
 * pid-named). Tests that inspect THEIR OWN run's pin must start from an
 * empty directory, or a stale pin from a previous run/corpus could be
 * picked up instead. */
static void wipe_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    char path[1024];
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        remove(path);
    }
    closedir(d);
}

/* Insert `ins` before the last ']' of `src` (the ranks-array closer) into
 * a malloc'd buffer returned to the caller. */
static char *insert_before_last_bracket(const char *src, const char *ins) {
    size_t n = strlen(src);
    const char *brk = strrchr(src, ']');
    if (!brk)
        return NULL;
    size_t at = (size_t)(brk - src);
    size_t il = strlen(ins);
    char *out = malloc(n + il + 1);
    if (!out)
        return NULL;
    memcpy(out, src, at);
    memcpy(out + at, ins, il);
    memcpy(out + at + il, src + at, n - at);
    out[n + il] = '\0';
    return out;
}

/* Replace the first `"confidence":<tok>` token's numeric text with a value
 * guaranteed to differ at 4 decimals. Returns a malloc'd copy or NULL. */
static char *mangle_first_confidence(const char *src) {
    const char *needle = "\"confidence\":";
    const char *p = strstr(src, needle);
    if (!p)
        return NULL;
    p += strlen(needle);
    double v = strtod(p, NULL);
    double nv = (v + 0.0333 <= 1.0) ? v + 0.0333 : v - 0.0333;
    char oldtok[32], newtok[32];
    /* the provider prints %.4f; reconstruct the exact old token */
    snprintf(oldtok, sizeof(oldtok), "%.4f", v);
    snprintf(newtok, sizeof(newtok), "%.4f", nv);
    const char *tok = strstr(p, oldtok);
    if (!tok || tok > p + 16)
        return NULL;
    size_t pre = (size_t)(tok - src);
    size_t ol = strlen(oldtok), nl = strlen(newtok);
    size_t n = strlen(src);
    char *out = malloc(n - ol + nl + 1);
    if (!out)
        return NULL;
    memcpy(out, src, pre);
    memcpy(out + pre, newtok, nl);
    memcpy(out + pre + nl, tok + ol, n - pre - ol);
    out[n - ol + nl] = '\0';
    return out;
}

int main(void) {
    alarm(180); /* gate safety: never hang a cell */
    signal(SIGPIPE, SIG_IGN);
    unsetenv("OC_OMNIDECIDE_BIN");
    unsetenv("OC_OMNIDECIDE_TIMEOUT_S");
    unsetenv("OC_OMNIDECIDE_ARTIFACTS_DIR");

    const char *provider = OC_PROVIDER_BIN;
    const char *tmp = OC_TEST_TMPDIR;

    /* ---- A) the explain auditor ---- */
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

    char resp[RESP_CAP];
    CHECK(run_protocol_bin(provider, req, NULL, NULL, resp, sizeof(resp)) == 0);
    CHECK_STR_CONTAINS(resp, "\"ranks\":[");

    /* A1: check mode passes on the honest response */
    char resp_path[512];
    snprintf(resp_path, sizeof(resp_path), "%s/audit_resp.json", tmp);
    write_file(resp_path, resp, strlen(resp));
    CHECK(run_explain(req, resp_path) == 0);

    /* A2: independent join -- every response confidence equals the dumped
     * explain probability at 4 decimals, monotone along rank order */
    char dump[RESP_CAP];
    CHECK(run_protocol_bin(OC_EXPLAIN_BIN, req, NULL, NULL, dump, sizeof(dump)) == 0);
    CHECK_STR_CONTAINS(dump, "\"explains\":[");
    const char *p = resp;
    double prev = 2.0;
    int joined = 0;
    while ((p = strstr(p, "\"id\":\"")) != NULL) {
        p += strlen("\"id\":\"");
        const char *end = strchr(p, '"');
        CHECK(end != NULL);
        if (!end)
            break;
        char id[128];
        size_t ilen = (size_t)(end - p);
        if (ilen >= sizeof(id))
            ilen = sizeof(id) - 1;
        memcpy(id, p, ilen);
        id[ilen] = '\0';
        char prob_needle[160];
        snprintf(prob_needle, sizeof(prob_needle), "\"id\":\"%s\",\"probability\":", id);
        const char *dp = strstr(dump, prob_needle);
        CHECK(dp != NULL);
        if (!dp) {
            p = end;
            continue;
        }
        dp += strlen(prob_needle);
        double prob = strtod(dp, NULL);
        const char *cp = strstr(end, "\"confidence\":");
        CHECK(cp != NULL);
        if (!cp)
            break;
        cp += strlen("\"confidence\":");
        double conf = strtod(cp, NULL);
        char a[32], b[32];
        snprintf(a, sizeof(a), "%.4f", conf);
        snprintf(b, sizeof(b), "%.4f", prob);
        CHECK_STR_EQ(a, b);
        CHECK(conf <= prev + 1e-9); /* monotone along rank order */
        prev = conf;
        joined++;
        p = cp;
    }
    CHECK(joined == 5); /* all five entries (duplicate-text sibling too) */

    /* A3: unknown response id -> refuse */
    char *corrupt =
        insert_before_last_bracket(resp, ",{\"id\":\"ghost\",\"rank\":5,\"confidence\":0.1000}");
    CHECK(corrupt != NULL);
    write_file(resp_path, corrupt, strlen(corrupt));
    CHECK(run_explain(req, resp_path) == 2);
    free(corrupt);

    /* A4: response id repeated more often than the request carries it
     * (sibling multiplicity violation) -> refuse */
    corrupt =
        insert_before_last_bracket(resp, ",{\"id\":\"fp-0\",\"rank\":5,\"confidence\":0.1000}");
    CHECK(corrupt != NULL);
    write_file(resp_path, corrupt, strlen(corrupt));
    CHECK(run_explain(req, resp_path) == 2);
    free(corrupt);

    /* A5: mangled confidence -> refuse */
    corrupt = mangle_first_confidence(resp);
    CHECK(corrupt != NULL);
    write_file(resp_path, corrupt, strlen(corrupt));
    CHECK(run_explain(req, resp_path) == 2);
    free(corrupt);

    /* A6: model mismatch -> refuse (same-length digit splice) */
    corrupt = malloc(strlen(resp) + 1);
    CHECK(corrupt != NULL);
    if (corrupt) {
        strcpy(corrupt, resp);
        const char *mp = strstr(corrupt, "\"model\":\"omnidecide-c/");
        CHECK(mp != NULL);
        if (mp) {
            const char *v = mp + strlen("\"model\":\"omnidecide-c/");
            corrupt[v - corrupt] = '9'; /* 2.0.0 -> 9.0.0: always a mismatch */
        }
        write_file(resp_path, corrupt, strlen(corrupt));
        CHECK(run_explain(req, resp_path) == 2);
        free(corrupt);
    }

    /* A7: empty ranks is a fail-open response: nothing to audit, exit 0 */
    {
        char ver[32] = "?";
        const char *mp = strstr(resp, "\"model\":\"omnidecide-c/");
        CHECK(mp != NULL);
        if (mp) {
            const char *v = mp + strlen("\"model\":\"");
            const char *ve = strchr(v, '"');
            if (ve && (size_t)(ve - v) < sizeof(ver)) {
                size_t vl = (size_t)(ve - v);
                memcpy(ver, v, vl);
                ver[vl] = '\0';
            }
        }
        char empty[256];
        snprintf(empty, sizeof(empty), "{\"model\":\"%s\",\"ranks\":[]}", ver);
        write_file(resp_path, empty, strlen(empty));
        CHECK(run_explain(req, resp_path) == 0);
    }

    /* A8: garbage and empty input -> refuse (fail-closed auditor) */
    CHECK(run_explain("not json at all", NULL) == 2);
    CHECK(run_explain("{\"op\":\"rank\",\"items\":[]}", NULL) == 2);
    CHECK(run_explain(req, "/nonexistent/audit_resp.json") == 2);

    /* A9: the seam's sibling-identity model end to end -- two carriers
     * share one id with byte-identical texts (src/triage.c documents this
     * as intentional); the provider ranks each carrier, the response
     * carries the id twice with equal confidence, and the auditor accepts */
    char sib_req[2048];
    strcpy(sib_req, "{\"op\":\"rank\",\"items\":["
                    "{\"id\":\"sib\",\"text\":\"TODO marker: rewrite loader\"},"
                    "{\"id\":\"x\",\"text\":\"Function 'bigFn' spans 81 lines\"},"
                    "{\"id\":\"sib\",\"text\":\"TODO marker: rewrite loader\"},"
                    "{\"id\":\"y\",\"text\":\"Potential secret: API key literal "
                    "in source\"}]}");
    char resp_sib[RESP_CAP];
    CHECK(run_protocol_bin(provider, sib_req, NULL, NULL, resp_sib, sizeof(resp_sib)) == 0);
    CHECK_STR_CONTAINS(resp_sib, "\"id\":\"sib\",\"rank\":0");
    CHECK_STR_CONTAINS(resp_sib, "\"id\":\"sib\",\"rank\":1");
    write_file(resp_path, resp_sib, strlen(resp_sib));
    CHECK(run_explain(sib_req, resp_path) == 0);

    /* A10: one id behind two DIFFERENT texts is the sibling-identity
     * defect class (fba5242) -- ambiguous join -> refuse */
    char amb_req[2048];
    strcpy(amb_req, "{\"op\":\"rank\",\"items\":["
                    "{\"id\":\"sib\",\"text\":\"TODO marker: rewrite loader\"},"
                    "{\"id\":\"x\",\"text\":\"Function 'bigFn' spans 81 lines\"},"
                    "{\"id\":\"sib\",\"text\":\"FIXME marker: validate input\"},"
                    "{\"id\":\"y\",\"text\":\"Potential secret: API key literal "
                    "in source\"}]}");
    CHECK(run_explain(amb_req, NULL) == 2);

    /* ---- B) the run-artifacts channel (default OFF) ---- */
    char art_on[512], art_missing[512];
    snprintf(art_on, sizeof(art_on), "%s/artifacts_on", tmp);
    snprintf(art_missing, sizeof(art_missing), "%s/no_such/deeper", tmp);
    wipe_dir(art_on); /* this run's pins only, not a stale pair */

    char resp_off[RESP_CAP], resp_on[RESP_CAP], resp_bad[RESP_CAP];
    CHECK(run_protocol_bin(provider, req, NULL, NULL, resp_off, sizeof(resp_off)) == 0);
    CHECK(run_protocol_bin(provider, req, "OC_OMNIDECIDE_ARTIFACTS_DIR", art_on, resp_on,
                           sizeof(resp_on)) == 0);
    /* B1: stdout is byte-identical with the channel on and off */
    CHECK_STR_EQ(resp_off, resp_on);

    /* B2: the two pin files exist and are honest */
    CHECK(dir_has_prefix(art_on, "request-"));
    CHECK(dir_has_prefix(art_on, "triage-"));
    DIR *d = opendir(art_on);
    CHECK(d != NULL);
    char req_pin_path[1024] = "", triage_path[1024] = "";
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strncmp(e->d_name, "request-", 8) == 0)
                snprintf(req_pin_path, sizeof(req_pin_path), "%s/%s", art_on, e->d_name);
            if (strncmp(e->d_name, "triage-", 7) == 0)
                snprintf(triage_path, sizeof(triage_path), "%s/%s", art_on, e->d_name);
        }
        closedir(d);
    }
    CHECK(req_pin_path[0] != '\0');
    CHECK(triage_path[0] != '\0');
    long req_pin_len = 0;
    char *req_pin = read_file(req_pin_path, &req_pin_len);
    CHECK(req_pin != NULL);
    CHECK(req_pin_len == (long)strlen(req));
    CHECK(memcmp(req_pin, req, strlen(req)) == 0); /* byte-exact pin */
    char *triage_art = read_file(triage_path, NULL);
    CHECK(triage_art != NULL);
    CHECK_STR_CONTAINS(triage_art, "\"schema\":\"oc-triage-artifact/1\"");
    CHECK_STR_CONTAINS(triage_art, "\"provider\":\"oc-odk-provider\"");
    CHECK_STR_CONTAINS(triage_art, "\"model\":\"omnidecide-c/");
    CHECK_STR_CONTAINS(triage_art, "\"items\":5");
    CHECK_STR_CONTAINS(triage_art, "\"ranked\":5");
    CHECK_STR_CONTAINS(triage_art, "\"emitted\":5");
    CHECK_STR_CONTAINS(triage_art, "\"explain_agreement\":true");
    CHECK_STR_CONTAINS(triage_art, "documentation only");
    for (long r = 0; r < 5; r++) {
        char needle[32];
        snprintf(needle, sizeof(needle), "\"rank\":%ld", r);
        CHECK_STR_CONTAINS(triage_art, needle);
    }
    CHECK_STR_CONTAINS(triage_art, "\"fused\":");
    CHECK_STR_CONTAINS(triage_art, "\"probability\":");
    CHECK_STR_CONTAINS(triage_art, "\"tf_cosine\":");
    CHECK_STR_CONTAINS(triage_art, "\"directional\":");
    /* the artifact carries the same confidences as the stdout response */
    {
        const char *ap = triage_art;
        int pairs = 0;
        while ((ap = strstr(ap, "\"id\":\"")) != NULL) {
            ap += strlen("\"id\":\"");
            const char *ae = strchr(ap, '"');
            CHECK(ae != NULL);
            if (!ae)
                break;
            char id[128];
            size_t ilen = (size_t)(ae - ap);
            if (ilen >= sizeof(id))
                ilen = sizeof(id) - 1;
            memcpy(id, ap, ilen);
            id[ilen] = '\0';
            char needle[160];
            snprintf(needle, sizeof(needle), "\"id\":\"%s\",\"rank\":", id);
            const char *rp3 = strstr(resp_on, needle);
            CHECK(rp3 != NULL);
            if (!rp3)
                break;
            const char *ac = strstr(ae, "\"confidence\":");
            const char *rc = strstr(rp3, "\"confidence\":");
            CHECK(ac != NULL && rc != NULL);
            if (!ac || !rc)
                break;
            ac += strlen("\"confidence\":");
            rc += strlen("\"confidence\":");
            char a[32], b[32];
            snprintf(a, sizeof(a), "%.4f", strtod(ac, NULL));
            snprintf(b, sizeof(b), "%.4f", strtod(rc, NULL));
            CHECK_STR_EQ(a, b);
            pairs++;
            ap = ae;
        }
        CHECK(pairs == 5);
    }

    /* B3: a broken artifacts path cannot break the provider, and the
     * stdout response stays byte-identical (best-effort by contract) */
    CHECK(run_protocol_bin(provider, req, "OC_OMNIDECIDE_ARTIFACTS_DIR", art_missing, resp_bad,
                           sizeof(resp_bad)) == 0);
    CHECK_STR_EQ(resp_off, resp_bad);
    CHECK(dir_has_prefix(art_missing, "request-") == 0);

    /* B4: e2e -- `oc check` with the channel on produces the same report
     * bytes as with it off, and the artifacts still appear */
    char path_ranked[512], path_ranked_art[512];
    snprintf(path_ranked, sizeof(path_ranked), "%s/audit_ranked.json", tmp);
    snprintf(path_ranked_art, sizeof(path_ranked_art), "%s/audit_ranked_art.json", tmp);
    int rc_ranked = run_oc_check(path_ranked, 1, NULL);
    int rc_ranked_art = run_oc_check(path_ranked_art, 1, art_on);
    CHECK(rc_ranked == rc_ranked_art);
    CHECK(rc_ranked == 1); /* corpus has findings */
    long len_ranked = 0, len_ranked_art = 0;
    char *rep_ranked = read_file(path_ranked, &len_ranked);
    char *rep_ranked_art = read_file(path_ranked_art, &len_ranked_art);
    CHECK(rep_ranked && rep_ranked_art);
    CHECK(len_ranked == len_ranked_art);
    CHECK(memcmp(rep_ranked, rep_ranked_art, (size_t)len_ranked) == 0);
    CHECK_STR_CONTAINS(rep_ranked_art, "\"triage\":{");
    CHECK(dir_has_prefix(art_on, "request-"));
    CHECK(dir_has_prefix(art_on, "triage-"));
    free(rep_ranked);
    free(rep_ranked_art);

    /* ---- C) replay from the request pin ---- */
    char resp_replay[RESP_CAP];
    CHECK(run_protocol_bin(provider, req_pin, NULL, NULL, resp_replay, sizeof(resp_replay)) == 0);
    CHECK_STR_EQ(resp_on, resp_replay); /* byte-identical re-derivation */

    free(req_pin);
    free(triage_art);
    TEST_SUMMARY();
}
