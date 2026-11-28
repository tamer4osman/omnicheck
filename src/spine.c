#include "oc/spine.h"

#include "oc/json.h"
#include "oc/str.h"

#include <omnidecide.h>
#include <odk_spine.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * T5 implementation. The kernel spine owns decision semantics and ids;
 * OmniCheck owns the durable store (one JSON document, atomic writes,
 * deterministic field order). The store is parsed with the hardened
 * oc_json parser (OC_JSON_MAX_ENTRIES budget applies) and capped at
 * OC_SPINE_MAX_PINS.
 */

#define OC_SPINE_MAX_PINS 10000u

static void spine_file_path(char *buf, size_t sz) {
    const char *env = getenv("OC_SPINE_FILE");
    snprintf(buf, sz, "%s", (env && env[0]) ? env : OC_SPINE_DEFAULT_FILE);
}

static int spine_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* Atomic store write: tmp file + fflush + fsync + rename (the C5
 * crash-safety discipline; the store is a plain 0644 document). */
static int spine_atomic_write(const char *path, const char *data, size_t len) {
    char tmp[560];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return 0;
    if (fwrite(data, 1, len, f) != len || fflush(f) != 0) {
        fclose(f);
        remove(tmp);
        return 0;
    }
    if (fsync(fileno(f)) != 0) {
        fclose(f);
        remove(tmp);
        return 0;
    }
    fclose(f);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return 0;
    }
    return 1;
}

/* JSON string emitter (defensive; never trust the wire). */
static void spine_json_string(oc_strbuf *b, const char *s) {
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

/* Loads the store document into the caller's arena; returns the pins
 * array (or NULL when the store is absent, which is an empty store). */
static oc_json *spine_load_store(const char *path, oc_arena *a, oc_status *st) {
    if (!spine_file_exists(path))
        return NULL;
    char *data = NULL;
    size_t len = 0;
    oc_code rc = oc_read_file(path, &data, &len, st);
    if (rc != OC_OK)
        return NULL;
    oc_status pst;
    oc_status_clear(&pst);
    oc_json *root = oc_json_parse(a, data, len, &pst);
    free(data);
    if (!root || root->type != OC_JSON_OBJ ||
        strcmp(oc_json_as_str(oc_json_get(root, "schema"), ""),
               "omnicheck.spine/v0") != 0) {
        oc_status_set(st, OC_ERR_PARSE,
                      "pin store '%s' is corrupt or foreign; refusing (fail "
                      "loud -- move it aside or delete it)",
                      path);
        return NULL;
    }
    return root;
}


#if !defined(OC_EMBED_OMNIDECIDE)

/* No vendored kernel: pin/replay cannot evaluate; fail loud. The list
 * command is pure store reading and stays available. */
oc_code oc_spine_cmd_add(const char *file, const char *request,
                         const char *replay_class, FILE *out, oc_status *st) {
    (void)file;
    (void)request;
    (void)replay_class;
    (void)out;
    oc_status_set(st, OC_ERR_USAGE,
                  "oc pin requires the vendored OmniDecide kernel (not "
                  "vendored in this build)");
    return OC_ERR_USAGE;
}

oc_code oc_spine_cmd_replay(const char *file, const char *pin_id, FILE *out,
                            int *verdict_out, oc_status *st) {
    (void)file;
    (void)pin_id;
    (void)out;
    (void)verdict_out;
    oc_status_set(st, OC_ERR_USAGE,
                  "oc replay requires the vendored OmniDecide kernel (not "
                  "vendored in this build)");
    return OC_ERR_USAGE;
}

#else /* OC_EMBED_OMNIDECIDE */

/* First-party request pre-validation. The vendored spine's error path
 * for an unknown primitive frees the parsed request document BEFORE
 * formatting the diagnostic that reads back into it -- a heap-use-after-
 * free (ASan-confirmed, third_party, NOT patched in the vendored tree
 * to keep VENDOR.sha256 intact). OmniCheck therefore validates the
 * request shape itself and only hands the spine well-formed documents;
 * anything else fails loud here with our own message. */
static const char *const SPINE_PRIMITIVES[] = {"choice", "score",  "rank",
                                               "compare", "extract", "noul"};

static int spine_primitive_known(const char *p) {
    for (size_t i = 0; i < sizeof(SPINE_PRIMITIVES) / sizeof(*SPINE_PRIMITIVES);
         i++)
        if (strcmp(p, SPINE_PRIMITIVES[i]) == 0)
            return 1;
    return 0;
}

static int spine_primitive_needs_options(const char *p) {
    return strcmp(p, "choice") == 0 || strcmp(p, "rank") == 0 ||
           strcmp(p, "compare") == 0;
}

/* Validates the request document shape. Returns 0 and sets st on any
 * violation (fail loud), 1 when the shape is well-formed. */
static int spine_validate_request(const char *request, oc_arena *a,
                                  oc_status *st) {
    oc_status pst;
    oc_status_clear(&pst);
    oc_json *root = oc_json_parse(a, request, strlen(request), &pst);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_USAGE,
                      "pin request must be a JSON object (fail loud)");
        return 0;
    }
    const char *primitive =
        oc_json_as_str(oc_json_get(root, "primitive"), NULL);
    if (!primitive || !spine_primitive_known(primitive)) {
        oc_status_set(st, OC_ERR_USAGE,
                      "pin request primitive must be one of choice|score|rank|"
                      "compare|extract|noul (fail loud)");
        return 0;
    }
    const char *instruction =
        oc_json_as_str(oc_json_get(root, "instruction"), NULL);
    if (!instruction || !instruction[0]) {
        oc_status_set(st, OC_ERR_USAGE,
                      "pin request requires a non-empty \"instruction\" string "
                      "(fail loud)");
        return 0;
    }
    if (spine_primitive_needs_options(primitive)) {
        const oc_json *options = oc_json_get(root, "options");
        if (!options || options->type != OC_JSON_ARR || options->n_items < 2) {
            oc_status_set(st, OC_ERR_USAGE,
                          "pin request primitive '%s' requires \"options\": an "
                          "array of at least 2 strings (fail loud)",
                          primitive);
            return 0;
        }
        for (size_t i = 0; i < options->n_items; i++) {
            const oc_json *o = options->items[i];
            if (!o || o->type != OC_JSON_STR) {
                oc_status_set(st, OC_ERR_USAGE,
                              "pin request options must all be strings "
                              "(fail loud)");
                return 0;
            }
        }
    }
    return 1;
}

oc_code oc_spine_cmd_add(const char *file, const char *request,
                         const char *replay_class, FILE *out, oc_status *st) {
    if (!request || !request[0]) {
        oc_status_set(st, OC_ERR_USAGE, "--request requires a JSON document");
        return OC_ERR_USAGE;
    }
    {
        oc_arena va;
        oc_arena_init(&va);
        int shape_ok = spine_validate_request(request, &va, st);
        oc_arena_destroy(&va);
        if (!shape_ok)
            return OC_ERR_USAGE;
    }
    if (!replay_class || (strcmp(replay_class, "exact") != 0 &&
                          strcmp(replay_class, "semantic") != 0 &&
                          strcmp(replay_class, "foreign") != 0)) {
        oc_status_set(st, OC_ERR_USAGE,
                      "--class must be exact|semantic|foreign");
        return OC_ERR_USAGE;
    }

    char path[512];
    spine_file_path(path, sizeof(path));
    if (file && file[0])
        snprintf(path, sizeof(path), "%s", file);

    /* The kernel + spine own decision semantics and the pin record. */
    odk_kernel_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.language = ODK_LANG_EN;
    odk_kernel *kernel = odk_kernel_create_ex(&cfg);
    odk_spine *spine = odk_spine_create();
    if (!kernel || !spine) {
        if (kernel)
            odk_kernel_free(kernel);
        if (spine)
            odk_spine_free(spine);
        oc_status_set(st, OC_ERR_INTERNAL, "kernel or spine failed to start");
        return OC_ERR_INTERNAL;
    }

    odk_spine_error err;
    memset(&err, 0, sizeof(err));
    odk_spine_decision_pin pin;
    odk_spine_decision_pin_init(&pin);
    /* timestamp 0: the caller-supplied pin timestamp is part of the
     * record; wall-clock time would break byte determinism. */
    odk_spine_status src = odk_spine_pin(spine, kernel, request, "oc-pin",
                                         replay_class, 0, &pin, &err);
    if (src != ODK_SPINE_OK) {
        odk_spine_decision_pin_release(&pin);
        odk_spine_free(spine);
        odk_kernel_free(kernel);
        oc_status_set(st, OC_ERR_USAGE,
                      "kernel refused the pin request: %s",
                      err.text[0] ? err.text : "invalid request document");
        return OC_ERR_USAGE;
    }

    /* Load (or start) the store, then append the pin document. */
    oc_arena a;
    oc_arena_init(&a);
    oc_json *store = spine_load_store(path, &a, st);
    if (spine_file_exists(path) && !store) {
        odk_spine_decision_pin_release(&pin);
        odk_spine_free(spine);
        odk_kernel_free(kernel);
        oc_arena_destroy(&a);
        return st->code; /* fail loud on a corrupt store */
    }
    size_t n_pins = 0;
    if (store) {
        const oc_json *pins = oc_json_get(store, "pins");
        if (pins && pins->type == OC_JSON_ARR)
            n_pins = pins->n_items;
    }
    if (n_pins + 1 > OC_SPINE_MAX_PINS) {
        odk_spine_decision_pin_release(&pin);
        odk_spine_free(spine);
        odk_kernel_free(kernel);
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_USAGE,
                      "pin store '%s' exceeds the %u-pin budget; refusing "
                      "loudly",
                      path, (unsigned)OC_SPINE_MAX_PINS);
        return OC_ERR_USAGE;
    }

    char our_id[32];
    snprintf(our_id, sizeof(our_id), "p%06zu", n_pins + 1);
    const char *current_fp = odk_spine_schema_fingerprint(spine);

    oc_strbuf b;
    oc_strbuf_init(&b);
    int ok = 1;
    if (n_pins == 0) {
        if (oc_strbuf_appends(&b, "{\"schema\":\"omnicheck.spine/v0\",\"pins\":[") == 0)
            ok = 0;
    } else {
        /* rewrite: original bytes are unknown; regenerate the whole
         * document from the parsed store + the new pin */
        const oc_json *pins = oc_json_get(store, "pins");
        if (oc_strbuf_appends(&b, "{\"schema\":\"omnicheck.spine/v0\",\"pins\":[") == 0)
            ok = 0;
        for (size_t i = 0; ok && i < pins->n_items; i++) {
            if (i > 0 && oc_strbuf_appendc(&b, ',') == 0)
                ok = 0;
            if (ok) {
                const oc_json *p = pins->items[i];
                /* each stored pin re-serializes from the parsed document */
                oc_jsonw w;
                oc_jsonw_init(&w);
                oc_jsonw_obj_begin(&w);
                for (size_t k = 0; k < p->n_fields; k++) {
                    oc_jsonw_key(&w, p->keys[k]);
                    const oc_json *val = p->vals[k];
                    if (val->type == OC_JSON_STR)
                        oc_jsonw_str(&w, val->str);
                    else if (val->type == OC_JSON_NUM)
                        oc_jsonw_float(&w, val->number);
                    else if (val->type == OC_JSON_BOOL)
                        oc_jsonw_bool(&w, val->boolean);
                    else
                        oc_jsonw_str(&w, NULL);
                }
                oc_jsonw_obj_end(&w);
                if (w.buf.data)
                    oc_strbuf_appends(&b, w.buf.data);
                oc_jsonw_destroy(&w);
            }
        }
        if (ok && n_pins > 0 && oc_strbuf_appendc(&b, ',') == 0)
            ok = 0;
    }
    if (ok) {
        if (oc_strbuf_appends(&b, "{\"id\":") == 0)
            ok = 0;
        if (ok) {
            spine_json_string(&b, our_id);
            if (oc_strbuf_appends(&b, ",\"spine_pin_id\":") == 0)
                ok = 0;
        }
        if (ok) {
            spine_json_string(&b, pin.pin_id ? pin.pin_id : "");
            if (oc_strbuf_appends(&b, ",\"kernel_version\":") == 0)
                ok = 0;
        }
        if (ok) {
            spine_json_string(&b, pin.kernel_version ? pin.kernel_version
                                                     : ODK_SPINE_KERNEL_VERSION);
            if (oc_strbuf_appends(&b, ",\"schema_fp\":") == 0)
                ok = 0;
        }
        if (ok) {
            spine_json_string(&b, pin.schema_fp ? pin.schema_fp : current_fp);
            if (oc_strbuf_appends(&b, ",\"request\":") == 0)
                ok = 0;
        }
        if (ok) {
            spine_json_string(&b, pin.request_json ? pin.request_json : request);
            if (oc_strbuf_appends(&b, ",\"decision\":") == 0)
                ok = 0;
        }
        if (ok) {
            spine_json_string(&b, pin.decision ? pin.decision : "");
            if (oc_strbuf_appends(&b, ",\"confidence\":") == 0)
                ok = 0;
        }
        if (ok) {
            /* %.17g as a STRING: the store re-serializes through the JSON
             * parser on every append, and a %.6g float rewrite would
             * silently degrade the exact-confidence replay verdict. */
            char conf[40];
            snprintf(conf, sizeof(conf), "%.17g", pin.confidence);
            spine_json_string(&b, conf);
            if (oc_strbuf_appends(&b, ",\"replay_class\":") == 0)
                ok = 0;
        }
        if (ok) {
            spine_json_string(&b, replay_class);
            if (oc_strbuf_appendf(&b, ",\"timestamp\":%lld}", (long long)0) == 0)
                ok = 0;
        }
    }
    if (ok && oc_strbuf_appends(&b, "]}\n") == 0)
        ok = 0;

    odk_spine_decision_pin_release(&pin);
    odk_spine_free(spine);
    odk_kernel_free(kernel);
    oc_arena_destroy(&a);

    if (!ok || b.data == NULL) {
        oc_strbuf_destroy(&b);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory writing the pin store");
        return OC_ERR_ALLOC;
    }
    if (!spine_atomic_write(path, b.data, b.len)) {
        oc_strbuf_destroy(&b);
        oc_status_set(st, OC_ERR_IO, "failed to save the pin store '%s'", path);
        return OC_ERR_IO;
    }

    /* Emit the pin document. */
    fprintf(out,
            "{\"schema\":\"omnicheck.spine.pin/v0\",\"id\":\"%s\",\"file\":",
            our_id);
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_str(&w, path);
    fprintf(out, "%s,\"saved\":true}\n", w.buf.data ? w.buf.data : "null");
    oc_jsonw_destroy(&w);
    oc_strbuf_destroy(&b);
    return OC_OK;
}

oc_code oc_spine_cmd_replay(const char *file, const char *pin_id, FILE *out,
                            int *verdict_out, oc_status *st) {
    if (!pin_id || !pin_id[0]) {
        oc_status_set(st, OC_ERR_USAGE, "--id requires a pin id (see 'oc pin list')");
        return OC_ERR_USAGE;
    }
    char path[512];
    spine_file_path(path, sizeof(path));
    if (file && file[0])
        snprintf(path, sizeof(path), "%s", file);

    oc_arena a;
    oc_arena_init(&a);
    oc_json *store = spine_load_store(path, &a, st);
    if (!store) {
        if (!spine_file_exists(path)) {
            oc_status_set(st, OC_ERR_USAGE, "no pin store at '%s' (pin a "
                          "decision first with 'oc pin add')",
                          path);
        }
        oc_arena_destroy(&a);
        return st->code ? st->code : OC_ERR_USAGE;
    }
    const oc_json *pins = oc_json_get(store, "pins");
    const oc_json *hit = NULL;
    if (pins && pins->type == OC_JSON_ARR) {
        for (size_t i = 0; i < pins->n_items; i++) {
            const oc_json *p = pins->items[i];
            const char *id = oc_json_as_str(oc_json_get(p, "id"), NULL);
            if (id && strcmp(id, pin_id) == 0) {
                hit = p;
                break;
            }
        }
    }
    if (!hit) {
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_USAGE, "unknown pin id '%s' in '%s'", pin_id,
                      path);
        return OC_ERR_USAGE;
    }

    const char *stored_fp = oc_json_as_str(oc_json_get(hit, "schema_fp"), "");
    const char *stored_decision = oc_json_as_str(oc_json_get(hit, "decision"), "");
    const char *stored_conf_str = oc_json_as_str(oc_json_get(hit, "confidence"), "0");
    double stored_conf = stored_conf_str[0] ? strtod(stored_conf_str, NULL) : 0.0;
    const char *request_json = oc_json_as_str(oc_json_get(hit, "request"), "");
    const char *stored_class = oc_json_as_str(oc_json_get(hit, "replay_class"), "exact");

    /* C3: schema fingerprint mismatch fails LOUD. */
    odk_spine *spine = odk_spine_create();
    if (!spine) {
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_INTERNAL, "spine failed to start");
        return OC_ERR_INTERNAL;
    }
    const char *current_fp = odk_spine_schema_fingerprint(spine);
    int fp_match = (strcmp(stored_fp, current_fp) == 0) ? 1 : 0;
    if (!fp_match) {
        /* The message references arena- and spine-owned strings: copy
         * them into local buffers BEFORE freeing either owner (ASan
         * caught the use-after-free here). */
        char id_buf[64], stored_buf[128], current_buf[128];
        snprintf(id_buf, sizeof(id_buf), "%s", pin_id);
        snprintf(stored_buf, sizeof(stored_buf), "%s", stored_fp);
        snprintf(current_buf, sizeof(current_buf), "%s", current_fp);
        odk_spine_free(spine);
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_USAGE,
                      "schema fingerprint mismatch: pin '%s' was recorded "
                      "under %s, the running build is %s; replay refused "
                      "(fail loud -- pin the decision anew to move forward)",
                      id_buf, stored_buf, current_buf);
        return OC_ERR_USAGE;
    }

    if (strcmp(stored_class, "foreign") == 0) {
        odk_spine_free(spine);
        fprintf(out,
                "{\"schema\":\"omnicheck.spine.replay/v0\",\"id\":\"%s\","
                "\"verdict\":\"foreign\",\"note\":\"recorded lineage only; "
                "not re-derivable locally\",\"schema_fp_matches\":true}\n",
                pin_id);
        oc_arena_destroy(&a);
        if (verdict_out)
            *verdict_out = 0;
        return OC_OK;
    }

    /* Re-derive with the current kernel. */
    odk_kernel_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.language = ODK_LANG_EN;
    odk_kernel *kernel = odk_kernel_create_ex(&cfg);
    if (!kernel) {
        odk_spine_free(spine);
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_INTERNAL, "kernel failed to start");
        return OC_ERR_INTERNAL;
    }
    odk_spine_error err;
    memset(&err, 0, sizeof(err));
    char *decision = NULL;
    double confidence = 0.0;
    char *result_json = NULL;
    odk_spine_status src = odk_spine_evaluate_request(kernel, request_json,
                                                      &decision, &confidence,
                                                      &result_json, &err);
    odk_spine_free(spine);
    if (src != ODK_SPINE_OK) {
        if (decision)
            free(decision);
        if (result_json)
            free(result_json);
        odk_kernel_free(kernel);
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_INTERNAL, "replay evaluation failed: %s",
                      err.text[0] ? err.text : "unknown error");
        return OC_ERR_INTERNAL;
    }

    char ea[32], eb[32];
    snprintf(ea, sizeof(ea), "%.17g", stored_conf);
    snprintf(eb, sizeof(eb), "%.17g", confidence);
    const char *verdict;
    if (strcmp(stored_decision, decision ? decision : "") == 0 &&
        strcmp(ea, eb) == 0)
        verdict = "exact";
    else if (strcmp(stored_decision, decision ? decision : "") == 0)
        verdict = "semantic";
    else
        verdict = "mismatch";

    fprintf(out, "{\"schema\":\"omnicheck.spine.replay/v0\",\"id\":\"%s\","
                 "\"verdict\":\"%s\",\"expected\":",
            pin_id, verdict);
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_str(&w, stored_decision);
    fprintf(out, "%s,\"actual\":", w.buf.data ? w.buf.data : "null");
    oc_jsonw_destroy(&w);
    oc_jsonw_init(&w);
    oc_jsonw_str(&w, decision ? decision : "");
    fprintf(out, "%s,\"expected_confidence\":%.17g,\"actual_confidence\":%.17g,"
                 "\"schema_fp_matches\":true}\n",
            w.buf.data ? w.buf.data : "null", stored_conf, confidence);
    oc_jsonw_destroy(&w);

    free(decision);
    free(result_json);
    odk_kernel_free(kernel);
    oc_arena_destroy(&a);
    if (verdict_out)
        *verdict_out = (strcmp(verdict, "mismatch") == 0) ? 1 : 0;
    return OC_OK;
}

#endif /* OC_EMBED_OMNIDECIDE */


oc_code oc_spine_cmd_list(const char *file, FILE *out, oc_status *st) {
    char path[512];
    spine_file_path(path, sizeof(path));
    if (file && file[0])
        snprintf(path, sizeof(path), "%s", file);
    oc_arena a;
    oc_arena_init(&a);
    oc_json *store = spine_load_store(path, &a, st);
    if (!store) {
        if (!spine_file_exists(path)) {
            fprintf(out,
                    "{\"schema\":\"omnicheck.spine.list/v0\",\"file\":");
            oc_jsonw w;
            oc_jsonw_init(&w);
            oc_jsonw_str(&w, path);
            fprintf(out, "%s,\"pins\":[]}\n", w.buf.data ? w.buf.data : "null");
            oc_jsonw_destroy(&w);
            oc_arena_destroy(&a);
            return OC_OK;
        }
        oc_arena_destroy(&a);
        return st->code ? st->code : OC_ERR_PARSE;
    }
    const oc_json *pins = oc_json_get(store, "pins");
    fprintf(out, "{\"schema\":\"omnicheck.spine.list/v0\",\"file\":");
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_str(&w, path);
    fprintf(out, "%s,\"pins\":[", w.buf.data ? w.buf.data : "null");
    oc_jsonw_destroy(&w);
    if (pins && pins->type == OC_JSON_ARR) {
        for (size_t i = 0; i < pins->n_items; i++) {
            const oc_json *p = pins->items[i];
            const char *id = oc_json_as_str(oc_json_get(p, "id"), "?");
            const char *dec = oc_json_as_str(oc_json_get(p, "decision"), "");
            const char *cls = oc_json_as_str(oc_json_get(p, "replay_class"), "?");
            fprintf(out, "%s{\"id\":\"%s\",\"decision\":", i > 0 ? "," : "", id);
            oc_jsonw_init(&w);
            oc_jsonw_str(&w, dec);
            fprintf(out, "%s,\"replay_class\":\"%s\"}", w.buf.data ? w.buf.data : "null",
                    cls);
            oc_jsonw_destroy(&w);
        }
    }
    fprintf(out, "]}\n");
    oc_arena_destroy(&a);
    return OC_OK;
}


