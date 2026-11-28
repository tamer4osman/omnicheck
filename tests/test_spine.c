/*
 * test_spine.c -- T5 gate for `oc pin` / `oc replay`.
 *
 * Requires the vendored kernel (default ON in both presets):
 *
 *   1. pin add: evaluates through the real spine, saves atomically,
 *      deterministic ids (p000001, ...); invalid request exits 2;
 *   2. replay: verdict "exact" for the identical kernel build
 *      (bit-identical confidence), exit 0;
 *   3. mismatch gate: a store whose recorded decision differs from the
 *      re-derivation gets verdict "mismatch" and exit 1;
 *   4. C3 fail loud: a tampered schema fingerprint refuses replay with
 *      exit 2 (never a silent wrong answer);
 *   5. foreign pins report recorded lineage only, exit 0;
 *   6. list is deterministic; unknown ids exit 2; corrupt store exits 2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef OC_OC_BIN
#define OC_OC_BIN "oc"
#endif
#ifndef OC_REPO_ROOT
#define OC_REPO_ROOT "."
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

static int run_oc(char *const args[], char **out, size_t *out_len) {
    size_t cmd_len = strlen(OC_OC_BIN) + 16;
    for (int i = 0; args[i]; i++)
        cmd_len += strlen(args[i]) + 4;
    char *cmd = malloc(cmd_len);
    if (!cmd)
        return -1;
    strcpy(cmd, "'" OC_OC_BIN "'");
    for (int i = 0; args[i]; i++) {
        strcat(cmd, " '");
        strcat(cmd, args[i]);
        strcat(cmd, "'");
    }
    strcat(cmd, " 2>&1");
    FILE *p = popen(cmd, "r");
    free(cmd);
    if (!p)
        return -1;
    size_t cap = 64 * 1024, n = 0;
    char *buf = malloc(cap);
    if (!buf) {
        pclose(p);
        return -1;
    }
    size_t r;
    while ((r = fread(buf + n, 1, cap - n - 1, p)) > 0) {
        n += r;
        if (n + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                pclose(p);
                return -1;
            }
            buf = nb;
        }
    }
    int rc = pclose(p);
    int code = (rc >= 0 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
    buf[n] = '\0';
    *out = buf;
    *out_len = n;
    return code;
}

static const char REQ[] =
    "{\"primitive\":\"choice\",\"instruction\":\"How should we fix the "
    "outage?\",\"options\":[\"restart the service\",\"reinstall the "
    "operating system\"],\"context\":\"The service stopped responding. "
    "Restarting the service restored normal operation within seconds.\"}";

int main(void) {
    if (chdir(OC_REPO_ROOT) != 0) {
        fprintf(stderr, "FAIL: cannot chdir to %s\n", OC_REPO_ROOT);
        return 1;
    }
    char bf[512];
    snprintf(bf, sizeof(bf), "%s/t5_spine.json", OC_TEST_TMPDIR);
    remove(bf);

    /* ---- 1: pin add (twice, deterministic ids) ---- */
    {
        char rq[2048];
        strcpy(rq, REQ);
        char *args[] = {"pin",   "add",     "--request", rq,
                        "--spine-file", bf, NULL};
        char *out = NULL;
        size_t len = 0;
        int rc = run_oc(args, &out, &len);
        check(rc == 0 && out && strstr(out, "\"id\":\"p000001\"") != NULL &&
                  strstr(out, "\"saved\":true") != NULL,
              "first pin gets id p000001 and saves");
        free(out);
        out = NULL;
        rc = run_oc(args, &out, &len);
        check(rc == 0 && out && strstr(out, "\"id\":\"p000002\"") != NULL,
              "second pin gets id p000002 (append-only)");
        free(out);
        /* invalid request */
        char *bad[] = {"pin", "add", "--request", "{\"primitive\":\"nope\"}",
                       "--spine-file", bf, NULL};
        out = NULL;
        check(run_oc(bad, &out, &len) == 2, "invalid pin request exits 2");
        free(out);
        char *noclass[] = {"pin", "add", "--request", rq, "--spine-file", bf,
                           "--class", "sometimes", NULL};
        out = NULL;
        check(run_oc(noclass, &out, &len) == 2, "bad --class exits 2");
        free(out);
    }

    /* ---- 2: replay exact ---- */
    {
        char *args[] = {"replay", "--id", "p000001", "--spine-file", bf, NULL};
        char *out = NULL;
        size_t len = 0;
        int rc = run_oc(args, &out, &len);
        check(rc == 0 && out && strstr(out, "\"verdict\":\"exact\"") != NULL,
              "same-kernel replay is exact (exit 0)");
        check(out && strstr(out, "\"schema_fp_matches\":true") != NULL,
              "schema fingerprint matches on replay");
        free(out);
    }

    /* ---- 3: mismatch gate (tampered decision) ---- */
    {
        FILE *f = fopen(bf, "r+b");
        char *data = malloc(64 * 1024);
        size_t n = fread(data, 1, 64 * 1024 - 1, f);
        data[n] = '\0';
        /* flip the FIRST pin's recorded decision to the other option */
        char *dec = strstr(data, "\"decision\":\"");
        const char *other = "reinstall the operating system";
        if (dec) {
            dec += strlen("\"decision\":\"");
            /* overwrite with the other option's text (shorter), keep JSON valid */
            size_t old_len = strlen("restart the service");
            memmove(dec + strlen(other), dec + old_len,
                    strlen(dec + old_len) + 1);
            memcpy(dec, other, strlen(other));
            /* re-pad the id so the store stays parseable */
        }
        rewind(f);
        fwrite(data, 1, strlen(data), f);
        fclose(f);
        free(data);
        char *args[] = {"replay", "--id", "p000001", "--spine-file", bf, NULL};
        char *out = NULL;
        size_t len = 0;
        int rc = run_oc(args, &out, &len);
        check(rc == 1 && out && strstr(out, "\"verdict\":\"mismatch\"") != NULL,
              "changed decision replays as mismatch with exit 1 (gate)");
        free(out);
    }

    /* ---- 4: C3 schema fingerprint fail loud ---- */
    {
        FILE *f = fopen(bf, "r+b");
        char *data = malloc(64 * 1024);
        size_t n = fread(data, 1, 64 * 1024 - 1, f);
        data[n] = '\0';
        char *fp = strstr(data, "\"schema_fp\":\"");
        if (fp) {
            fp += strlen("\"schema_fp\":\"");
            /* overwrite the fingerprint with a foreign one */
            for (size_t i = 0; i < 8; i++)
                fp[i] = "deadbeef"[i];
        }
        rewind(f);
        fwrite(data, 1, strlen(data), f);
        fclose(f);
        free(data);
        /* p000001 carries the tampered fingerprint (and, from section 3,
         * a tampered decision) -- the fingerprint gate must fire BEFORE
         * any re-derivation. */
        char *args[] = {"replay", "--id", "p000001", "--spine-file", bf, NULL};
        char *out = NULL;
        size_t len = 0;
        int rc = run_oc(args, &out, &len);
        check(rc == 2, "fingerprint mismatch refuses replay (exit 2, fail loud)");
        check(out && strstr(out, "schema fingerprint mismatch") != NULL,
              "refusal names the fingerprint contract");
        free(out);
    }

    /* ---- 5: foreign pin ---- */
    {
        char rq[2048];
        strcpy(rq, REQ);
        char bf2[512];
        snprintf(bf2, sizeof(bf2), "%s/t5_spine2.json", OC_TEST_TMPDIR);
        remove(bf2);
        char *args[] = {"pin",  "add",    "--request", rq,
                        "--class", "foreign", "--spine-file", bf2, NULL};
        char *out = NULL;
        size_t len = 0;
        int rc = run_oc(args, &out, &len);
        check(rc == 0, "foreign pin records");
        free(out);
        char *rargs[] = {"replay", "--id", "p000001", "--spine-file", bf2,
                         NULL};
        out = NULL;
        rc = run_oc(rargs, &out, &len);
        check(rc == 0 && out && strstr(out, "\"verdict\":\"foreign\"") != NULL,
              "foreign pin replays as recorded lineage only (exit 0)");
        free(out);
    }

    /* ---- 6: list determinism + unknown id + corrupt store ---- */
    {
        char *args[] = {"pin", "list", "--spine-file", bf, NULL};
        char *out = NULL;
        size_t len = 0;
        int rc = run_oc(args, &out, &len);
        check(rc == 0 && out && strstr(out, "\"id\":\"p000002\"") != NULL,
              "list contains the second pin");
        char *out2 = NULL;
        size_t len2 = 0;
        run_oc(args, &out2, &len2);
        check(len == len2 && memcmp(out, out2, len) == 0,
              "list is deterministic");
        free(out);
        free(out2);
        char *unk[] = {"replay", "--id", "p999999", "--spine-file", bf, NULL};
        out = NULL;
        check(run_oc(unk, &out, &len) == 2, "unknown pin id exits 2");
        free(out);
        /* corrupt the store */
        FILE *f = fopen(bf, "wb");
        fputs("{\"schema\":\"omnicheck.spine/v0\",\"pins\":[broken", f);
        fclose(f);
        char *cor[] = {"pin", "list", "--spine-file", bf, NULL};
        out = NULL;
        check(run_oc(cor, &out, &len) == 2, "corrupt store fails loud (exit 2)");
        check(out && strstr(out, "corrupt") != NULL,
              "corrupt refusal names the store");
        free(out);
        char *miss[] = {"replay", "--id", "p000001", "--spine-file",
                        "/nonexistent-t5.json", NULL};
        out = NULL;
        check(run_oc(miss, &out, &len) == 2,
              "replay with no store exits 2 with guidance");
        free(out);
    }

    if (failures == 0) {
        printf("test_spine: all checks passed\n");
        return 0;
    }
    printf("test_spine: %d check(s) FAILED\n", failures);
    return 1;
}
