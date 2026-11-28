/*
 * test_triage_embed.c -- T2 protocol-equality gate (C1).
 *
 * Asserts that the embedded OmniDecide provider (src/triage_embed.c,
 * compiled into lib oc when OC_EMBED_OMNIDECIDE=ON) and the sidecar shim
 * (oc-omnidecide-provider, tools/omnidecide/oc_odk_provider.c) speak
 * BYTE-IDENTICAL protocol-v0 responses for the same request bytes:
 *
 *   1. success path: same bytes for a 4-item rank request (byte compare,
 *      including the trailing newline);
 *   2. success path: same bytes for a 200-item rank request;
 *   3. embedded determinism: two calls, byte-identical responses;
 *   4. provider label: "embedded:omnidecide-<version>";
 *   5. failure-path equality: for malformed JSON, an empty items array,
 *      a single-item request (the rank primitive needs >= 2 options),
 *      items missing text and a non-rank op, the sidecar prints NOTHING
 *      and the embedded provider returns 0 (fail-open) -- failure is
 *      observable and identical on both paths;
 *   6. named reasons: every embedded fail-open carries a non-empty,
 *      human-readable reason.
 *
 * argv[1] (or OC_PROVIDER_BIN) is the path to the built sidecar shim, so
 * the test exercises the exact binaries users run.
 */
#include "oc/triage_embed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OC_PROVIDER_BIN
#define OC_PROVIDER_BIN "oc-omnidecide-provider"
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

/* Runs the sidecar shim on the request bytes and returns its full stdout
 * (malloc'd, NUL-terminated; *len excludes the NUL). NULL on exec loss. */
static char *run_provider(const char *bin, const char *req, size_t *len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/t2_request.json", OC_TEST_TMPDIR);
    FILE *f = fopen(path, "wb");
    if (!f)
        return NULL;
    if (fwrite(req, 1, strlen(req), f) != strlen(req)) {
        fclose(f);
        return NULL;
    }
    fclose(f);

    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "'%s' < '%s'", bin, path);
    FILE *p = popen(cmd, "r");
    if (!p)
        return NULL;
    size_t cap = 4096, n = 0;
    char *out = malloc(cap);
    if (!out) {
        pclose(p);
        return NULL;
    }
    size_t r;
    while ((r = fread(out + n, 1, cap - n - 1, p)) > 0) {
        n += r;
        if (n + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(out, cap);
            if (!nb) {
                free(out);
                pclose(p);
                return NULL;
            }
            out = nb;
        }
    }
    int rc = pclose(p);
    if (rc != 0) {
        free(out);
        return NULL;
    }
    out[n] = '\0';
    *len = n;
    return out;
}

/* Calls the embedded provider and returns its response (malloc'd) or NULL
 * on fail-open (reason copied into why). */
static char *run_embedded(const char *req, char *why, size_t why_sz) {
    char *resp = NULL;
    size_t resp_len = 0;
    int ok = oc_triage_embed_respond(req, strlen(req), &resp, &resp_len, why, why_sz);
    if (!ok)
        return NULL;
    resp[resp_len] = '\0'; /* responder already NUL-terminates; belt+braces */
    return resp;
}

int main(int argc, char **argv) {
    const char *bin = (argc > 1) ? argv[1] : OC_PROVIDER_BIN;

    /* ---- 1. success path: 4 fixed items, byte equality ---- */
    const char *req4 =
        "{\"op\":\"rank\",\"items\":["
        "{\"id\":\"fp-a\",\"text\":\"SQL query built by string concatenation with user input\"},"
        "{\"id\":\"fp-b\",\"text\":\"TODO: refactor this function later\"},"
        "{\"id\":\"fp-c\",\"text\":\"hardcoded credentials in source file\"},"
        "{\"id\":\"fp-d\",\"text\":\"unused import statement\"}]}";

    char why[192];
    char *emb = run_embedded(req4, why, sizeof(why));
    check(emb != NULL, "embedded responds to a valid 4-item request");
    if (emb) {
        size_t prov_len = 0;
        char *prov = run_provider(bin, req4, &prov_len);
        check(prov != NULL, "provider binary runs");
        if (prov) {
            check(prov_len == strlen(emb), "4-item response lengths match");
            check(prov_len > 0 && memcmp(prov, emb, prov_len) == 0,
                  "4-item response bytes match (sidecar == embedded)");
            free(prov);
        }
        /* ---- 3. embedded determinism ---- */
        char *emb2 = run_embedded(req4, why, sizeof(why));
        check(emb2 != NULL, "second embedded call succeeds");
        if (emb2) {
            check(strcmp(emb, emb2) == 0, "embedded responses are byte-stable across calls");
            free(emb2);
        }
        free(emb);
    }

    /* ---- 2. success path: 200 generated items, byte equality ---- */
    {
        char *big = malloc(200 * 96 + 64);
        check(big != NULL, "big request allocated");
        if (big) {
            strcpy(big, "{\"op\":\"rank\",\"items\":[");
            for (int i = 0; i < 200; i++)
                sprintf(big + strlen(big),
                        "%s{\"id\":\"fp-%03d\",\"text\":\"finding number %d "
                        "severity %d module alpha beta gamma\"}",
                        i ? "," : "", i, i, 100 - (i % 100));
            strcat(big, "]}");
            size_t prov_len = 0;
            char *prov = run_provider(bin, big, &prov_len);
            char *emb_big = run_embedded(big, why, sizeof(why));
            check(prov != NULL && emb_big != NULL, "both providers answer the 200-item request");
            if (prov && emb_big) {
                check(prov_len == strlen(emb_big), "200-item response lengths match");
                check(prov_len > 0 && memcmp(prov, emb_big, prov_len) == 0,
                      "200-item response bytes match (sidecar == embedded)");
            }
            free(prov);
            free(emb_big);
            free(big);
        }
    }

    /* ---- 4. provider label ---- */
    char label[64];
    oc_triage_embed_provider_label(label, sizeof(label));
    check(strncmp(label, "embedded:omnidecide-", 20) == 0,
          "provider label starts with embedded:omnidecide-");

    /* ---- 5/6. failure-path equality matrix ---- */
    static const struct {
        const char *name;
        const char *req;
    } neg[] = {
        {"malformed JSON", "{\"op\":\"rank\""},
        {"empty items", "{\"op\":\"rank\",\"items\":[]}"},
        {"single item (rank needs >= 2 options)",
         "{\"op\":\"rank\",\"items\":[{\"id\":\"fp-a\",\"text\":\"only one option here\"}]}"},
        {"item without text",
         "{\"op\":\"rank\",\"items\":[{\"id\":\"fp-a\"},{\"id\":\"fp-b\",\"text\":\"second\"}]}"},
        {"non-rank op", "{\"op\":\"noul\",\"items\":[]}"},
    };
    for (size_t i = 0; i < sizeof(neg) / sizeof(neg[0]); i++) {
        size_t prov_len = 12345; /* sentinel: must become 0 */
        char *prov = run_provider(bin, neg[i].req, &prov_len);
        check(prov != NULL && prov_len == 0, "provider prints nothing on the fail path");
        free(prov);
        char *r = run_embedded(neg[i].req, why, sizeof(why));
        check(r == NULL, "embedded fail-opens on the fail path");
        check(why[0] != '\0', "embedded fail-open carries a named reason");
        free(r);
    }

    if (failures == 0) {
        printf("test_triage_embed: all checks passed\n");
        return 0;
    }
    printf("test_triage_embed: %d check(s) FAILED\n", failures);
    return 1;
}
