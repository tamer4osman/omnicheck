/*
 * oc_odk_provider.c — OmniCheck triage protocol-v0 provider, linking the
 * REAL OmniDecide-C kernel (v2.0.0, Apache-2.0).
 *
 * Contract (mirrors include/oc/triage.h in OmniCheck):
 *   stdin : ONE line  {"op":"rank","items":[{"id":"...","text":"..."}, ...]}
 *   stdout: ONE line  {"model":"...","ranks":[{"id":"...","rank":0,
 *                       "confidence":0.42}, ...]}
 *
 * Guardrail compliance:
 *   rank-only     : this process only ever ranks; it cannot drop, reorder
 *                   or re-severity findings (OmniCheck enforces that side).
 *   prose-only    : the only per-finding data used is the message text,
 *                   passed as rank options. The instruction below is a
 *                   fixed provider-side rubric, not per-finding data; the
 *                   context channel is left empty.
 *   conf-exposure : per-item calibrated probability is emitted and
 *                   printed by OmniCheck next to the provider id.
 *   fail-open     : on ANY error this provider prints nothing and exits 0;
 *                   OmniCheck treats empty/invalid output as fail-open and
 *                   the scan proceeds unranked.
 *
 * Response-size discipline (BUGFIX N-4): OmniCheck's client GROWS its
 * read buffer dynamically now (64 MiB cap), so this provider emits the
 * FULL ranked list -- no byte budget, no entry cap beyond a sanity
 * bound -- and rank coverage is complete for every request size. The
 * old 58 KiB emit budget existed only to stay under the client's fixed
 * 64 KiB read buffer; with that truncation removed on both ends, the
 * "best first, capped" discipline is obsolete. Items beyond the sanity
 * bound would still fail-open honestly rather than truncate silently.
 *
 * Optional run-artifacts channel (memo L2, default OFF): when the env var
 * OC_OMNIDECIDE_ARTIFACTS_DIR points at a directory, this provider ALSO
 * writes two audit files per run --
 *   request-<pid>.json  the byte-exact protocol request (the replay pin);
 *   triage-<pid>.json   the FULL ranked list (uncapped, unlike stdout)
 *                       plus the kernel explain components for every
 *                       ranked item, and an explain_agreement flag.
 * Best-effort by contract: every artifact failure is silent, the stdout
 * response above is emitted FIRST and is the ONLY pipeline input; the
 * artifacts are documentation, never an input to the pipeline. Enabling
 * the channel roughly doubles provider compute (the explain pass); the
 * deep-CI budget profile covers that, interactive scans should leave it
 * off. The companion auditor `oc_odk_explain` can verify a captured
 * response against a request (exit 2 on mismatch, fail-CLOSED: auditors
 * may refuse, pipeline components may not).
 *
 * Build: first-class CMake targets `oc-omnidecide-provider` and
 * `oc-omnidecide-explain` when the vendored kernel is present
 * (third_party/omnidecide/, see its PROVENANCE.md):
 * the root CMakeLists builds the `omnidecide` static library from the
 * vendored sources and links this shim against it. Out-of-tree build is
 * still possible for pinned upstream checkouts:
 *   cc -std=c11 -O2 -Wall -Wextra -pedantic oc_odk_provider.c \
 *      -I<omnidecide>/include <omnidecide>/build/libomnidecide.a -lm -o oc_odk_provider
 */
#define _POSIX_C_SOURCE 200809L

#include <omnidecide.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define OC_ODK_INPUT_MAX   (32u * 1024u * 1024u)
/* BUGFIX N-4: emit cap is now a SANITY bound (not a budget): the client
 * reads dynamically, so every ranked item is emitted. 1<<20 items is
 * far beyond any honest request; beyond it the provider fails open. */
#define OC_ODK_EMIT_MAX    (1u << 20)
#define OC_ODK_PATH_MAX    4096u

static const char OC_ODK_INSTRUCTION[] =
    "Rank these static-analysis findings from most to least important "
    "for a developer to address first.";

/* JSON string emitter with defensive escaping (ids are ASCII fingerprints,
 * but never trust the wire). */
static void emit_json_string_to(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\b': fputs("\\b", out); break;
        case '\f': fputs("\\f", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (*p < 0x20)
                fprintf(out, "\\u%04x", *p);
            else
                fputc(*p, out);
        }
    }
    fputc('"', out);
}

/* -------------------- optional run-artifacts channel --------------------
 * Memo L2, default OFF (only active when OC_OMNIDECIDE_ARTIFACTS_DIR is
 * set). Best-effort by contract: any failure is silent and leaves the
 * already-emitted stdout response -- the ONLY pipeline input -- untouched.
 * The artifacts are documentation, never an input to the pipeline. */

static void write_artifacts(const char *dir, const char *req, size_t req_len,
                            odk_kernel *kernel, const char *const *ids,
                            const char *const *texts, size_t m,
                            const odk_ranked_item *ranked, size_t ranked_n,
                            size_t emitted) {
    if (!dir || !dir[0])
        return;
    if (strlen(dir) + 40 > OC_ODK_PATH_MAX)
        return; /* path would truncate: write nothing rather than anywhere */

    mkdir(dir, 0777); /* best effort; EEXIST is fine */
    char path[OC_ODK_PATH_MAX];
    long pid = (long)getpid();

    /* 1) the pin: the byte-exact request (the request_json discipline of
     * the kernel's REPLAY.md, at the provider boundary). */
    snprintf(path, sizeof(path), "%s/request-%ld.json", dir, pid);
    FILE *f = fopen(path, "wb");
    if (f) {
        if (fwrite(req, 1, req_len, f) != req_len || fflush(f) != 0) {
            fclose(f);
            remove(path); /* a partial pin is worse than no pin */
        } else {
            fclose(f);
        }
    }

    /* 2) the FULL ranked list (uncapped, unlike stdout) plus the kernel's
     * explain components for every ranked item. The explain pass runs
     * over the same options with the same instruction; per EXPLAIN.md
     * section 4 it changes no arithmetic and is decision-identical. */
    int explain_ok = 0;
    odk_explain ex;
    memset(&ex, 0, sizeof(ex));
    if (m >= 2) { /* the rank primitive itself needs >= 2 options */
        odk_error eerr;
        memset(&eerr, 0, sizeof(eerr));
        if (odk_explain_choice(kernel, OC_ODK_INSTRUCTION, texts, m, NULL,
                               NULL, &ex, &eerr) == ODK_OK &&
            ex.option_count == m)
            explain_ok = 1;
    }

    /* explain_agreement: every ranked score equals the explained
     * probability at 4 decimals (the EXPLAIN.md exactness contract).
     * Measured and recorded honestly; a disagreement never changes the
     * stdout response, it only marks the artifact for the auditor. */
    int agreement = -1; /* -1: explain unavailable (JSON null) */
    if (explain_ok) {
        agreement = 1;
        for (size_t i = 0; i < ranked_n; i++) {
            size_t idx = ranked[i].index;
            if (idx == (size_t)-1 || idx >= m)
                continue;
            char a[32], b[32];
            snprintf(a, sizeof(a), "%.4f", ranked[i].score);
            snprintf(b, sizeof(b), "%.4f", ex.options[idx].probability);
            if (strcmp(a, b) != 0) {
                agreement = 0;
                break;
            }
        }
    }

    snprintf(path, sizeof(path), "%s/triage-%ld.json", dir, pid);
    f = fopen(path, "wb");
    if (!f) {
        if (explain_ok)
            odk_explain_release(&ex);
        return;
    }
    fprintf(f, "{\"schema\":\"oc-triage-artifact/1\","
               "\"provider\":\"oc-odk-provider\","
               "\"model\":\"omnidecide-c/%d.%d.%d\",\"language\":\"en\","
               "\"items\":%zu,\"ranked\":%zu,\"emitted\":%zu,"
               "\"request_file\":\"request-%ld.json\","
               "\"note\":\"documentation only; never an input to the "
               "pipeline\",\"explain_agreement\":",
           ODK_VERSION_MAJOR, ODK_VERSION_MINOR, ODK_VERSION_PATCH, m,
           ranked_n, emitted, pid);
    if (agreement < 0)
        fputs("null", f);
    else
        fputs(agreement ? "true" : "false", f);
    fputs(",\"ranks\":[", f);
    size_t seen = 0;
    for (size_t i = 0; i < ranked_n; i++) {
        size_t idx = ranked[i].index;
        if (idx == (size_t)-1 || idx >= m || !ids[idx])
            continue;
        if (seen > 0)
            fputc(',', f);
        seen++;
        fputs("{\"id\":", f);
        emit_json_string_to(f, ids[idx]);
        fprintf(f, ",\"rank\":%zu,\"confidence\":%.4f", i, ranked[i].score);
        if (explain_ok) {
            const odk_explain_option *o = &ex.options[idx];
            fprintf(f, ",\"probability\":%.4f,\"fused\":%.17g,"
                       "\"tf_cosine\":%.17g,\"jaccard\":%.17g,"
                       "\"trigram\":%.17g,\"whole\":%.17g,"
                       "\"best_chunk\":%.17g,\"contextual\":%.17g,"
                       "\"directional\":%.17g",
                    o->probability, o->fused, o->tf_cosine, o->jaccard,
                    o->trigram, o->whole, o->best_chunk, o->contextual,
                    o->directional);
        }
        fputc('}', f);
    }
    fputs("]}\n", f);
    if (fflush(f) != 0 || ferror(f)) {
        fclose(f);
        remove(path); /* a truncated artifact is worse than none */
    } else {
        fclose(f);
    }
    if (explain_ok)
        odk_explain_release(&ex);
}

static char *slurp_stdin(size_t *out_len) {
    size_t cap = 64 * 1024, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;
    for (;;) {
        if (len == cap) {
            if (cap >= OC_ODK_INPUT_MAX) {
                free(buf);
                return NULL;
            }
            cap *= 2;
            if (cap > OC_ODK_INPUT_MAX)
                cap = OC_ODK_INPUT_MAX;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                return NULL;
            }
            buf = nb;
        }
        size_t r = fread(buf + len, 1, cap - len, stdin);
        len += r;
        if (r == 0)
            break;
    }
    *out_len = len;
    return buf;
}

int main(void) {
    size_t req_len = 0;
    char *req = slurp_stdin(&req_len);
    if (!req || req_len == 0)
        return 0; /* fail-open: no output */

    odk_error err;
    memset(&err, 0, sizeof(err));
    odk_json_value *root = odk_json_parse(req, req_len, &err);
    if (!root || root->type != ODK_JSON_OBJECT) {
        odk_json_free(root);
        free(req);
        return 0;
    }

    const odk_json_value *items = odk_json_object_get(root, "items");
    if (!items || items->type != ODK_JSON_ARRAY || items->item_count == 0) {
        odk_json_free(root);
        free(req);
        return 0;
    }
    size_t n = items->item_count;

    /* Collect ids and texts in request order; texts become rank options. */
    const char **ids = calloc(n, sizeof(*ids));
    const char **texts = calloc(n, sizeof(*texts));
    if (!ids || !texts) {
        free(ids);
        free(texts);
        odk_json_free(root);
        free(req);
        return 0;
    }
    size_t m = 0; /* usable items */
    for (size_t i = 0; i < n; i++) {
        const odk_json_value *it = items->items[i];
        if (!it || it->type != ODK_JSON_OBJECT)
            continue;
        const odk_json_value *id = odk_json_object_get(it, "id");
        const odk_json_value *tx = odk_json_object_get(it, "text");
        if (!id || id->type != ODK_JSON_STRING || !tx ||
            tx->type != ODK_JSON_STRING)
            continue;
        ids[m] = id->string;
        texts[m] = tx->string;
        m++;
    }
    if (m == 0) {
        free(ids);
        free(texts);
        odk_json_free(root);
        free(req);
        return 0;
    }

    /* English prose findings: the documented reviewed pack, set explicitly
     * (never AUTO) so no detector variance can enter the pipeline. */
    odk_kernel_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.language = ODK_LANG_EN;

    odk_kernel *kernel = odk_kernel_create_ex(&cfg);
    if (!kernel) {
        free(ids);
        free(texts);
        odk_json_free(root);
        free(req);
        return 0;
    }

    odk_result result;
    memset(&result, 0, sizeof(result));
    odk_status st = odk_evaluate_rank(kernel, OC_ODK_INSTRUCTION, texts, m,
                                      NULL, NULL, &result, &err);
    if (st != ODK_OK) {
        odk_kernel_free(kernel);
        free(ids);
        free(texts);
        odk_json_free(root);
        free(req);
        return 0;
    }

    odk_ranked_item *ranked = NULL;
    size_t ranked_n = odk_result_rank_items(&result, texts, m, &ranked);
    if (ranked_n == 0 || !ranked) {
        odk_kernel_free(kernel);
        free(ids);
        free(texts);
        odk_json_free(root);
        free(req);
        return 0;
    }

    /* Emit EVERY ranked entry, best first (BUGFIX N-4: the old byte
     * budget + 2000-entry cap existed to stay under the client's fixed
     * 64 KiB read buffer; with the client reading dynamically, capping
     * here only hid ranks -- 928 of 1,926 on the nodejs/node lib/ run).
     * rank = position in ranked order; confidence = the kernel's
     * calibrated probability. */
    size_t emitted = 0;
    fputs("{\"model\":\"omnidecide-c/", stdout);
    printf("%d.%d.%d\",\"ranks\":[", ODK_VERSION_MAJOR, ODK_VERSION_MINOR,
           ODK_VERSION_PATCH);
    for (size_t i = 0; i < ranked_n && emitted < OC_ODK_EMIT_MAX; i++) {
        size_t idx = ranked[i].index;
        if (idx == (size_t)-1 || idx >= m || !ids[idx])
            continue;
        if (emitted > 0)
            putchar(',');
        fputs("{\"id\":", stdout);
        emit_json_string_to(stdout, ids[idx]);
        printf(",\"rank\":%zu,\"confidence\":%.4f}", i, ranked[i].score);
        emitted++;
    }
    fputs("]}\n", stdout);
    fflush(stdout);

    /* stdout is complete and flushed: the pipeline input is done. Anything
     * the artifacts channel does from here can no longer delay, break or
     * change it. */
    write_artifacts(getenv("OC_OMNIDECIDE_ARTIFACTS_DIR"), req, req_len,
                    kernel, ids, texts, m, ranked, ranked_n, emitted);

    odk_result_rank_items_free(ranked);
    odk_result_release(&result);
    odk_kernel_free(kernel);
    free(ids);
    free(texts);
    odk_json_free(root);
    free(req);
    return 0;
}
