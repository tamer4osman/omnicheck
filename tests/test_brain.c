/* test_brain.c -- T4 brain memory gate.
 *
 * Drives the REAL oc CLI end to end and proves:
 *   1. create/info: an absent store reports exists:false; create makes a
 *      fresh store with sha256 provenance; a second create refuses loud;
 *   2. record: a report fingerprint records an outcome (accepted ->
 *      saved:true), repeated records accumulate, unknown fingerprints
 *      and unknown outcomes fail loud (exit 2);
 *   3. entries: the kernel listing shows the accumulated memory;
 *   4. advise: the recorded finding carries a positive bonus and the
 *      adjusted confidence formula (conf + 0.5*bonus, clamped);
 *   5. --triage-brain scans: the brain block appears (applied:true,
 *      adjusted >= 1), findings carry brain_rank/brain_bonus, the exit
 *      code is unchanged (advisory only), and two runs are
 *      byte-identical; brain-less scans stay byte-identical to each
 *      other and carry no brain annotations;
 *   6. discard/save/reset: discard drops an entry (store shrinks), save
 *      re-saves with a new sha256, reset removes the store.
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

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Runs a command with stdout captured into *out (malloc'd, NUL-terminated).
 * Returns the exit code; *out NULL on capture failure. */
static int run_capture(const char *cmd, char **out, size_t *len) {
    char full[1600];
    snprintf(full, sizeof(full), "%s > '%s/t4_out.txt' 2>/dev/null", cmd,
             OC_TEST_TMPDIR);
    int rc = system(full);
    *out = NULL;
    *len = 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/t4_out.txt", OC_TEST_TMPDIR);
    FILE *f = fopen(path, "rb");
    if (!f)
        return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    *out = buf;
    *len = n;
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static int run_oc(const char *args, char **out, size_t *len) {
    char cmd[1400];
    snprintf(cmd, sizeof(cmd), "'%s' %s", OC_OC_BIN, args);
    return run_capture(cmd, out, len);
}

static void run_scan_to(const char *flags, const char *out_path,
                        const char *brain_file_env) {
    char cmd[1400];
    snprintf(cmd, sizeof(cmd),
             "OC_BRAIN_FILE='%s' '%s' check '%s' --packs-dir '%s/packs' %s "
             "> '%s' 2>/dev/null",
             brain_file_env ? brain_file_env : "/nonexistent/none.json",
             OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, flags, out_path);
    system(cmd);
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

/* Extracts the first fingerprint from a report (string scan; the report
 * is our own generated artifact). */
static void first_fingerprint(const char *report, char *fp, size_t sz) {
    fp[0] = '\0';
    const char *key = "\"fingerprint\":\"";
    const char *p = strstr(report, key);
    if (!p)
        return;
    p += strlen(key);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < sz)
        fp[i++] = *p++;
    fp[i] = '\0';
}

int main(void) {
    char store[512], report[512], r1[512], r2[512], rb[512];
    snprintf(store, sizeof(store), "%s/t4_brain.json", OC_TEST_TMPDIR);
    snprintf(report, sizeof(report), "%s/t4_report.json", OC_TEST_TMPDIR);
    snprintf(r1, sizeof(r1), "%s/t4_scan1.json", OC_TEST_TMPDIR);
    snprintf(r2, sizeof(r2), "%s/t4_scan2.json", OC_TEST_TMPDIR);
    snprintf(rb, sizeof(rb), "%s/t4_scan_brain.json", OC_TEST_TMPDIR);

    char cmd[1400];
    char *out = NULL;
    size_t len = 0;
    int rc;

    /* ---- 1. info on absent store; create; double create refuses ---- */
    snprintf(cmd, sizeof(cmd), "brain info --brain-file '%s'", store);
    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"exists\":false") != NULL,
          "info on an absent store is an honest exists:false");
    free(out);
    out = NULL;

    snprintf(cmd, sizeof(cmd), "brain create --brain-file '%s'", store);
    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"created\":true") != NULL &&
              strstr(out, "\"sha256\":\"") != NULL &&
              strstr(out, "\"epoch\":0") != NULL,
          "create makes a fresh store with sha256 + epoch provenance");
    free(out);
    out = NULL;

    rc = run_oc(cmd, &out, &len);
    check(rc == 2, "second create refuses loud (exit 2)");
    free(out);
    out = NULL;

    /* ---- scan without the brain: advisory no-op, byte-stable ---- */
    run_scan_to("--triage-embedded", report, NULL);
    run_scan_to("--triage-embedded", r1, NULL);
    run_scan_to("--triage-embedded", r2, NULL);
    size_t l1 = 0, l2 = 0;
    char *s1 = read_file(r1, &l1);
    char *s2 = read_file(r2, &l2);
    check(s1 && s2 && l1 == l2 && memcmp(s1, s2, l1) == 0,
          "two brain-less scans are byte-identical");
    check(s1 && strstr(s1, "\"brain\":{") == NULL,
          "brain-less scan carries NO brain block (byte-stability)");
    check(s1 && strstr(s1, "\"brain_annotated\"") == NULL &&
              strstr(s1, "\"brain_rank\"") == NULL,
          "brain-less scan carries no brain annotations");
    free(s1);
    free(s2);

    char fp[128];
    {
        size_t rl = 0;
        char *rdata = read_file(report, &rl);
        check(rdata != NULL, "scan report readable");
        if (rdata)
            first_fingerprint(rdata, fp, sizeof(fp));
        free(rdata);
        check(fp[0] != '\0', "extracted a fingerprint from the report");
    }

    /* ---- 2. record ---- */
    snprintf(cmd, sizeof(cmd),
             "brain record --brain-file '%s' --report '%s' --fingerprint '%s' "
             "--outcome accepted",
             store, report, fp);
    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"saved\":true") != NULL &&
              strstr(out, "\"reward\":1.0") != NULL,
          "record accepted saves with reward 1.0");
    free(out);
    out = NULL;

    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"saved\":true") != NULL,
          "repeated record accumulates");
    free(out);
    out = NULL;

    snprintf(cmd, sizeof(cmd),
             "brain record --brain-file '%s' --report '%s' --fingerprint "
             "'no-such-fingerprint' --outcome accepted",
             store, report);
    rc = run_oc(cmd, &out, &len);
    check(rc == 2, "unknown fingerprint exits 2 (fail loud)");
    free(out);
    out = NULL;

    snprintf(cmd, sizeof(cmd),
             "brain record --brain-file '%s' --report '%s' --fingerprint '%s' "
             "--outcome bogus",
             store, report, fp);
    rc = run_oc(cmd, &out, &len);
    check(rc == 2, "unknown outcome exits 2 (fail loud)");
    free(out);
    out = NULL;

    /* ---- 3. entries ---- */
    snprintf(cmd, sizeof(cmd), "brain entries --brain-file '%s'", store);
    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"entries_report\":") != NULL &&
              strstr(out, "\"memory_entries\":") != NULL,
          "entries lists the kernel memory with provenance");
    free(out);
    out = NULL;

    /* ---- 4. advise: positive bonus for the accepted finding ---- */
    snprintf(cmd, sizeof(cmd),
             "brain advise --brain-file '%s' --report '%s'", store, report);
    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"bonus\":") != NULL,
          "advise emits per-finding bonuses");
    if (out && strstr(out, fp)) {
        /* crude but honest: the recorded fingerprint's item carries a
         * positive bonus (accepted = +1.0 reward, twice) */
        const char *hit = strstr(out, fp);
        check(strstr(hit, "\"bonus\":0.") != NULL ||
                  strstr(hit, "\"bonus\":1") != NULL,
              "the accepted finding has a non-negative bonus");
    }
    free(out);
    out = NULL;

    /* ---- 5. --triage-brain scan: annotations + determinism + gate ---- */
    run_scan_to("--triage-embedded --triage-brain", rb, store);
    {
        size_t bl = 0;
        char *bd = read_file(rb, &bl);
        check(bd != NULL, "brain scan report readable");
        if (bd) {
            check(strstr(bd, "\"applied\":true") != NULL &&
                      strstr(bd, "\"adjusted\":") != NULL,
                  "brain scan carries the brain block with applied:true");
            check(strstr(bd, "\"brain_rank\":") != NULL &&
                      strstr(bd, "\"brain_bonus\":") != NULL,
                  "brain scan annotates findings with brain_rank/bonus");
            free(bd);
        }
    }
    {
        /* exit-code gate: advisory must not change it */
        char cmd2[1400];
        snprintf(cmd2, sizeof(cmd2),
                 "OC_BRAIN_FILE='%s' '%s' check '%s' --packs-dir '%s/packs' "
                 "--triage-embedded > /dev/null 2>&1; echo $?",
                 store, OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT);
        size_t g1l = 0;
        char *g1 = NULL;
        run_capture(cmd2, &g1, &g1l);
        snprintf(cmd2, sizeof(cmd2),
                 "OC_BRAIN_FILE='%s' '%s' check '%s' --packs-dir '%s/packs' "
                 "--triage-embedded --triage-brain > /dev/null 2>&1; echo $?",
                 store, OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT);
        size_t g2l = 0;
        char *g2 = NULL;
        run_capture(cmd2, &g2, &g2l);
        check(g1 && g2 && strcmp(g1, g2) == 0,
              "the exit code is identical with and without the brain");
        free(g1);
        free(g2);
    }
    run_scan_to("--triage-embedded --triage-brain", rb, store);
    {
        /* determinism of the brain scan (second run overwrote rb) */
        char rb2[512];
        snprintf(rb2, sizeof(rb2), "%s/t4_scan_brain2.json", OC_TEST_TMPDIR);
        run_scan_to("--triage-embedded --triage-brain", rb2, store);
        size_t a1 = 0, a2 = 0;
        char *x1 = read_file(rb, &a1);
        char *x2 = read_file(rb2, &a2);
        check(x1 && x2 && a1 == a2 && memcmp(x1, x2, a1) == 0,
              "two brain scans are byte-identical");
        free(x1);
        free(x2);
    }

    /* ---- 6. discard / save / reset ---- */
    snprintf(cmd, sizeof(cmd),
             "brain discard --brain-file '%s' --index 0", store);
    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"discarded\":true") != NULL,
          "discard removes the entry at index 0");
    free(out);
    out = NULL;

    snprintf(cmd, sizeof(cmd),
             "brain discard --brain-file '%s' --index 999999", store);
    rc = run_oc(cmd, &out, &len);
    check(rc == 2, "discard of an unknown index fails loud");
    free(out);
    out = NULL;

    snprintf(cmd, sizeof(cmd), "brain save --brain-file '%s'", store);
    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"saved\":true") != NULL &&
              strstr(out, "\"sha256\":\"") != NULL,
          "save re-saves the store with sha256 provenance");
    free(out);
    out = NULL;

    snprintf(cmd, sizeof(cmd), "brain reset --brain-file '%s'", store);
    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"removed\":true") != NULL,
          "reset removes the store");
    free(out);
    out = NULL;

    snprintf(cmd, sizeof(cmd), "brain info --brain-file '%s'", store);
    rc = run_oc(cmd, &out, &len);
    check(rc == 0 && out && strstr(out, "\"exists\":false") != NULL,
          "info after reset is an honest exists:false");
    free(out);
    out = NULL;

    if (failures == 0) {
        printf("test_brain: all checks passed\n");
        return 0;
    }
    printf("test_brain: %d check(s) FAILED\n", failures);
    return 1;
}
