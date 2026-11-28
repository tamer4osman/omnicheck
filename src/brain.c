#include "oc/brain.h"

#include "oc/json.h"
#include "oc/sha256.h"
#include "oc/str.h"
#include "oc/triage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(OC_EMBED_OMNIDECIDE)
#include <omnidecide.h>
#endif

/*
 * T4 implementation. The kernel calls mirror the documented data model:
 * memory keys are (primitive="rank", OC_TRIAGE_INSTRUCTION, option
 * text); advise() is deterministic; the kernel's save_file() is atomic
 * (writes <path>.tmp, renames -- the H-4 crash-safety discipline).
 * Everything advisory fail-opens with a named reason; everything
 * explicit fails loud.
 */

/* Resolves the brain file path: OC_BRAIN_FILE override or the
 * project-local default. Returns 0 on truncation (caller fails loud). */
static int brain_file_path(char *buf, size_t sz) {
    const char *env = getenv("OC_BRAIN_FILE");
    const char *src = (env && env[0]) ? env : OC_BRAIN_DEFAULT_FILE;
    int n = snprintf(buf, sz, "%s", src);
    if (n < 0 || (size_t)n >= sz)
        return 0;
    return 1;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* Streaming sha256 of a file (the store provenance in info/entries). */
static int file_sha256(const char *path, char *hex /* 65 bytes */) {
    char *data = NULL;
    size_t len = 0;
    oc_status st;
    oc_status_clear(&st);
    oc_code rc = oc_read_file(path, &data, &len, &st);
    if (rc != OC_OK) {
        free(data);
        return 0;
    }
    oc_sha256_hex((const unsigned char *)data, len, hex);
    free(data);
    return 1;
}

#if !defined(OC_EMBED_OMNIDECIDE)

/* No embedded kernel: the advisory layer fail-opens; the explicit
 * commands fail loud (they cannot do their job without the kernel). */
oc_code oc_brain_adjust_findings(oc_finding_vec *v, oc_brain_outcome *out, oc_status *st) {
    (void)v;
    memset(out, 0, sizeof(*out));
    out->requested = 1;
    out->epoch = -1;
    if (!brain_file_path(out->file, sizeof(out->file))) {
        snprintf(out->reason, sizeof(out->reason),
                 "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_OK;
    }
    snprintf(out->reason, sizeof(out->reason),
             "brain advisory requires the embedded OmniDecide kernel "
             "(rebuild with -DOC_EMBED_OMNIDECIDE=ON)");
    return OC_OK;
}

#define BRAIN_EXPLICIT_STUB(name)                                                                  \
    oc_code name(const char *file, FILE *out, oc_status *st) {                                     \
        (void)file;                                                                                \
        (void)out;                                                                                 \
        oc_status_set(st, OC_ERR_USAGE,                                                            \
                      "oc brain requires the embedded OmniDecide kernel "                          \
                      "(rebuild with -DOC_EMBED_OMNIDECIDE=ON)");                                  \
        return OC_ERR_USAGE;                                                                       \
    }

BRAIN_EXPLICIT_STUB(oc_brain_cmd_info)
BRAIN_EXPLICIT_STUB(oc_brain_cmd_create)
BRAIN_EXPLICIT_STUB(oc_brain_cmd_entries)
BRAIN_EXPLICIT_STUB(oc_brain_cmd_save)

oc_code oc_brain_cmd_record(const char *file, const char *report_path, const char *fingerprint,
                            const char *outcome, FILE *out, oc_status *st) {
    (void)file;
    (void)report_path;
    (void)fingerprint;
    (void)outcome;
    (void)out;
    oc_status_set(st, OC_ERR_USAGE,
                  "oc brain requires the embedded OmniDecide kernel "
                  "(rebuild with -DOC_EMBED_OMNIDECIDE=ON)");
    return OC_ERR_USAGE;
}

oc_code oc_brain_cmd_advise(const char *file, const char *report_path, FILE *out, oc_status *st) {
    (void)file;
    (void)report_path;
    (void)out;
    oc_status_set(st, OC_ERR_USAGE,
                  "oc brain requires the embedded OmniDecide kernel "
                  "(rebuild with -DOC_EMBED_OMNIDECIDE=ON)");
    return OC_ERR_USAGE;
}

oc_code oc_brain_cmd_discard(const char *file, long index, FILE *out, oc_status *st) {
    (void)file;
    (void)index;
    (void)out;
    oc_status_set(st, OC_ERR_USAGE,
                  "oc brain requires the embedded OmniDecide kernel "
                  "(rebuild with -DOC_EMBED_OMNIDECIDE=ON)");
    return OC_ERR_USAGE;
}

BRAIN_EXPLICIT_STUB(oc_brain_cmd_reset)

#else /* OC_EMBED_OMNIDECIDE */

/* JSON string emitter (defensive; never trust the wire). */
static void brain_json_string(oc_strbuf *b, const char *s) {
    oc_strbuf_appendc(b, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':
            oc_strbuf_appends(b, "\\\"");
            break;
        case '\\':
            oc_strbuf_appends(b, "\\\\");
            break;
        case '\b':
            oc_strbuf_appends(b, "\\b");
            break;
        case '\f':
            oc_strbuf_appends(b, "\\f");
            break;
        case '\n':
            oc_strbuf_appends(b, "\\n");
            break;
        case '\r':
            oc_strbuf_appends(b, "\\r");
            break;
        case '\t':
            oc_strbuf_appends(b, "\\t");
            break;
        default:
            if (*p < 0x20)
                oc_strbuf_appendf(b, "\\u%04x", (unsigned)*p);
            else
                oc_strbuf_appendc(b, (char)*p);
        }
    }
    oc_strbuf_appendc(b, '"');
}

/* Loads the brain snapshot, or creates an empty brain when the file is
 * absent. NULL on kernel failure (caller fail-opens / fails loud). */
static odk_brain *brain_open(const char *path, int *existed) {
    *existed = file_exists(path) ? 1 : 0;
    if (*existed) {
        odk_error err;
        memset(&err, 0, sizeof(err));
        return odk_brain_load_file(path, &err);
    }
    return odk_brain_create();
}

/* Pulls "epoch" out of the kernel's summary payload (-1 when absent). */
static long long brain_epoch(const odk_brain *brain) {
    char *sum = odk_brain_summary_json(brain);
    if (!sum)
        return -1;
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_json *root = oc_json_parse(&a, sum, strlen(sum), &st);
    long long epoch = -1;
    if (root && root->type == OC_JSON_OBJ) {
        const oc_json *e = oc_json_get(root, "epoch");
        if (e && e->type == OC_JSON_NUM)
            epoch = (long long)e->number;
    }
    oc_arena_destroy(&a);
    free(sum);
    return epoch;
}

oc_code oc_brain_adjust_findings(oc_finding_vec *v, oc_brain_outcome *out, oc_status *st) {
    (void)v;
    memset(out, 0, sizeof(*out));
    out->requested = 1;
    out->epoch = -1;
    if (!brain_file_path(out->file, sizeof(out->file))) {
        snprintf(out->reason, sizeof(out->reason),
                 "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_OK;
    }

    /* Rank-only boundary: only triaged findings can be annotated. */
    size_t n_triaged = 0;
    for (size_t i = 0; i < v->n; i++)
        if (v->items[i].triaged)
            n_triaged++;
    if (n_triaged == 0) {
        snprintf(out->reason, sizeof(out->reason),
                 "no triaged findings to adjust (advisory no-op)");
        return OC_OK;
    }

    int existed = 0;
    odk_brain *brain = brain_open(out->file, &existed);
    if (!existed) {
        if (brain)
            odk_brain_free(brain);
        snprintf(out->reason, sizeof(out->reason),
                 "no brain file yet; advisory skipped (record outcomes with "
                 "'oc brain record')");
        return OC_OK;
    }
    if (!brain) {
        snprintf(out->reason, sizeof(out->reason),
                 "brain file unreadable or corrupt; advisory skipped "
                 "(fail-open)");
        return OC_OK;
    }
    out->epoch = brain_epoch(brain);

    /* One advise() over triaged findings only (options array = triaged
     * subset; untriaged entries never receive annotations). */
    double *bonuses = (double *)calloc(n_triaged ? n_triaged : 1, sizeof(*bonuses));
    if (!bonuses) {
        odk_brain_free(brain);
        snprintf(out->reason, sizeof(out->reason),
                 "out of memory consulting the brain (fail-open)");
        return OC_OK;
    }
    const char **options = (const char **)calloc(n_triaged ? n_triaged : 1, sizeof(*options));
    if (!options) {
        free(bonuses);
        odk_brain_free(brain);
        snprintf(out->reason, sizeof(out->reason),
                 "out of memory consulting the brain (fail-open)");
        return OC_OK;
    }
    for (size_t i = 0, j = 0; i < v->n; i++) {
        if (!v->items[i].triaged)
            continue;
        options[j++] = v->items[i].message;
    }

    odk_error err;
    memset(&err, 0, sizeof(err));
    odk_status rc =
        odk_brain_advise(brain, "rank", OC_TRIAGE_INSTRUCTION, options, n_triaged, bonuses, &err);
    if (rc != ODK_OK) {
        free(bonuses);
        free(options);
        odk_brain_free(brain);
        snprintf(out->reason, sizeof(out->reason),
                 "brain advise failed; advisory skipped (fail-open)");
        return OC_OK;
    }

    /* Annotate: adjusted confidence = kernel confidence + weight*bonus,
     * clamped to [0,1]. brain_rank orders triaged findings by adjusted
     * confidence desc, ties by kernel rank asc. Selection sort over the
     * triaged subset (k is tiny compared to v->n in practice; O(k^2)
     * with k <= findings, and deterministic). */
    size_t *idx = (size_t *)calloc(n_triaged ? n_triaged : 1, sizeof(*idx));
    if (!idx) {
        free(bonuses);
        free(options);
        odk_brain_free(brain);
        snprintf(out->reason, sizeof(out->reason),
                 "out of memory consulting the brain (fail-open)");
        return OC_OK;
    }
    double *adj = (double *)calloc(n_triaged ? n_triaged : 1, sizeof(*adj));
    if (!adj) {
        free(idx);
        free(bonuses);
        free(options);
        odk_brain_free(brain);
        snprintf(out->reason, sizeof(out->reason),
                 "out of memory consulting the brain (fail-open)");
        return OC_OK;
    }
    size_t k = 0;
    for (size_t i = 0; i < v->n; i++) {
        if (!v->items[i].triaged)
            continue;
        idx[k] = i;
        double c = v->items[i].triage_confidence + OC_BRAIN_WEIGHT * bonuses[k];
        if (c < 0.0)
            c = 0.0;
        if (c > 1.0)
            c = 1.0;
        adj[k] = c;
        k++;
    }
    for (size_t rank = 0; rank < k; rank++) {
        size_t best = (size_t)-1;
        for (size_t t = 0; t < k; t++) {
            if (idx[t] == (size_t)-1)
                continue;
            if (best == (size_t)-1) {
                best = t;
                continue;
            }
            if (adj[t] > adj[best] || (adj[t] == adj[best] && v->items[idx[t]].triage_rank <
                                                                  v->items[idx[best]].triage_rank))
                best = t;
        }
        oc_finding *f = &v->items[idx[best]];
        f->brain_annotated = 1;
        f->brain_rank = (int)rank;
        f->brain_bonus = bonuses[best];
        idx[best] = (size_t)-1;
        out->adjusted++;
    }

    free(idx);
    free(adj);
    free(bonuses);
    free(options);
    odk_brain_free(brain);
    snprintf(out->reason, sizeof(out->reason), "ok");
    out->applied = 1;
    return OC_OK;
}

oc_code oc_brain_cmd_info(const char *file, FILE *out, oc_status *st) {
    char path[512];
    /* Prefer explicit file over environment/default; validate selected path. */
    if (file && file[0]) {
        int n = snprintf(path, sizeof(path), "%s", file);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            oc_status_set(st, OC_ERR_USAGE, "specified --brain-file path is too long");
            return OC_ERR_USAGE;
        }
    } else if (!brain_file_path(path, sizeof(path))) {
        oc_status_set(st, OC_ERR_USAGE, "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_ERR_USAGE;
    }
    int existed = file_exists(path) ? 1 : 0;

    long long epoch = -1;
    char *summary = NULL;
    char sha[65] = "";
    if (existed) {
        odk_error err;
        memset(&err, 0, sizeof(err));
        odk_brain *brain = odk_brain_load_file(path, &err);
        if (!brain) {
            oc_status_set(st, OC_ERR_PARSE, "brain file '%s' is corrupt or unreadable: %s", path,
                          err.text[0] ? err.text : "kernel load failed");
            return OC_ERR_PARSE;
        }
        epoch = brain_epoch(brain);
        summary = odk_brain_summary_json(brain);
        odk_brain_free(brain);
        if (!summary) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory building the summary");
            return OC_ERR_ALLOC;
        }
        if (!file_sha256(path, sha)) {
            free(summary);
            oc_status_set(st, OC_ERR_IO, "failed to hash the brain file '%s'", path);
            return OC_ERR_IO;
        }
    }

    oc_strbuf b;
    oc_strbuf_init(&b);
    int ok = 1;
    if (oc_strbuf_appends(&b, "{\"schema\":\"omnicheck.brain.info/v0\","
                              "\"file\":") == 0)
        ok = 0;
    if (ok) {
        brain_json_string(&b, path);
        if (oc_strbuf_appendf(&b, ",\"exists\":%s,\"kernel\":\"omnidecide-%d.%d.%d\"",
                              existed ? "true" : "false", ODK_VERSION_MAJOR, ODK_VERSION_MINOR,
                              ODK_VERSION_PATCH) == 0)
            ok = 0;
    }
    if (ok && oc_strbuf_appendf(&b, ",\"epoch\":%lld", epoch) == 0)
        ok = 0;
    if (ok) {
        if (oc_strbuf_appends(&b, ",\"sha256\":") == 0)
            ok = 0;
        if (ok) {
            brain_json_string(&b, existed ? sha : "");
            if (oc_strbuf_appends(&b, ",\"summary\":") == 0)
                ok = 0;
        }
    }
    if (ok && summary && oc_strbuf_appends(&b, summary) == 0)
        ok = 0;
    if (ok && !summary && oc_strbuf_appends(&b, "null") == 0)
        ok = 0;
    if (ok && oc_strbuf_appends(&b, "}\n") == 0)
        ok = 0;
    free(summary);

    if (!ok || b.data == NULL) {
        oc_strbuf_destroy(&b);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory writing the info document");
        return OC_ERR_ALLOC;
    }
    size_t w = fwrite(b.data, 1, b.len, out);
    int werr = (w != b.len) || ferror(out);
    oc_strbuf_destroy(&b);
    if (werr) {
        oc_status_set(st, OC_ERR_IO, "failed to write the brain info");
        return OC_ERR_IO;
    }
    return OC_OK;
}

oc_code oc_brain_cmd_create(const char *file, FILE *out, oc_status *st) {
    char path[512];
    /* Prefer explicit file over environment/default; validate selected path. */
    if (file && file[0]) {
        int n = snprintf(path, sizeof(path), "%s", file);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            oc_status_set(st, OC_ERR_USAGE, "specified --brain-file path is too long");
            return OC_ERR_USAGE;
        }
    } else if (!brain_file_path(path, sizeof(path))) {
        oc_status_set(st, OC_ERR_USAGE, "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_ERR_USAGE;
    }
    if (file_exists(path)) {
        oc_status_set(st, OC_ERR_USAGE,
                      "brain store '%s' already exists; refusing to overwrite "
                      "(use 'oc brain reset' to start over)",
                      path);
        return OC_ERR_USAGE;
    }
    odk_brain *brain = odk_brain_create();
    if (!brain) {
        oc_status_set(st, OC_ERR_INTERNAL, "kernel failed to start");
        return OC_ERR_INTERNAL;
    }
    odk_error err;
    memset(&err, 0, sizeof(err));
    long long epoch = brain_epoch(brain);
    odk_status svr = odk_brain_save_file(brain, path, &err);
    odk_brain_free(brain);
    if (svr != ODK_OK) {
        oc_status_set(st, OC_ERR_IO, "failed to create the brain store '%s': %s", path,
                      err.text[0] ? err.text : "kernel save failed");
        return OC_ERR_IO;
    }
    char sha[65] = "";
    if (!file_sha256(path, sha)) {
        oc_status_set(st, OC_ERR_IO, "failed to hash the brain file '%s'", path);
        return OC_ERR_IO;
    }
    fprintf(out, "{\"schema\":\"omnicheck.brain.create/v0\",\"file\":");
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_str(&w, path);
    size_t w1 =
        fwrite(w.buf.data ? w.buf.data : "null", 1, w.buf.data ? strlen(w.buf.data) : 4, out);
    size_t w1_len = w.buf.data ? strlen(w.buf.data) : 4;
    if (w1 != w1_len || ferror(out)) {
        oc_status_set(st, OC_ERR_IO, "failed to write the create result");
        oc_jsonw_destroy(&w);
        return OC_ERR_IO;
    }
    int w2 = fprintf(out, ",\"created\":true,\"epoch\":%lld,\"sha256\":\"%s\"}\n", epoch, sha);
    if (w2 < 0 || ferror(out)) {
        oc_status_set(st, OC_ERR_IO, "failed to write the create result");
        oc_jsonw_destroy(&w);
        return OC_ERR_IO;
    }
    oc_jsonw_destroy(&w);
    return OC_OK;
}

static int reward_for_outcome(const char *outcome, double *reward) {
    if (strcmp(outcome, "accepted") == 0) {
        *reward = OC_BRAIN_REWARD_ACCEPTED;
        return 1;
    }
    if (strcmp(outcome, "fixed") == 0) {
        *reward = OC_BRAIN_REWARD_FIXED;
        return 1;
    }
    if (strcmp(outcome, "dismissed") == 0) {
        *reward = OC_BRAIN_REWARD_DISMISSED;
        return 1;
    }
    return 0;
}

/* Loads the report, gates the schema, resolves the finding message for
 * fingerprint (first match). Returns OC_OK with *message set (arena-
 * owned) or a fail-loud code. */
static oc_code brain_resolve_finding(oc_arena *a, const char *report_path, const char *fingerprint,
                                     const char **message, oc_status *st) {
    char *data = NULL;
    size_t len = 0;
    oc_code rc = oc_read_file(report_path, &data, &len, st);
    if (rc != OC_OK)
        return rc;
    oc_status pst;
    oc_status_clear(&pst);
    oc_json *root = oc_json_parse(a, data, len, &pst);
    free(data);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_PARSE, "'%s' is not valid JSON", report_path);
        return OC_ERR_PARSE;
    }
    const oc_json *meta = oc_json_get(root, "meta");
    const char *schema = oc_json_as_str(oc_json_get(meta, "schema"), NULL);
    if (!schema || strcmp(schema, "omnicheck.report/v0") != 0) {
        oc_status_set(st, OC_ERR_PARSE, "'%s' is not an omnicheck.report/v0 document", report_path);
        return OC_ERR_PARSE;
    }
    const oc_json *findings = oc_json_get(root, "findings");
    if (!findings || findings->type != OC_JSON_ARR) {
        oc_status_set(st, OC_ERR_PARSE, "'%s' has no findings array", report_path);
        return OC_ERR_PARSE;
    }
    for (size_t i = 0; i < findings->n_items; i++) {
        const oc_json *f = findings->items[i];
        if (!f || f->type != OC_JSON_OBJ)
            continue;
        const char *fp = oc_json_as_str(oc_json_get(f, "fingerprint"), NULL);
        if (fp && strcmp(fp, fingerprint) == 0) {
            *message = oc_json_as_str(oc_json_get(f, "message"), NULL);
            if (*message)
                return OC_OK;
        }
    }
    oc_status_set(st, OC_ERR_USAGE,
                  "fingerprint '%s' not found in '%s' (fail loud: recording "
                  "an outcome for an unknown finding would poison the "
                  "memory silently)",
                  fingerprint, report_path);
    return OC_ERR_USAGE;
}

oc_code oc_brain_cmd_record(const char *file, const char *report_path, const char *fingerprint,
                            const char *outcome, FILE *out, oc_status *st) {
    double reward = 0.0;
    if (!outcome || !reward_for_outcome(outcome, &reward)) {
        oc_status_set(st, OC_ERR_USAGE, "--outcome must be accepted|fixed|dismissed");
        return OC_ERR_USAGE;
    }
    if (!fingerprint || !fingerprint[0]) {
        oc_status_set(st, OC_ERR_USAGE, "--fingerprint is required");
        return OC_ERR_USAGE;
    }
    if (!report_path || !report_path[0]) {
        oc_status_set(st, OC_ERR_USAGE, "--report is required");
        return OC_ERR_USAGE;
    }

    /* Resolve the finding text from the report (memory keys are finding
     * texts under the one fixed instruction). */
    oc_arena a;
    oc_arena_init(&a);
    const char *message = NULL;
    oc_code rc = brain_resolve_finding(&a, report_path, fingerprint, &message, st);
    if (rc != OC_OK) {
        oc_arena_destroy(&a);
        return rc;
    }

    /* Prefer explicit file over environment/default; validate selected path. */
    char path[512];
    if (file && file[0]) {
        int n = snprintf(path, sizeof(path), "%s", file);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            oc_arena_destroy(&a);
            oc_status_set(st, OC_ERR_USAGE, "specified --brain-file path is too long");
            return OC_ERR_USAGE;
        }
    } else if (!brain_file_path(path, sizeof(path))) {
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_USAGE, "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_ERR_USAGE;
    }
    int existed = 0;
    odk_brain *brain = brain_open(path, &existed);
    if (!brain) {
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_PARSE,
                      "brain file '%s' is corrupt; record refused (fail loud "
                      "-- use 'oc brain reset' to start over)",
                      path);
        return OC_ERR_PARSE;
    }

    odk_error err;
    memset(&err, 0, sizeof(err));
    odk_status src =
        odk_brain_record_outcome(brain, "rank", OC_TRIAGE_INSTRUCTION, message, reward, &err);
    if (src != ODK_OK) {
        odk_brain_free(brain);
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_INTERNAL, "kernel refused the outcome: %s",
                      err.text[0] ? err.text : "unknown error");
        return OC_ERR_INTERNAL;
    }
    long long epoch = brain_epoch(brain);
    odk_status svr = odk_brain_save_file(brain, path, &err);
    odk_brain_free(brain);
    if (svr != ODK_OK) {
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_IO, "failed to save the brain file '%s': %s", path,
                      err.text[0] ? err.text : "kernel save failed");
        return OC_ERR_IO;
    }

    char sha[65] = "";
    if (!file_sha256(path, sha)) {
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_IO, "failed to hash the brain file '%s'", path);
        return OC_ERR_IO;
    }
    oc_strbuf b;
    oc_strbuf_init(&b);
    int ok = 1;
    if (oc_strbuf_appends(&b, "{\"schema\":\"omnicheck.brain.record/v0\","
                              "\"file\":") == 0)
        ok = 0;
    if (ok) {
        brain_json_string(&b, path);
        if (oc_strbuf_appends(&b, ",\"sha256\":") == 0)
            ok = 0;
    }
    if (ok) {
        brain_json_string(&b, sha);
        if (oc_strbuf_appends(&b, ",\"fingerprint\":") == 0)
            ok = 0;
    }
    if (ok) {
        brain_json_string(&b, fingerprint);
        if (oc_strbuf_appendf(&b,
                              ",\"outcome\":\"%s\",\"reward\":%.1f,"
                              "\"epoch\":%lld,\"saved\":true}\n",
                              outcome, reward, epoch) == 0)
            ok = 0;
    }
    oc_arena_destroy(&a);
    if (!ok || b.data == NULL) {
        oc_strbuf_destroy(&b);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory writing the record");
        return OC_ERR_ALLOC;
    }
    size_t w = fwrite(b.data, 1, b.len, out);
    int werr = (w != b.len) || ferror(out);
    oc_strbuf_destroy(&b);
    if (werr) {
        oc_status_set(st, OC_ERR_IO, "failed to write the record");
        return OC_ERR_IO;
    }
    return OC_OK;
}

oc_code oc_brain_cmd_save(const char *file, FILE *out, oc_status *st) {
    char path[512];
    /* Prefer explicit file over environment/default; validate selected path. */
    if (file && file[0]) {
        int n = snprintf(path, sizeof(path), "%s", file);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            oc_status_set(st, OC_ERR_USAGE, "specified --brain-file path is too long");
            return OC_ERR_USAGE;
        }
    } else if (!brain_file_path(path, sizeof(path))) {
        oc_status_set(st, OC_ERR_USAGE, "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_ERR_USAGE;
    }
    /* Save requires an existing brain; distinguish absent from corrupt. */
    if (!file_exists(path)) {
        oc_status_set(st, OC_ERR_USAGE,
                      "no brain store at '%s' (create one with 'oc brain create' "
                      "or record an outcome)",
                      path);
        return OC_ERR_USAGE;
    }
    odk_error err;
    memset(&err, 0, sizeof(err));
    odk_brain *brain = odk_brain_load_file(path, &err);
    if (!brain) {
        oc_status_set(st, OC_ERR_PARSE, "brain file '%s' is corrupt or unreadable: %s", path,
                      err.text[0] ? err.text : "kernel load failed");
        return OC_ERR_PARSE;
    }
    long long epoch = brain_epoch(brain);
    odk_status svr = odk_brain_save_file(brain, path, &err);
    odk_brain_free(brain);
    if (svr != ODK_OK) {
        oc_status_set(st, OC_ERR_IO, "failed to save the brain file '%s': %s", path,
                      err.text[0] ? err.text : "kernel save failed");
        return OC_ERR_IO;
    }
    char sha[65] = "";
    if (!file_sha256(path, sha)) {
        oc_status_set(st, OC_ERR_IO, "failed to hash the brain file '%s'", path);
        return OC_ERR_IO;
    }
    fprintf(out, "{\"schema\":\"omnicheck.brain.save/v0\",\"file\":");
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_str(&w, path);
    size_t w1 =
        fwrite(w.buf.data ? w.buf.data : "null", 1, w.buf.data ? strlen(w.buf.data) : 4, out);
    size_t w1_len = w.buf.data ? strlen(w.buf.data) : 4;
    if (w1 != w1_len || ferror(out)) {
        oc_jsonw_destroy(&w);
        oc_status_set(st, OC_ERR_IO,
                      "failed to write the save result (brain store was saved to '%s')", path);
        return OC_ERR_IO;
    }
    oc_jsonw_destroy(&w);
    int w2 = fprintf(out,
                     ",\"saved\":true,\"existed\":true,\"epoch\":%lld,"
                     "\"sha256\":\"%s\"}\n",
                     epoch, sha);
    if (w2 < 0 || ferror(out)) {
        oc_status_set(st, OC_ERR_IO,
                      "failed to write the save result (brain store was saved to '%s')", path);
        return OC_ERR_IO;
    }
    return OC_OK;
}

oc_code oc_brain_cmd_advise(const char *file, const char *report_path, FILE *out, oc_status *st) {
    if (!report_path || !report_path[0]) {
        oc_status_set(st, OC_ERR_USAGE, "advise requires --report REPORT.json");
        return OC_ERR_USAGE;
    }
    char path[512];
    /* Prefer explicit file over environment/default; validate selected path. */
    if (file && file[0]) {
        int n = snprintf(path, sizeof(path), "%s", file);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            oc_status_set(st, OC_ERR_USAGE, "specified --brain-file path is too long");
            return OC_ERR_USAGE;
        }
    } else if (!brain_file_path(path, sizeof(path))) {
        oc_status_set(st, OC_ERR_USAGE, "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_ERR_USAGE;
    }

    oc_arena a;
    oc_arena_init(&a);
    char *data = NULL;
    size_t len = 0;
    oc_code rc = oc_read_file(report_path, &data, &len, st);
    if (rc != OC_OK) {
        oc_arena_destroy(&a);
        return rc;
    }
    oc_status pst;
    oc_status_clear(&pst);
    oc_json *root = oc_json_parse(&a, data, len, &pst);
    free(data);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_PARSE, "'%s' is not valid JSON", report_path);
        return OC_ERR_PARSE;
    }
    const oc_json *meta = oc_json_get(root, "meta");
    const char *schema = oc_json_as_str(oc_json_get(meta, "schema"), NULL);
    if (!schema || strcmp(schema, "omnicheck.report/v0") != 0) {
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_PARSE, "'%s' is not an omnicheck.report/v0 document", report_path);
        return OC_ERR_PARSE;
    }
    const oc_json *findings = oc_json_get(root, "findings");
    if (!findings || findings->type != OC_JSON_ARR) {
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_PARSE, "'%s' has no findings array", report_path);
        return OC_ERR_PARSE;
    }
    size_t n = findings->n_items;
    const char **fps = (const char **)calloc(n ? n : 1, sizeof(*fps));
    const char **texts = (const char **)calloc(n ? n : 1, sizeof(*texts));
    double *confs = (double *)calloc(n ? n : 1, sizeof(*confs));
    double *bonuses = (double *)calloc(n ? n : 1, sizeof(*bonuses));
    if (!fps || !texts || !confs || !bonuses) {
        free(fps);
        free(texts);
        free(confs);
        free(bonuses);
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory preparing the advise");
        return OC_ERR_ALLOC;
    }
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        const oc_json *f = findings->items[i];
        if (!f || f->type != OC_JSON_OBJ)
            continue;
        const char *fp = oc_json_as_str(oc_json_get(f, "fingerprint"), NULL);
        const char *msg = oc_json_as_str(oc_json_get(f, "message"), NULL);
        if (!fp || !msg)
            continue;
        double conf = oc_json_as_num(oc_json_get(f, "triage_confidence"), 0.0);
        if (conf < 0.0 || conf > 1.0) {
            oc_status_set(st, OC_ERR_PARSE, "finding '%s' has triage_confidence outside [0,1]", fp);
            free(fps);
            free(texts);
            free(confs);
            free(bonuses);
            oc_arena_destroy(&a);
            return OC_ERR_PARSE;
        }
        fps[m] = fp;
        texts[m] = msg;
        confs[m] = conf;
        m++;
    }

    int existed = file_exists(path) ? 1 : 0;
    long long epoch = -1;
    if (existed) {
        odk_error err;
        memset(&err, 0, sizeof(err));
        odk_brain *brain = odk_brain_load_file(path, &err);
        if (!brain) {
            oc_status_set(st, OC_ERR_PARSE, "brain file '%s' is corrupt or unreadable: %s", path,
                          err.text[0] ? err.text : "kernel load failed");
            free(fps);
            free(texts);
            free(confs);
            free(bonuses);
            oc_arena_destroy(&a);
            return OC_ERR_PARSE;
        }
        epoch = brain_epoch(brain);
        memset(&err, 0, sizeof(err));
        odk_status rc2 =
            odk_brain_advise(brain, "rank", OC_TRIAGE_INSTRUCTION, texts, m, bonuses, &err);
        odk_brain_free(brain);
        if (rc2 != ODK_OK) {
            oc_status_set(st, OC_ERR_INTERNAL, "brain advise failed: %s",
                          err.text[0] ? err.text : "unknown error");
            free(fps);
            free(texts);
            free(confs);
            free(bonuses);
            oc_arena_destroy(&a);
            return OC_ERR_INTERNAL;
        }
    }

    oc_strbuf b;
    oc_strbuf_init(&b);
    int ok = 1;
    if (oc_strbuf_appends(&b, "{\"schema\":\"omnicheck.brain.advise/v0\","
                              "\"file\":") == 0)
        ok = 0;
    if (ok) {
        brain_json_string(&b, path);
        if (oc_strbuf_appendf(&b,
                              ",\"exists\":%s,\"epoch\":%lld,"
                              "\"weight\":%.1f,\"items\":[",
                              existed ? "true" : "false", epoch, OC_BRAIN_WEIGHT) == 0)
            ok = 0;
    }
    for (size_t i = 0; ok && i < m; i++) {
        double c = confs[i] + OC_BRAIN_WEIGHT * bonuses[i];
        if (c < 0.0)
            c = 0.0;
        if (c > 1.0)
            c = 1.0;
        if (i > 0 && oc_strbuf_appendc(&b, ',') == 0)
            ok = 0;
        if (ok) {
            oc_strbuf_appends(&b, "{\"fingerprint\":");
            brain_json_string(&b, fps[i]);
            if (oc_strbuf_appendf(&b,
                                  ",\"bonus\":%.17g,"
                                  "\"confidence\":%.4f,"
                                  "\"adjusted\":%.17g}",
                                  bonuses[i], confs[i], c) == 0)
                ok = 0;
        }
    }
    if (ok && oc_strbuf_appends(&b, "]}\n") == 0)
        ok = 0;
    free(fps);
    free(texts);
    free(confs);
    free(bonuses);
    oc_arena_destroy(&a);

    if (!ok || b.data == NULL) {
        oc_strbuf_destroy(&b);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory writing the advise");
        return OC_ERR_ALLOC;
    }
    size_t w = fwrite(b.data, 1, b.len, out);
    int werr = (w != b.len) || ferror(out);
    oc_strbuf_destroy(&b);
    if (werr) {
        oc_status_set(st, OC_ERR_IO, "failed to write the advise document");
        return OC_ERR_IO;
    }
    return OC_OK;
}

oc_code oc_brain_cmd_entries(const char *file, FILE *out, oc_status *st) {
    char path[512];
    /* Prefer explicit file over environment/default; validate selected path. */
    if (file && file[0]) {
        int n = snprintf(path, sizeof(path), "%s", file);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            oc_status_set(st, OC_ERR_USAGE, "specified --brain-file path is too long");
            return OC_ERR_USAGE;
        }
    } else if (!brain_file_path(path, sizeof(path))) {
        oc_status_set(st, OC_ERR_USAGE, "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_ERR_USAGE;
    }
    if (!file_exists(path)) {
        oc_status_set(st, OC_ERR_USAGE,
                      "no brain store at '%s' (create one with 'oc brain "
                      "create' or record an outcome)",
                      path);
        return OC_ERR_USAGE;
    }
    odk_error err;
    memset(&err, 0, sizeof(err));
    odk_brain *brain = odk_brain_load_file(path, &err);
    if (!brain) {
        oc_status_set(st, OC_ERR_PARSE, "brain file '%s' is corrupt or unreadable: %s", path,
                      err.text[0] ? err.text : "kernel load failed");
        return OC_ERR_PARSE;
    }
    long long epoch = brain_epoch(brain);
    char *entries = odk_brain_entries_report_alloc(brain);
    odk_brain_free(brain);
    if (!entries) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory listing the entries");
        return OC_ERR_ALLOC;
    }
    char sha[65] = "";
    if (!file_sha256(path, sha)) {
        free(entries);
        oc_status_set(st, OC_ERR_IO, "failed to hash the brain file '%s'", path);
        return OC_ERR_IO;
    }
    fprintf(out, "{\"schema\":\"omnicheck.brain.entries/v0\",\"file\":");
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_str(&w, path);
    size_t w1 =
        fwrite(w.buf.data ? w.buf.data : "null", 1, w.buf.data ? strlen(w.buf.data) : 4, out);
    size_t w1_len = w.buf.data ? strlen(w.buf.data) : 4;
    if (w1 != w1_len || ferror(out)) {
        oc_status_set(st, OC_ERR_IO, "failed to write the entries document");
        return OC_ERR_IO;
    }
    int w2 = fprintf(out, ",\"sha256\":\"%s\",\"epoch\":%lld,\"entries_report\":", sha, epoch);
    if (w2 < 0 || ferror(out)) {
        oc_status_set(st, OC_ERR_IO, "failed to write the entries document");
        return OC_ERR_IO;
    }
    oc_jsonw_destroy(&w);
    size_t w3 = fwrite(entries, 1, strlen(entries), out);
    int w4 = fprintf(out, "}\n");
    if (w3 != strlen(entries) || w4 < 0 || ferror(out)) {
        oc_status_set(st, OC_ERR_IO, "failed to write the entries document");
        return OC_ERR_IO;
    }
    free(entries);
    return OC_OK;
}

oc_code oc_brain_cmd_discard(const char *file, long index, FILE *out, oc_status *st) {
    char path[512];
    /* Prefer explicit file over environment/default; validate selected path. */
    if (file && file[0]) {
        int n = snprintf(path, sizeof(path), "%s", file);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            oc_status_set(st, OC_ERR_USAGE, "specified --brain-file path is too long");
            return OC_ERR_USAGE;
        }
    } else if (!brain_file_path(path, sizeof(path))) {
        oc_status_set(st, OC_ERR_USAGE, "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_ERR_USAGE;
    }
    if (!file_exists(path)) {
        oc_status_set(st, OC_ERR_USAGE, "no brain store at '%s'", path);
        return OC_ERR_USAGE;
    }
    if (index < 0) {
        oc_status_set(st, OC_ERR_USAGE,
                      "--index requires a non-negative entry index (see 'oc "
                      "brain entries')");
        return OC_ERR_USAGE;
    }
    odk_error err;
    memset(&err, 0, sizeof(err));
    odk_brain *brain = odk_brain_load_file(path, &err);
    if (!brain) {
        oc_status_set(st, OC_ERR_PARSE, "brain file '%s' is corrupt or unreadable: %s", path,
                      err.text[0] ? err.text : "kernel load failed");
        return OC_ERR_PARSE;
    }
    /* Validate index against actual entry count to avoid truncation
     * when casting long -> size_t on 32-bit platforms. */
    char *entries_json = odk_brain_entries_report_alloc(brain);
    size_t n_entries = 0;
    if (entries_json) {
        oc_arena a;
        oc_arena_init(&a);
        oc_status st;
        oc_status_clear(&st);
        oc_json *root = oc_json_parse(&a, entries_json, strlen(entries_json), &st);
        if (root && root->type == OC_JSON_OBJ) {
            const oc_json *me = oc_json_get(root, "memory_entries");
            if (me && me->type == OC_JSON_NUM)
                n_entries = (size_t)me->number;
        }
        oc_arena_destroy(&a);
        free(entries_json);
    }
    if (n_entries > 0 && (size_t)index >= n_entries) {
        odk_brain_free(brain);
        oc_status_set(st, OC_ERR_USAGE, "--index %ld out of range (brain has %zu entries)", index,
                      n_entries);
        return OC_ERR_USAGE;
    }
    odk_status rc = odk_brain_discard_entry(brain, (size_t)index, &err);
    if (rc != ODK_OK) {
        odk_brain_free(brain);
        oc_status_set(st, OC_ERR_USAGE, "kernel refused the discard: %s",
                      err.text[0] ? err.text : "unknown index");
        return OC_ERR_USAGE;
    }
    long long epoch = brain_epoch(brain);
    odk_status svr = odk_brain_save_file(brain, path, &err);
    odk_brain_free(brain);
    if (svr != ODK_OK) {
        oc_status_set(st, OC_ERR_IO, "failed to save the brain file '%s': %s", path,
                      err.text[0] ? err.text : "kernel save failed");
        return OC_ERR_IO;
    }
    char sha[65] = "";
    if (!file_sha256(path, sha)) {
        oc_status_set(st, OC_ERR_IO, "failed to hash the brain file '%s'", path);
        return OC_ERR_IO;
    }
    fprintf(out, "{\"schema\":\"omnicheck.brain.discard/v0\",\"file\":");
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_str(&w, path);
    size_t w1 =
        fwrite(w.buf.data ? w.buf.data : "null", 1, w.buf.data ? strlen(w.buf.data) : 4, out);
    size_t w1_len = w.buf.data ? strlen(w.buf.data) : 4;
    if (w1 != w1_len || ferror(out)) {
        oc_jsonw_destroy(&w);
        oc_status_set(st, OC_ERR_IO,
                      "failed to write the discard document (brain store was discarded to '%s')",
                      path);
        return OC_ERR_IO;
    }
    oc_jsonw_destroy(&w);
    int w2 = fprintf(out,
                     ",\"index\":%ld,\"discarded\":true,\"epoch\":%lld,"
                     "\"sha256\":\"%s\"}\n",
                     index, epoch, sha);
    if (w2 < 0 || ferror(out)) {
        oc_status_set(st, OC_ERR_IO,
                      "failed to write the discard document (brain store was discarded to '%s')",
                      path);
        return OC_ERR_IO;
    }
    return OC_OK;
}

oc_code oc_brain_cmd_reset(const char *file, FILE *out, oc_status *st) {
    char path[512];
    /* Prefer explicit file over environment/default; validate selected path. */
    if (file && file[0]) {
        int n = snprintf(path, sizeof(path), "%s", file);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            oc_status_set(st, OC_ERR_USAGE, "specified --brain-file path is too long");
            return OC_ERR_USAGE;
        }
    } else if (!brain_file_path(path, sizeof(path))) {
        oc_status_set(st, OC_ERR_USAGE, "brain file path overflow (OC_BRAIN_FILE too long)");
        return OC_ERR_USAGE;
    }
    int existed = file_exists(path) ? 1 : 0;
    int removed = 0;
    if (existed)
        removed = (remove(path) == 0) ? 1 : 0;
    /* Check removal failure before writing output. */
    if (existed && !removed) {
        oc_status_set(st, OC_ERR_IO, "failed to remove the brain file '%s'", path);
        return OC_ERR_IO;
    }
    fprintf(out, "{\"schema\":\"omnicheck.brain.reset/v0\",\"file\":");
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_str(&w, path);
    size_t w1 =
        fwrite(w.buf.data ? w.buf.data : "null", 1, w.buf.data ? strlen(w.buf.data) : 4, out);
    size_t w1_len = w.buf.data ? strlen(w.buf.data) : 4;
    if (w1 != w1_len || ferror(out)) {
        oc_jsonw_destroy(&w);
        oc_status_set(st, OC_ERR_IO, "failed to write the reset result for '%s'", path);
        return OC_ERR_IO;
    }
    int w2 = fprintf(out, ",\"removed\":%s}\n", removed ? "true" : "false");
    if (w2 < 0 || ferror(out)) {
        oc_jsonw_destroy(&w);
        oc_status_set(st, OC_ERR_IO, "failed to write the reset result for '%s'", path);
        return OC_ERR_IO;
    }
    oc_jsonw_destroy(&w);
    return OC_OK;
}

#endif /* OC_EMBED_OMNIDECIDE */
