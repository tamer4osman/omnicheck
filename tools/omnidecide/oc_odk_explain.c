/*
 * oc_odk_explain.c — protocol-v0 AUDITOR companion to oc_odk_provider,
 * linked against the vendored OmniDecide-C kernel (v2.0.0, Apache-2.0).
 *
 * This is a post-hoc audit tool, NOT a pipeline component: the pipeline
 * provider may only fail open, while this tool MAY exit 2 on mismatch.
 * Nothing it prints or rejects ever changes an OmniCheck report.
 *
 * Usage:
 *   oc_odk_explain                 (dump mode)
 *     stdin : ONE protocol-v0 request, exactly what OmniCheck writes:
 *             {"op":"rank","items":[{"id":"...","text":"..."}, ...]}
 *     stdout: ONE line
 *             {"model":"omnidecide-c/x.y.z","explains":[
 *               {"id":"...","probability":0.1111,"fused":0.036,...}, ...]}
 *             best-first, ordered like the provider's ranks.
 *
 *   oc_odk_explain RESPONSE_FILE   (check mode: audit a captured response)
 *     stdin : the same request as above;
 *     file  : ONE captured provider response line
 *             {"model":"omnidecide-c/x.y.z","ranks":[{"id":"...","rank":0,
 *              "confidence":0.42}, ...]}
 *     exit 0 when the audit passes, 2 with a diagnostic on stderr when it
 *     fails. Checked (docs/EXPLAIN.md section 4 -- rank and choice share
 *     the fused scores, so the explained probability reproduces the rank's
 *     calibrated probability bit-for-bit, rounded to the same 4 decimals):
 *       1. the response's model equals this build's kernel version;
 *       2. ids follow the seam's sibling-identity model (src/triage.c):
 *          byte-identical siblings may share one fingerprint and appear
 *          once per carrier, but one id behind TWO DIFFERENT texts is the
 *          sibling-identity defect class and is refused, and no response
 *          id may appear more often than the request carries it;
 *       3. every response confidence equals the explained probability at
 *          4 decimals (all carriers of one id share the text, hence the
 *          probability; carriers that disagree are refused);
 *       4. confidences are non-increasing along rank order (softmax is
 *          monotone in fused; ties allowed);
 *       5. rank fields are a sequential 0..E-1 permutation prefix.
 *     An empty ranks list is a fail-open response: nothing to audit,
 *     exit 0 (the pipeline records ranked:false independently).
 *
 * Exactness contract (EXPLAIN.md section 4): the explain capture is
 * threaded through the REAL scoring functions and changes no arithmetic;
 * softmax(6*(fused-0.5)) reproduces the primitive's probability rounded to
 * 4 decimals. Word links (brain) are off in the explain path, matching
 * every no-brain decision path. Read-only by design.
 *
 * Limitations (honest, by design): ids are parsed escape-free; OmniCheck
 * fingerprints are ASCII hex and never need escapes. A response id that
 * does not round-trip is reported as unknown, which is the correct audit
 * verdict for a tampered line anyway.
 *
 * Build (also built as CMake target `oc-omnidecide-explain`):
 *   cc -std=c11 -O2 -Wall -Wextra -pedantic oc_odk_explain.c \
 *      -I<omnidecide>/include <omnidecide>/build/libomnidecide.a -lm \
 *      -o oc_odk_explain
 */
#include <omnidecide.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_MAX (32u * 1024u * 1024u)

static const char INSTRUCTION[] =
    "Rank these static-analysis findings from most to least important "
    "for a developer to address first.";

static char *slurp_stdin(size_t *out_len) {
    size_t cap = 64 * 1024, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;
    for (;;) {
        if (len == cap) {
            if (cap >= INPUT_MAX) {
                free(buf);
                return NULL;
            }
            cap *= 2;
            if (cap > INPUT_MAX)
                cap = INPUT_MAX;
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

static char *slurp_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 64 * 1024, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        if (len == cap) {
            char *nb = realloc(buf, cap * 2);
            if (!nb) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
            cap *= 2;
        }
        size_t r = fread(buf + len, 1, cap - len, f);
        len += r;
        if (r == 0)
            break;
    }
    fclose(f);
    *out_len = len;
    return buf;
}

typedef struct {
    const char **ids; /* unique (checked); point into the request JSON tree */
    const char **texts;
    size_t m;
} req_items;

typedef struct {
    size_t idx;
    double probability;
} prob_row;

static int row_cmp(const void *a, const void *b) {
    const prob_row *x = a, *y = b;
    if (x->probability > y->probability)
        return -1;
    if (x->probability < y->probability)
        return 1;
    if (x->idx < y->idx)
        return -1; /* deterministic tie-break: request order */
    return x->idx > y->idx;
}

/* ---------------------------- dump mode ---------------------------------
 *
 * Ownership: `ri.ids`/`ri.texts` point into the request JSON tree, which
 * stays alive for the whole call and is freed by main() afterwards -- so
 * every early-exit path below is leak-free by construction. */

static int run_dump(const req_items *ri) {
    odk_kernel_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.language = ODK_LANG_EN; /* identical to oc_odk_provider */

    odk_kernel *kernel = odk_kernel_create_ex(&cfg);
    if (!kernel) {
        fprintf(stderr, "explain: kernel create failed\n");
        return 2;
    }

    odk_explain ex;
    memset(&ex, 0, sizeof(ex));
    odk_error err;
    memset(&err, 0, sizeof(err));
    if (odk_explain_choice(kernel, INSTRUCTION, ri->texts, ri->m, NULL, NULL,
                           &ex, &err) != ODK_OK) {
        fprintf(stderr, "explain: odk_explain_choice failed: %s\n", err.text);
        odk_kernel_free(kernel);
        return 2;
    }
    if (ex.option_count != ri->m) {
        fprintf(stderr, "explain: option_count %zu != %zu\n", ex.option_count,
                ri->m);
        odk_explain_release(&ex);
        odk_kernel_free(kernel);
        return 2;
    }

    /* Sort best-first by probability; ties break on request order. */
    prob_row *rows = malloc(ri->m * sizeof(*rows));
    if (!rows) {
        odk_explain_release(&ex);
        odk_kernel_free(kernel);
        return 2;
    }
    for (size_t i = 0; i < ri->m; i++) {
        rows[i].idx = i;
        rows[i].probability = ex.options[i].probability;
    }
    qsort(rows, ri->m, sizeof(*rows), row_cmp);

    printf("{\"model\":\"omnidecide-c/%d.%d.%d\",\"explains\":[",
           ODK_VERSION_MAJOR, ODK_VERSION_MINOR, ODK_VERSION_PATCH);
    for (size_t r = 0; r < ri->m; r++) {
        size_t i = rows[r].idx;
        const odk_explain_option *o = &ex.options[i];
        if (r > 0)
            putchar(',');
        printf("{\"id\":\"%s\",\"probability\":%.4f,\"fused\":%.17g,"
               "\"tf_cosine\":%.17g,\"jaccard\":%.17g,\"trigram\":%.17g,"
               "\"whole\":%.17g,\"best_chunk\":%.17g,\"contextual\":%.17g,"
               "\"directional\":%.17g}",
               ri->ids[i], o->probability, o->fused, o->tf_cosine, o->jaccard,
               o->trigram, o->whole, o->best_chunk, o->contextual,
               o->directional);
    }
    fputs("]}\n", stdout);
    fflush(stdout);

    free(rows);
    odk_explain_release(&ex);
    odk_kernel_free(kernel);
    return 0;
}

/* ---------------------------- check mode -------------------------------- */

/* Response rank entry, parsed in response order. */
typedef struct {
    char *id; /* NUL-terminated copy */
    double confidence;
    long rank_field; /* the entry's "rank": value (-1 when absent) */
} resp_entry;

/* Parse `{"model":"omnidecide-c/x.y.z","ranks":[{"id":"...","rank":N,
 * "confidence":P}, ...]}`. Rank entries only; ids are copied NUL-terminated
 * (bounded by the 64 KiB response cap). Returns 0 and fills the out
 * parameters (caller frees), or -1 with a diagnostic on stderr. */
static int parse_response(const char *resp, char **model_out,
                          resp_entry **entries_out, size_t *n_out) {
    const char *m = strstr(resp, "\"model\":\"");
    if (!m) {
        fprintf(stderr, "explain: response has no model field\n");
        return -1;
    }
    m += strlen("\"model\":\"");
    const char *mend = strchr(m, '"');
    if (!mend) {
        fprintf(stderr, "explain: malformed model field\n");
        return -1;
    }
    size_t mlen = (size_t)(mend - m);
    char *model = malloc(mlen + 1);
    if (!model)
        return -1;
    memcpy(model, m, mlen);
    model[mlen] = '\0';

    const char *ranks = strstr(resp, "\"ranks\":[");
    if (!ranks) {
        fprintf(stderr, "explain: response has no ranks array\n");
        free(model);
        return -1;
    }
    ranks += strlen("\"ranks\":[");

    size_t cap = 16, n = 0;
    resp_entry *es = malloc(cap * sizeof(*es));
    if (!es) {
        free(model);
        return -1;
    }
    const char *p = ranks;
    for (;;) {
        const char *idm = strstr(p, "\"id\":\"");
        if (!idm)
            break; /* end of the ranks array */
        idm += strlen("\"id\":\"");
        const char *idend = strchr(idm, '"');
        if (!idend)
            break;
        const char *conf = strstr(idend, "\"confidence\":");
        if (!conf)
            break;
        conf += strlen("\"confidence\":");
        double v = strtod(conf, NULL);
        long rf = -1;
        {
            const char *rk = strstr(idend, "\"rank\":");
            if (rk && rk < conf)
                rf = strtol(rk + strlen("\"rank\":"), NULL, 10);
        }
        if (n == cap) {
            cap *= 2;
            resp_entry *nes = realloc(es, cap * sizeof(*es));
            if (!nes) {
                free(es);
                free(model);
                return -1;
            }
            es = nes;
        }
        size_t ilen = (size_t)(idend - idm);
        char *id = malloc(ilen + 1);
        if (!id) {
            free(es);
            free(model);
            return -1;
        }
        memcpy(id, idm, ilen);
        id[ilen] = '\0';
        es[n].id = id;
        es[n].confidence = v;
        es[n].rank_field = rf;
        n++;
        p = conf;
    }
    *model_out = model;
    *entries_out = es;
    *n_out = n;
    return 0;
}

static void free_entries(resp_entry *es, size_t n) {
    for (size_t i = 0; i < n; i++)
        free(es[i].id);
    free(es);
}

static int run_check(const req_items *ri, const char *resp_path) {
    size_t resp_len = 0;
    char *resp = slurp_file(resp_path, &resp_len);
    if (!resp || resp_len == 0) {
        fprintf(stderr, "explain: cannot read response file '%s'\n",
                resp_path);
        free(resp);
        return 2;
    }

    int verdict = 2;
    char *model = NULL;
    resp_entry *es = NULL;
    size_t ne = 0;
    double *prob = NULL;
    odk_kernel *kernel = NULL;
    odk_explain ex;
    memset(&ex, 0, sizeof(ex));
    int ex_live = 0;

    if (parse_response(resp, &model, &es, &ne) != 0)
        goto out;

    {
        char want_model[64];
        snprintf(want_model, sizeof(want_model), "omnidecide-c/%d.%d.%d",
                 ODK_VERSION_MAJOR, ODK_VERSION_MINOR, ODK_VERSION_PATCH);
        if (strcmp(model, want_model) != 0) {
            fprintf(stderr,
                    "explain: model mismatch: response '%s' vs this build "
                    "'%s'\n",
                    model, want_model);
            goto out;
        }
    }

    {
        odk_kernel_config cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.language = ODK_LANG_EN; /* identical to oc_odk_provider */
        kernel = odk_kernel_create_ex(&cfg);
    }
    if (!kernel) {
        fprintf(stderr, "explain: kernel create failed\n");
        goto out;
    }

    {
        odk_error err;
        memset(&err, 0, sizeof(err));
        if (odk_explain_choice(kernel, INSTRUCTION, ri->texts, ri->m, NULL,
                               NULL, &ex, &err) != ODK_OK) {
            fprintf(stderr, "explain: odk_explain_choice failed: %s\n",
                    err.text);
            goto out;
        }
    }
    ex_live = 1;
    if (ex.option_count != ri->m) {
        fprintf(stderr, "explain: option_count %zu != %zu\n", ex.option_count,
                ri->m);
        goto out;
    }

    prob = malloc(ri->m * sizeof(*prob));
    if (!prob)
        goto out;
    for (size_t i = 0; i < ri->m; i++)
        prob[i] = ex.options[i].probability;

    if (ne == 0) {
        printf("explain: OK: empty ranks (fail-open response), %zu items "
               "explained, model %s\n",
               ri->m, model);
        verdict = 0;
        goto out;
    }

    double prev = 2.0; /* above the valid range so the first check passes */
    for (size_t k = 0; k < ne; k++) {
        /* sibling-identity model: no id may appear more often than the
         * request carries it, and all carriers share one text, hence one
         * probability -- response entries for one id must agree */
        size_t seen_here = 0, req_carriers = 0;
        for (size_t j = 0; j < k; j++)
            if (strcmp(es[j].id, es[k].id) == 0)
                seen_here++;
        for (size_t i = 0; i < ri->m; i++)
            if (strcmp(ri->ids[i], es[k].id) == 0)
                req_carriers++;
        if (seen_here + 1 > req_carriers || req_carriers == 0) {
            if (req_carriers == 0)
                fprintf(stderr,
                        "explain: response id not in request: \"%s\"\n",
                        es[k].id);
            else
                fprintf(stderr,
                        "explain: id \"%s\" appears %zu times in the "
                        "response but the request carries it %zu time(s)\n",
                        es[k].id, seen_here + 1, req_carriers);
            goto out;
        }
        if (seen_here > 0 &&
            es[k].confidence != es[k - 1].confidence) {
            /* carriers adjacent in rank order: same text, same probability;
             * a difference means the join is ambiguous -- refuse */
            fprintf(stderr,
                    "explain: carriers of id \"%s\" disagree: %.4f vs %.4f\n",
                    es[k].id, es[k - 1].confidence, es[k].confidence);
            goto out;
        }
        long req_idx = -1;
        for (size_t i = 0; i < ri->m; i++) {
            if (strcmp(es[k].id, ri->ids[i]) == 0) {
                req_idx = (long)i;
                break;
            }
        }
        if (req_idx < 0) {
            fprintf(stderr, "explain: response id not in request: \"%s\"\n",
                    es[k].id);
            goto out;
        }
        char got[32], want[32];
        snprintf(got, sizeof(got), "%.4f", es[k].confidence);
        snprintf(want, sizeof(want), "%.4f", prob[req_idx]);
        if (strcmp(got, want) != 0) {
            fprintf(stderr,
                    "explain: confidence mismatch for \"%s\": response %s "
                    "vs explained %s\n",
                    es[k].id, got, want);
            goto out;
        }
        if (es[k].confidence > prev + 1e-9) {
            fprintf(stderr,
                    "explain: confidence increases along rank order at "
                    "entry %zu (\"%s\": %.4f after %.4f)\n",
                    k, es[k].id, es[k].confidence, prev);
            goto out;
        }
        prev = es[k].confidence;

        /* rank field must equal the entry position (sequential 0..E-1);
         * parsed at parse time, so carriers of one id are handled too */
        if (es[k].rank_field != (long)k) {
            fprintf(stderr,
                    "explain: rank field out of sequence for \"%s\" "
                    "(entry %zu carries rank %ld)\n",
                    es[k].id, k, es[k].rank_field);
            goto out;
        }
    }

    printf("explain: OK: %zu/%zu response entries agree with the explained "
           "probabilities (4-decimal), order monotone, model %s, %zu items\n",
           ne, ne, model, ri->m);
    verdict = 0;

out:
    free(prob);
    if (ex_live)
        odk_explain_release(&ex);
    if (kernel)
        odk_kernel_free(kernel);
    free_entries(es, ne);
    free(model);
    free(resp);
    return verdict;
}

int main(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "usage: oc_odk_explain [RESPONSE_FILE]\n");
        return 2;
    }
    size_t req_len = 0;
    char *req = slurp_stdin(&req_len);
    if (!req || req_len == 0) {
        fprintf(stderr, "explain: empty request on stdin\n");
        free(req);
        return 2;
    }

    int rc = 2;
    odk_error err;
    memset(&err, 0, sizeof(err));
    odk_json_value *root = odk_json_parse(req, req_len, &err);
    if (!root || root->type != ODK_JSON_OBJECT) {
        fprintf(stderr, "explain: bad request JSON: %s\n", err.text);
        goto done;
    }

    {
        const odk_json_value *items = odk_json_object_get(root, "items");
        if (!items || items->type != ODK_JSON_ARRAY ||
            items->item_count == 0) {
            fprintf(stderr, "explain: no items\n");
            goto done;
        }
        size_t n = items->item_count;
        req_items ri;
        memset(&ri, 0, sizeof(ri));
        ri.ids = calloc(n, sizeof(*ri.ids));
        ri.texts = calloc(n, sizeof(*ri.texts));
        if (!ri.ids || !ri.texts) {
            free(ri.ids);
            free(ri.texts);
            goto done;
        }
        for (size_t i = 0; i < n; i++) {
            const odk_json_value *it = items->items[i];
            if (!it || it->type != ODK_JSON_OBJECT)
                continue;
            const odk_json_value *id = odk_json_object_get(it, "id");
            const odk_json_value *tx = odk_json_object_get(it, "text");
            if (!id || id->type != ODK_JSON_STRING || !tx ||
                tx->type != ODK_JSON_STRING)
                continue;
            ri.ids[ri.m] = id->string;
            ri.texts[ri.m] = tx->string;
            ri.m++;
        }
        if (ri.m < 2) {
            fprintf(stderr, "explain: rank needs >= 2 options, got %zu\n",
                    ri.m);
            free(ri.ids);
            free(ri.texts);
            goto done;
        }
        /* ids follow the seam's sibling-identity model (src/triage.c):
         * byte-identical siblings share one fingerprint by design. One id
         * behind two DIFFERENT texts is the sibling-identity defect class
         * (fba5242) -- the join would be genuinely ambiguous -- and is
         * refused. Identical texts under one id are allowed. */
        for (size_t i = 0; i < ri.m; i++) {
            for (size_t j = i + 1; j < ri.m; j++) {
                if (strcmp(ri.ids[i], ri.ids[j]) == 0 &&
                    strcmp(ri.texts[i], ri.texts[j]) != 0) {
                    fprintf(stderr,
                            "explain: duplicate id \"%s\" carries different "
                            "texts (ambiguous join)\n",
                            ri.ids[i]);
                    free(ri.ids);
                    free(ri.texts);
                    goto done;
                }
            }
        }

        /* ri.ids/ri.texts point into the request JSON tree; the tree must
         * stay alive until run_* returns and is freed once at `done`, so
         * every error path (here and inside run_*) is leak-free by
         * construction. */
        if (argc == 2)
            rc = run_check(&ri, argv[1]);
        else
            rc = run_dump(&ri);
        free(ri.ids);
        free(ri.texts);
    }

done:
    odk_json_free(root);
    free(req);
    return rc;
}
