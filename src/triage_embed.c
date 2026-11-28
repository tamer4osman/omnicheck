#include "oc/triage_embed.h"

#include "oc/str.h"

#include <omnidecide.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Embedded protocol-v0 provider -- the in-process twin of
 * tools/omnidecide/oc_odk_provider.c. Every kernel call, every format
 * string and every skip rule below mirrors that shim on purpose: the C1
 * protocol-equality contract is that the embedded path and the sidecar
 * path produce BYTE-IDENTICAL responses for the same request bytes
 * (asserted by tests/test_triage_embed.c), so reports only ever differ
 * in the provider label.
 */

#define OC_TEMBED_EMIT_MAX (1u << 20) /* same sanity bound as the shim */

/* The shim's fixed provider-side rubric, byte-identical. */
static const char OC_TEMBED_INSTRUCTION[] =
    "Rank these static-analysis findings from most to least important "
    "for a developer to address first.";

/* The shim's emit_json_string_to, writing into an oc_strbuf. */
static void tembed_json_string(oc_strbuf *b, const char *s) {
    oc_strbuf_appendc(b, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  oc_strbuf_appends(b, "\\\""); break;
        case '\\': oc_strbuf_appends(b, "\\\\"); break;
        case '\b': oc_strbuf_appends(b, "\\b"); break;
        case '\f': oc_strbuf_appends(b, "\\f"); break;
        case '\n': oc_strbuf_appends(b, "\\n"); break;
        case '\r': oc_strbuf_appends(b, "\\r"); break;
        case '\t': oc_strbuf_appends(b, "\\t"); break;
        default:
            if (*p < 0x20)
                oc_strbuf_appendf(b, "\\u%04x", (unsigned)*p);
            else
                oc_strbuf_appendc(b, (char)*p);
        }
    }
    oc_strbuf_appendc(b, '"');
}

void oc_triage_embed_provider_label(char *buf, size_t sz) {
    snprintf(buf, sz, "embedded:omnidecide-%d.%d.%d", ODK_VERSION_MAJOR,
             ODK_VERSION_MINOR, ODK_VERSION_PATCH);
}

/* T7: pending kernel customization files (set once per scan). */
static const char *const *tembed_lexicon_paths = NULL;
static size_t tembed_n_lexicons = 0;
static const char *tembed_model_path = NULL;
static size_t tembed_lexicons_applied = 0;
static int tembed_model_applied = 0;

void oc_triage_embed_set_kernel_files(const char *const *lexicon_paths,
                                      size_t n_lexicons, const char *model_path) {
    tembed_lexicon_paths = lexicon_paths;
    tembed_n_lexicons = n_lexicons;
    tembed_model_path = model_path;
    tembed_lexicons_applied = 0;
    tembed_model_applied = 0;
}

size_t oc_triage_embed_lexicons_applied(void) {
    return tembed_lexicons_applied;
}

int oc_triage_embed_model_applied(void) {
    return tembed_model_applied;
}

int oc_triage_embed_files_requested(void) {
    return (tembed_n_lexicons > 0 || (tembed_model_path && tembed_model_path[0]))
               ? 1
               : 0;
}

/* T7 loading, shared by the rank and explain responders: LOUD rejection
 * contract. On ANY kernel refusal the reason begins "kernel rejected "
 * (triage.c maps that to a loud usage error) and 0 is returned. */
static int tembed_load_files(odk_kernel *kernel, char *reason,
                             size_t reason_sz) {
    for (size_t i = 0; i < tembed_n_lexicons; i++) {
        odk_error lerr;
        memset(&lerr, 0, sizeof(lerr));
        if (odk_kernel_load_lexicon(kernel, tembed_lexicon_paths[i], &lerr) ==
            ODK_OK) {
            tembed_lexicons_applied++;
            continue;
        }
        snprintf(reason, reason_sz, "kernel rejected lexicon '%s': %s",
                 tembed_lexicon_paths[i],
                 lerr.text[0] ? lerr.text : "unknown kernel error");
        return 0;
    }
    if (tembed_model_path && tembed_model_path[0]) {
        odk_error lerr;
        memset(&lerr, 0, sizeof(lerr));
        if (odk_kernel_load_model(kernel, tembed_model_path, &lerr) != ODK_OK) {
            snprintf(reason, reason_sz, "kernel rejected model file '%s': %s",
                     tembed_model_path,
                     lerr.text[0] ? lerr.text : "unknown kernel error");
            return 0;
        }
        tembed_model_applied = 1;
    }
    return 1;
}

int oc_triage_embed_respond(const char *req, size_t req_len, char **resp,
                            size_t *resp_len, char *reason, size_t reason_sz) {
    *resp = NULL;
    *resp_len = 0;
    if (reason_sz > 0)
        reason[0] = '\0';
    if (!req || req_len == 0) {
        snprintf(reason, reason_sz, "embedded provider got an empty request");
        return 0;
    }

    /* Parse the request with the KERNEL's own parser, exactly like the
     * shim does -- same acceptance, same limits, same wire semantics. */
    odk_error err;
    memset(&err, 0, sizeof(err));
    odk_json_value *root = odk_json_parse(req, req_len, &err);
    if (!root || root->type != ODK_JSON_OBJECT) {
        odk_json_free(root);
        snprintf(reason, reason_sz, "embedded provider could not parse the request");
        return 0;
    }
    const odk_json_value *items = odk_json_object_get(root, "items");
    if (!items || items->type != ODK_JSON_ARRAY || items->item_count == 0) {
        odk_json_free(root);
        snprintf(reason, reason_sz, "embedded provider found no items to rank");
        return 0;
    }
    size_t n = items->item_count;

    /* Collect ids and texts in request order; texts become rank options.
     * Same field checks, same "skip unusable items" discipline. */
    const char **ids = calloc(n, sizeof(*ids));
    const char **texts = calloc(n, sizeof(*texts));
    if (!ids || !texts) {
        free(ids);
        free(texts);
        odk_json_free(root);
        snprintf(reason, reason_sz, "out of memory in the embedded provider");
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
        snprintf(reason, reason_sz, "embedded provider found no usable items");
        return 0;
    }

    /* The shim's kernel configuration: the documented reviewed English
     * pack, set explicitly (never AUTO) so no detector variance can enter
     * the pipeline. */
    odk_kernel_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.language = ODK_LANG_EN;
    odk_kernel *kernel = odk_kernel_create_ex(&cfg);
    if (!kernel) {
        free(ids);
        free(texts);
        odk_json_free(root);
        snprintf(reason, reason_sz, "embedded provider could not create the kernel");
        return 0;
    }

    /* T7: lexicon packs and a model file load right after kernel
     * creation, before any ranking. A file the kernel refuses is LOUD
     * (the reason begins "kernel rejected " and triage.c fails the run). */
    if (!tembed_load_files(kernel, reason, reason_sz)) {
        odk_kernel_free(kernel);
        free(ids);
        free(texts);
        odk_json_free(root);
        return 0;
    }

    odk_result result;
    memset(&result, 0, sizeof(result));
    odk_status st = odk_evaluate_rank(kernel, OC_TEMBED_INSTRUCTION, texts, m,
                                      NULL, NULL, &result, &err);
    if (st != ODK_OK) {
        odk_kernel_free(kernel);
        free(ids);
        free(texts);
        odk_json_free(root);
        snprintf(reason, reason_sz, "embedded provider rank call failed");
        return 0;
    }

    odk_ranked_item *ranked = NULL;
    size_t ranked_n = odk_result_rank_items(&result, texts, m, &ranked);
    if (ranked_n == 0 || !ranked) {
        odk_kernel_free(kernel);
        free(ids);
        free(texts);
        odk_json_free(root);
        snprintf(reason, reason_sz, "embedded provider produced no ranked items");
        return 0;
    }

    /* Emit EVERY ranked entry, best first -- same walk, same skip rules,
     * same cap, same printf formats as the shim's stdout block. */
    oc_strbuf b;
    oc_strbuf_init(&b);
    int ok = 1;
    if (oc_strbuf_appendf(&b, "{\"model\":\"omnidecide-c/%d.%d.%d\",\"ranks\":[",
                          ODK_VERSION_MAJOR, ODK_VERSION_MINOR,
                          ODK_VERSION_PATCH) == 0) {
        ok = 0;
    }
    size_t emitted = 0;
    for (size_t i = 0; ok && i < ranked_n && emitted < OC_TEMBED_EMIT_MAX; i++) {
        size_t idx = ranked[i].index;
        if (idx == (size_t)-1 || idx >= m || !ids[idx])
            continue;
        if (emitted > 0 && oc_strbuf_appendc(&b, ',') == 0)
            ok = 0;
        if (ok) {
            oc_strbuf_appends(&b, "{\"id\":");
            tembed_json_string(&b, ids[idx]);
            if (oc_strbuf_appendf(&b, ",\"rank\":%zu,\"confidence\":%.4f}", i,
                                  ranked[i].score) == 0)
                ok = 0;
        }
        emitted++;
    }
    if (ok && oc_strbuf_appends(&b, "]}\n") == 0)
        ok = 0;

    odk_result_rank_items_free(ranked);
    odk_result_release(&result);
    odk_kernel_free(kernel);
    free(ids);
    free(texts);
    odk_json_free(root);

    /* Same emission-cap discipline as the shim: the loop stops at the
     * sanity bound and the response still completes as valid JSON. */
    if (!ok || b.data == NULL) {
        oc_strbuf_destroy(&b);
        snprintf(reason, reason_sz, "out of memory in the embedded provider");
        return 0;
    }

    /* Hand the buffer to the caller: oc_strbuf's exact bytes, including
     * the trailing newline, NUL-terminated for convenient parsing. */
    if (oc_strbuf_appendc(&b, '\0') == 0) {
        oc_strbuf_destroy(&b);
        snprintf(reason, reason_sz, "out of memory in the embedded provider");
        return 0;
    }
    *resp = b.data;
    *resp_len = b.len - 1; /* exclude the NUL terminator */
    return 1;
}

int oc_triage_embed_explain(const char *req, size_t req_len, char **resp,
                            size_t *resp_len, char *reason, size_t reason_sz) {
    *resp = NULL;
    *resp_len = 0;
    if (reason_sz > 0)
        reason[0] = '\0';
    if (!req || req_len == 0) {
        snprintf(reason, reason_sz, "embedded explain got an empty request");
        return 0;
    }

    /* Same request parse as the rank responder: the KERNEL's own parser,
     * same acceptance, same skip rules. */
    odk_error err;
    memset(&err, 0, sizeof(err));
    odk_json_value *root = odk_json_parse(req, req_len, &err);
    if (!root || root->type != ODK_JSON_OBJECT) {
        odk_json_free(root);
        snprintf(reason, reason_sz, "embedded explain could not parse the request");
        return 0;
    }
    const odk_json_value *items = odk_json_object_get(root, "items");
    if (!items || items->type != ODK_JSON_ARRAY || items->item_count == 0) {
        odk_json_free(root);
        snprintf(reason, reason_sz, "embedded explain found no items");
        return 0;
    }
    size_t n = items->item_count;

    const char **ids = calloc(n, sizeof(*ids));
    const char **texts = calloc(n, sizeof(*texts));
    if (!ids || !texts) {
        free(ids);
        free(texts);
        odk_json_free(root);
        snprintf(reason, reason_sz, "out of memory in the embedded explain");
        return 0;
    }
    size_t m = 0;
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
    /* The rank primitive (and with it the explain pass over the same
     * options) needs at least 2 options. */
    if (m < 2) {
        free(ids);
        free(texts);
        odk_json_free(root);
        snprintf(reason, reason_sz,
                 "embedded explain needs at least 2 usable items (the rank "
                 "primitive requires at least 2 options)");
        return 0;
    }

    /* Same kernel configuration as the rank responder and the shim: the
     * documented reviewed English pack, explicit (never AUTO). */
    odk_kernel_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.language = ODK_LANG_EN;
    odk_kernel *kernel = odk_kernel_create_ex(&cfg);
    if (!kernel) {
        free(ids);
        free(texts);
        odk_json_free(root);
        snprintf(reason, reason_sz, "embedded explain could not create the kernel");
        return 0;
    }

    /* T7 hooks, same as the rank responder: a kernel refusal is LOUD
     * ("kernel rejected ..." reason). */
    if (!tembed_load_files(kernel, reason, reason_sz)) {
        odk_kernel_free(kernel);
        free(ids);
        free(texts);
        odk_json_free(root);
        return 0;
    }

    /* The read-only explain pass over ALL options: per EXPLAIN.md it
     * changes no arithmetic (the golden corpus stays byte-identical), so
     * rank arithmetic is untouched by construction. */
    odk_explain ex;
    memset(&ex, 0, sizeof(ex));
    if (odk_explain_choice(kernel, OC_TEMBED_INSTRUCTION, texts, m, NULL, NULL,
                           &ex, &err) != ODK_OK ||
        ex.option_count != m) {
        odk_explain_release(&ex);
        odk_kernel_free(kernel);
        free(ids);
        free(texts);
        odk_json_free(root);
        snprintf(reason, reason_sz, "embedded explain call failed");
        return 0;
    }

    /* Emit in REQUEST order: a stable 1:1 mapping onto the caller's
     * items. probability %.4f, channels %.17g -- the auditor's formats. */
    oc_strbuf b;
    oc_strbuf_init(&b);
    int ok = 1;
    if (oc_strbuf_appendf(&b, "{\"model\":\"omnidecide-c/%d.%d.%d\",\"explain\":[",
                          ODK_VERSION_MAJOR, ODK_VERSION_MINOR,
                          ODK_VERSION_PATCH) == 0)
        ok = 0;
    for (size_t i = 0; ok && i < m; i++) {
        const odk_explain_option *o = &ex.options[i];
        if (i > 0 && oc_strbuf_appendc(&b, ',') == 0)
            ok = 0;
        if (!ok)
            break;
        oc_strbuf_appends(&b, "{\"id\":");
        tembed_json_string(&b, ids[i]);
        if (oc_strbuf_appendf(&b,
                              ",\"probability\":%.4f,\"fused\":%.17g,"
                              "\"tf_cosine\":%.17g,\"jaccard\":%.17g,"
                              "\"trigram\":%.17g,\"whole\":%.17g,"
                              "\"best_chunk\":%.17g,\"contextual\":%.17g,"
                              "\"directional\":%.17g}",
                              o->probability, o->fused, o->tf_cosine,
                              o->jaccard, o->trigram, o->whole, o->best_chunk,
                              o->contextual, o->directional) == 0)
            ok = 0;
    }
    if (ok && oc_strbuf_appends(&b, "]}\n") == 0)
        ok = 0;

    odk_explain_release(&ex);
    odk_kernel_free(kernel);
    free(ids);
    free(texts);
    odk_json_free(root);

    if (!ok || b.data == NULL) {
        oc_strbuf_destroy(&b);
        snprintf(reason, reason_sz, "out of memory in the embedded explain");
        return 0;
    }
    if (oc_strbuf_appendc(&b, '\0') == 0) {
        oc_strbuf_destroy(&b);
        snprintf(reason, reason_sz, "out of memory in the embedded explain");
        return 0;
    }
    *resp = b.data;
    *resp_len = b.len - 1;
    return 1;
}
