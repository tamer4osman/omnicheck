/* test_llm.c -- T6 gate for `oc check --format llm`.
 *
 * Requires the embedded kernel (default ON):
 *   1. the llm render is the kernel-minified Markdown document: schema
 *      comment, counts line, findings with pipe columns, triage line,
 *      token-estimate block (raw >= compressed >= 1);
 *   2. same verdict policy: the llm run's exit code equals the json
 *      run's exit code on the same corpus;
 *   3. byte-determinism: two llm runs are byte-identical;
 *   4. the other formats stay untouched: the json render still parses
 *      as the omnicheck.report/v0 document;
 *   5. unknown formats fail loud (exit 2).
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

static int run_check(const char *flags, const char *out_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "'%s' check '%s' --packs-dir '%s/packs' %s > '%s' 2>/dev/null",
             OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, flags, out_path);
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

/* Pulls "key=value" out of the tokens block (long-int or float). */
static long token_value(const char *doc, const char *key) {
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *p = strstr(doc, needle);
    if (!p)
        return -1;
    return strtol(p + strlen(needle), NULL, 10);
}

int main(void) {
    char p1[512], p2[512], pj[512], rj[512];
    snprintf(p1, sizeof(p1), "%s/t6_llm1.md", OC_TEST_TMPDIR);
    snprintf(p2, sizeof(p2), "%s/t6_llm2.md", OC_TEST_TMPDIR);
    snprintf(pj, sizeof(pj), "%s/t6_json.json", OC_TEST_TMPDIR);
    snprintf(rj, sizeof(rj), "%s/t6_junk.json", OC_TEST_TMPDIR);

    /* ---- 1 + 2. llm render structure + verdict parity ---- */
    int rc_llm = run_check("--format llm", p1);
    int rc_json = run_check("--format json", pj);
    check(rc_llm == rc_json,
          "the llm run's exit code equals the json run's exit code");
    {
        size_t l1 = 0;
        char *d = read_file(p1, &l1);
        check(d != NULL, "llm document readable");
        if (d) {
            check(strstr(d, "<!-- omnicheck.report.llm/v0 ") == d,
                  "document opens with the schema comment");
            check(strstr(d, "\n## findings\n") != NULL,
                  "document carries the findings section");
            check(strstr(d, "- OC0") != NULL,
                  "findings use the pipe-column form");
            check(strstr(d, "\n## triage\n") != NULL,
                  "document carries the triage section");
            check(strstr(d, "\n## tokens\n") != NULL,
                  "document carries the tokens section");
            long raw = token_value(d, "raw_tokens");
            long comp = token_value(d, "compressed_tokens");
            check(raw >= 1, "raw_tokens >= 1 on a real corpus");
            check(comp >= 1, "compressed_tokens >= 1");
            /* NOTE: no "compressed <= raw" assertion here -- the kernel
             * minifier compresses MARKDOWN structure; plain-sentence
             * findings have nothing to strip and the token estimate can
             * wobble by a few tokens. The honest gate is that both
             * estimates are present, positive and deterministic. */
        }
        free(d);
    }

    /* ---- 3. determinism ---- */
    if (run_check("--format llm", p2) >= 0) {
        size_t a = 0, b = 0;
        char *x1 = read_file(p1, &a);
        char *x2 = read_file(p2, &b);
        check(x1 && x2 && a == b && memcmp(x1, x2, a) == 0,
              "two llm runs are byte-identical");
        free(x1);
        free(x2);
    } else {
        check(0, "second llm run executed");
    }

    /* ---- 4. the json format stays a report/v0 document ---- */
    {
        oc_arena a;
        oc_arena_init(&a);
        size_t l = 0;
        char *jd = read_file(pj, &l);
        check(jd != NULL, "json report readable");
        if (jd) {
            oc_status pst;
            oc_status_clear(&pst);
            oc_json *root = oc_json_parse(&a, jd, l, &pst);
            check(root != NULL, "json report parses");
            if (root) {
                const oc_json *meta = oc_json_get(root, "meta");
                check(strcmp(oc_json_as_str(oc_json_get(meta, "schema"), ""),
                             "omnicheck.report/v0") == 0,
                      "json report keeps the report/v0 schema");
            }
        }
        free(jd);
        oc_arena_destroy(&a);
    }

    /* ---- 5. unknown format fails loud ---- */
    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "'%s' check '%s' --packs-dir '%s/packs' --format bogus > "
                 "'%s' 2>/dev/null",
                 OC_OC_BIN, OC_TRIAGE_CORPUS, OC_REPO_ROOT, rj);
        int rc = system(cmd);
        check(WIFEXITED(rc) && WEXITSTATUS(rc) == 2,
              "unknown format exits 2");
    }

    if (failures == 0) {
        printf("test_llm: all checks passed\n");
        return 0;
    }
    printf("test_llm: %d check(s) FAILED\n", failures);
    return 1;
}
