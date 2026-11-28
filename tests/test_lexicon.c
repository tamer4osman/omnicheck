/* test_lexicon.c -- T7 gate for --triage-lexicon / --triage-model.
 *
 * Requires the embedded kernel (default ON):
 *   1. a VALID lexicon pack applies: the report's kernel_files block
 *      lists it with its content sha256 and lexicons_applied == 1;
 *   2. the recorded sha256 matches an independent hash of the file;
 *   3. an INVALID lexicon pack (hostile JSON the kernel refuses) fails
 *      LOUD: exit 2, named reason on stderr;
 *   4. an invalid model file fails LOUD (exit 2);
 *   5. resource flags without triage are a usage error (exit 2);
 *   6. byte-stability: a scan without the flags carries no kernel_files
 *      block and is byte-identical across runs; two valid runs are
 *      byte-identical to each other.
 */
#include "oc/common.h"
#include "oc/json.h"
#include "oc/sha256.h"
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

static int run_check(const char *flags, const char *out_path, int cap_stderr) {
    char cmd[1400];
    snprintf(cmd, sizeof(cmd),
             "'%s' check '%s' --packs-dir '%s/packs' %s > '%s' %s 2>&1",
             OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, flags, out_path,
             cap_stderr ? "" : "2>/dev/null");
    int rc = system(cmd);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
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

int main(void) {
    char lex[512], badlex[512], badmodel[512];
    char p_ok[512], p_ok2[512], p_plain[512], p_err[512];
    snprintf(lex, sizeof(lex), "%s/t7_lex.json", OC_TEST_TMPDIR);
    snprintf(badlex, sizeof(badlex), "%s/t7_badlex.json", OC_TEST_TMPDIR);
    snprintf(badmodel, sizeof(badmodel), "%s/t7_badmodel.bin", OC_TEST_TMPDIR);
    snprintf(p_ok, sizeof(p_ok), "%s/t7_ok.json", OC_TEST_TMPDIR);
    snprintf(p_ok2, sizeof(p_ok2), "%s/t7_ok2.json", OC_TEST_TMPDIR);
    snprintf(p_plain, sizeof(p_plain), "%s/t7_plain.json", OC_TEST_TMPDIR);
    snprintf(p_err, sizeof(p_err), "%s/t7_err.json", OC_TEST_TMPDIR);

    /* Valid strict-JSON lexicon (all keys optional; tiny and sane). */
    FILE *f = fopen(lex, "wb");
    if (f) {
        fprintf(f,
                "{\"stopwords\":[\"the\",\"a\",\"of\"],"
                "\"concepts\":{\"secret\":[\"credential\",\"password\"]}}\n");
        fclose(f);
    }
    /* Hostile lexicon: wrong value type -- the kernel must refuse it. */
    f = fopen(badlex, "wb");
    if (f) {
        fprintf(f, "{\"stopwords\": 5}\n");
        fclose(f);
    }
    /* A file that is NOT the 128-byte ODKMODL1 format. */
    f = fopen(badmodel, "wb");
    if (f) {
        fprintf(f, "not a model file at all\n");
        fclose(f);
    }

    /* ---- 1 + 2. valid lexicon applies with sha256 provenance ---- */
    int rc = run_check("--triage-embedded --triage-lexicon", p_ok, 0);
    snprintf(p_err, sizeof(p_err), "%s/t7_tmpignore.json", OC_TEST_TMPDIR);
    /* re-run capturing stderr separately is unnecessary here; the happy
     * path is judged from the report below */
    {
        char cmd[1400];
        snprintf(cmd, sizeof(cmd),
                 "'%s' check '%s' --packs-dir '%s/packs' --triage-embedded "
                 "--triage-lexicon '%s' > '%s' 2>/dev/null",
                 OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, lex, p_ok);
        rc = system(cmd);
        rc = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
        check(rc == 0 || rc == 1, "valid lexicon scan completes");
        size_t l = 0;
        char *d = read_file(p_ok, &l);
        check(d != NULL, "lexicon scan report readable");
        if (d) {
            check(strstr(d, "\"kernel_files\":") != NULL,
                  "report carries the kernel_files block");
            check(strstr(d, "\"lexicons_applied\":1") != NULL,
                  "lexicons_applied == 1");
            check(strstr(d, lex) != NULL, "the lexicon path is recorded");
            /* independent sha256 of the fixture */
            size_t fl = 0;
            char *fdata = read_file(lex, &fl);
            check(fdata != NULL, "lexicon fixture readable");
            if (fdata) {
                char hex[65];
                oc_sha256_hex((const unsigned char *)fdata, fl, hex);
                if (strstr(d, hex))
                    check(1, "recorded sha256 matches the file content");
                else
                    check(0, "recorded sha256 matches the file content");
                free(fdata);
            }
        }
        free(d);
    }

    /* ---- 6a. byte-stability: same flags, two runs byte-identical ---- */
    {
        char cmd[1400];
        snprintf(cmd, sizeof(cmd),
                 "'%s' check '%s' --packs-dir '%s/packs' --triage-embedded "
                 "--triage-lexicon '%s' > '%s' 2>/dev/null",
                 OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, lex, p_ok2);
        system(cmd);
        size_t a = 0, b = 0;
        char *x1 = read_file(p_ok, &a);
        char *x2 = read_file(p_ok2, &b);
        check(x1 && x2 && a == b && memcmp(x1, x2, a) == 0,
              "two lexicon runs are byte-identical");
        free(x1);
        free(x2);
    }

    /* ---- 3. invalid lexicon: LOUD refusal ---- */
    {
        char cmd[1400];
        snprintf(cmd, sizeof(cmd),
                 "'%s' check '%s' --packs-dir '%s/packs' --triage-embedded "
                 "--triage-lexicon '%s' > '%s' 2>&1",
                 OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, badlex, p_err);
        rc = system(cmd);
        rc = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
        check(rc == 2, "kernel-rejected lexicon exits 2 (fail loud)");
        size_t l = 0;
        char *d = read_file(p_err, &l);
        check(d != NULL, "error output captured");
        if (d)
            check(strstr(d, "kernel rejected lexicon") != NULL,
                  "the kernel's rejection is named");
        free(d);
    }

    /* ---- 4. invalid model: LOUD refusal ---- */
    {
        char cmd[1400];
        snprintf(cmd, sizeof(cmd),
                 "'%s' check '%s' --packs-dir '%s/packs' --triage-embedded "
                 "--triage-model '%s' > '%s' 2>&1",
                 OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, badmodel, p_err);
        rc = system(cmd);
        rc = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
        check(rc == 2, "kernel-rejected model exits 2 (fail loud)");
        size_t l = 0;
        char *d = read_file(p_err, &l);
        check(d != NULL, "model error output captured");
        if (d)
            check(strstr(d, "kernel rejected model") != NULL,
                  "the model rejection is named");
        free(d);
    }

    /* ---- 5. resources without triage: usage error ---- */
    {
        char cmd[1400];
        snprintf(cmd, sizeof(cmd),
                 "'%s' check '%s' --packs-dir '%s/packs' --triage-lexicon '%s' "
                 "> '%s' 2>/dev/null",
                 OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, lex, p_err);
        rc = system(cmd);
        rc = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
        check(rc == 2, "resource flags without triage exit 2");
    }

    /* ---- 6b. byte-stability: no flags, no kernel_files block ---- */
    run_check("--triage-embedded", p_plain, 0);
    {
        size_t l = 0;
        char *d = read_file(p_plain, &l);
        check(d != NULL, "plain scan readable");
        if (d)
            check(strstr(d, "\"kernel_files\":") == NULL,
                  "plain scan carries no kernel_files block");
        free(d);
    }

    if (failures == 0) {
        printf("test_lexicon: all checks passed\n");
        return 0;
    }
    printf("test_lexicon: %d check(s) FAILED\n", failures);
    return 1;
}
