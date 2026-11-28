#include "oc/reportdiff.h"

#include "oc/json.h"
#include "oc/str.h"
#include "oc/triage_embed.h"

#include <stdlib.h>
#include <string.h>

/*
 * T3 implementation. All comparisons are fingerprint multiset
 * comparisons (see the header for the contract); line numbers and
 * messages are carried for human-readable output only; the KERNEL never
 * decides identity. Every qsort below is guarded for NULL/empty inputs
 * (the M-10 hygiene rule), every growable is bounded by
 * OC_REPORTDIFF_MAX_FINDINGS, and the document is byte-deterministic:
 * fixed field order, fingerprint-asc ordering, no timestamps.
 */

/* Hostile-input budget: reports beyond this size refuse loudly. */
#define OC_REPORTDIFF_MAX_FINDINGS 200000u

typedef struct {
    const char *fp; /* fingerprint (arena-owned, borrowed) */
    const char *rule;
    const char *file;
    long line;
    const char *message;
    size_t occ;   /* k-th occurrence of this fp on this side */
    size_t count; /* total occurrences of this fp on this side */
} rd_entry;

/* Sorted view over the entries: (fp asc, occ asc) — (fp, occ) is unique. */
typedef struct {
    rd_entry *e;
    size_t n;
} rd_side;

static int rd_cmp_entry(const void *pa, const void *pb) {
    const rd_entry *a = (const rd_entry *)pa;
    const rd_entry *b = (const rd_entry *)pb;
    int c = strcmp(a->fp, b->fp);
    if (c != 0)
        return c;
    return (a->occ < b->occ) ? -1 : (a->occ > b->occ) ? 1 : 0;
}

/* JSON string emitter with defensive escaping (never trust the wire). */
static void rd_json_string(oc_strbuf *b, const char *s) {
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

/* Loads one side: parse, schema-gate, collect entries, assign occ/count.
 * The entries array and the arena are owned by the caller. */
static oc_code rd_load_side(const char *path, oc_arena *a, rd_entry **out_e, size_t *out_n,
                            size_t *out_finding_count, oc_status *st) {
    char *data = NULL;
    size_t len = 0;
    oc_code rc = oc_read_file(path, &data, &len, st);
    if (rc != OC_OK)
        return rc;

    oc_status pst;
    oc_status_clear(&pst);
    oc_json *root = oc_json_parse(a, data, len, &pst);
    free(data); /* parsed or not, the raw bytes are no longer needed */
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_PARSE, "'%s' is not valid JSON", path);
        return OC_ERR_PARSE;
    }
    const oc_json *meta = oc_json_get(root, "meta");
    const char *schema = oc_json_as_str(oc_json_get(meta, "schema"), NULL);
    if (!schema || strcmp(schema, "omnicheck.report/v0") != 0) {
        oc_status_set(st, OC_ERR_PARSE,
                      "'%s' is not an omnicheck.report/v0 document; oc diff "
                      "compares OmniCheck reports",
                      path);
        return OC_ERR_PARSE;
    }
    const oc_json *findings = oc_json_get(root, "findings");
    if (!findings || findings->type != OC_JSON_ARR) {
        oc_status_set(st, OC_ERR_PARSE, "'%s' has no findings array", path);
        return OC_ERR_PARSE;
    }
    size_t n = findings->n_items;
    if (n > OC_REPORTDIFF_MAX_FINDINGS) {
        oc_status_set(st, OC_ERR_USAGE,
                      "'%s' exceeds the diff budget (%u findings); refusing "
                      "loudly",
                      path, (unsigned)OC_REPORTDIFF_MAX_FINDINGS);
        return OC_ERR_USAGE;
    }
    *out_finding_count = n;

    rd_entry *e = (rd_entry *)calloc(n > 0 ? n : 1, sizeof(*e));
    if (!e) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory loading '%s'", path);
        return OC_ERR_ALLOC;
    }
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        const oc_json *f = findings->items[i];
        if (!f || f->type != OC_JSON_OBJ)
            continue;
        const char *fp = oc_json_as_str(oc_json_get(f, "fingerprint"), NULL);
        if (!fp)
            continue; /* unusable finding: not comparable, skipped */
        e[m].fp = fp;
        e[m].rule = oc_json_as_str(oc_json_get(f, "rule"), "");
        e[m].file = oc_json_as_str(oc_json_get(f, "file"), "");
        e[m].line = (long)oc_json_as_num(oc_json_get(f, "line"), 0.0);
        e[m].message = oc_json_as_str(oc_json_get(f, "message"), "");
        m++;
    }
    /* occ + count: set occ to the ORIGINAL position first, then sort by
     * (fp, occ) -- the tie-break preserves first-occurrence order within
     * each fp -- and finally rewrite occ as the k-th occurrence within
     * the fp run while recording the run count. */
    if (m > 0) {
        for (size_t i = 0; i < m; i++)
            e[i].occ = i; /* original order, temporarily */
        qsort(e, m, sizeof(*e), rd_cmp_entry);
        /* The comparator ties break on occ (original index), so entries
         * of one fp keep their first-occurrence order. Rewrite occ as the
         * k-th occurrence within the fp run and store the run count. */
        size_t i = 0;
        while (i < m) {
            size_t j = i;
            while (j < m && strcmp(e[j].fp, e[i].fp) == 0)
                j++;
            size_t run = j - i;
            for (size_t k = i; k < j; k++)
                e[k].occ = k - i;
            for (size_t k = i; k < j; k++)
                e[k].count = run;
            i = j;
        }
    }
    *out_e = e;
    *out_n = m;
    return OC_OK;
}

#if defined(OC_EMBED_OMNIDECIDE)
/* --semantic advisory scores: ONE rank call over ALL current-side
 * findings (sorted order: fp asc, occ asc -- a deterministic option
 * order), then a per-entry advisory {rank, confidence} lookup. ADVISORY
 * ONLY: the classification happened before this runs and the exit gate
 * never reads these numbers. */
static oc_code rd_semantic_scores(const rd_entry *ec, size_t nc, long **out_rank, double **out_conf,
                                  oc_status *st) {
    long *rank = (long *)calloc(nc > 0 ? nc : 1, sizeof(*rank));
    double *conf = (double *)calloc(nc > 0 ? nc : 1, sizeof(*conf));
    if (!rank || !conf) {
        free(rank);
        free(conf);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory preparing the advisory scores");
        return OC_ERR_ALLOC;
    }
    for (size_t i = 0; i < nc; i++)
        rank[i] = -1;

    /* Build the protocol-v0 request: texts become rank options (prose
     * only), ids are fingerprints. Duplicate fingerprints (identical
     * siblings) appear once per occurrence; the first response hit wins
     * (deterministic). */
    oc_strbuf req;
    oc_strbuf_init(&req);
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_obj_begin(&w);
    oc_jsonw_key(&w, "op");
    oc_jsonw_str(&w, "rank");
    oc_jsonw_key(&w, "items");
    oc_jsonw_arr_begin(&w);
    for (size_t i = 0; i < nc; i++) {
        oc_jsonw_obj_begin(&w);
        oc_jsonw_key(&w, "id");
        oc_jsonw_str(&w, ec[i].fp);
        oc_jsonw_key(&w, "text");
        oc_jsonw_str(&w, ec[i].message);
        oc_jsonw_obj_end(&w);
    }
    oc_jsonw_arr_end(&w);
    oc_jsonw_obj_end(&w);
    oc_strbuf_appends(&req, w.buf.data ? w.buf.data : "");
    int ok = w.buf.data != NULL;
    oc_jsonw_destroy(&w);
    if (!ok) {
        oc_strbuf_destroy(&req);
        free(rank);
        free(conf);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building the advisory request");
        return OC_ERR_ALLOC;
    }

    char *resp = NULL;
    size_t resp_len = 0;
    char why[192];
    why[0] = '\0';
    int rok = oc_triage_embed_respond(req.data, req.len, &resp, &resp_len, why, sizeof(why));
    oc_strbuf_destroy(&req);
    if (!rok) {
        free(rank);
        free(conf);
        oc_status_set(st, OC_ERR_INTERNAL, "advisory rank call failed: %s",
                      why[0] ? why : "embedded provider failed");
        return OC_ERR_INTERNAL;
    }

    /* Map response ranks onto entries: the response walks the ranked
     * order and emits {id, rank, confidence}; binary-search the FIRST
     * entry with the id (ec is sorted by (fp, occ)). */
    oc_arena a;
    oc_arena_init(&a);
    oc_status pst;
    oc_status_clear(&pst);
    oc_json *root = oc_json_parse(&a, resp, resp_len, &pst);
    free(resp);
    int parse_ok = (root && root->type == OC_JSON_OBJ);
    const oc_json *ranks = parse_ok ? oc_json_get(root, "ranks") : NULL;
    if (parse_ok && ranks && ranks->type == OC_JSON_ARR) {
        for (size_t i = 0; i < ranks->n_items; i++) {
            const oc_json *r = ranks->items[i];
            const char *id = oc_json_as_str(oc_json_get(r, "id"), NULL);
            if (!id)
                continue;
            size_t lo = 0, hi = nc;
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (strcmp(ec[mid].fp, id) < 0)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo < nc && strcmp(ec[lo].fp, id) == 0 && rank[lo] < 0) {
                rank[lo] = (long)oc_json_as_num(oc_json_get(r, "rank"), -1.0);
                conf[lo] = oc_json_as_num(oc_json_get(r, "confidence"), 0.0);
            }
        }
    }
    oc_arena_destroy(&a);
    if (!parse_ok) {
        free(rank);
        free(conf);
        oc_status_set(st, OC_ERR_INTERNAL, "advisory rank response was not valid JSON");
        return OC_ERR_INTERNAL;
    }
    *out_rank = rank;
    *out_conf = conf;
    return OC_OK;
}
#endif /* OC_EMBED_OMNIDECIDE */

oc_code oc_reportdiff_run(const char *baseline_path, const char *current_path, int want_semantic,
                          int *semantic_changes, FILE *out, oc_status *st) {
    oc_arena a;
    oc_arena_init(&a);
    rd_entry *eb = NULL, *ec = NULL;
    size_t nb = 0, nc = 0, fcb = 0, fcc = 0;
    oc_code rc = rd_load_side(baseline_path, &a, &eb, &nb, &fcb, st);
    if (rc != OC_OK) {
        oc_arena_destroy(&a);
        return rc;
    }
    rc = rd_load_side(current_path, &a, &ec, &nc, &fcc, st);
    if (rc != OC_OK) {
        free(eb);
        oc_arena_destroy(&a);
        return rc;
    }

#if defined(OC_EMBED_OMNIDECIDE)
    /* --semantic advisory scores over the current side (see the header
     * for the advisory contract). */
    long *adv_rank = NULL;
    double *adv_conf = NULL;
    if (want_semantic) {
        rc = rd_semantic_scores(ec, nc, &adv_rank, &adv_conf, st);
        if (rc != OC_OK) {
            free(eb);
            free(ec);
            oc_arena_destroy(&a);
            return rc;
        }
    }
#else
    long *adv_rank = NULL;
    double *adv_conf = NULL;
    if (want_semantic) {
        free(eb);
        free(ec);
        oc_arena_destroy(&a);
        oc_status_set(st, OC_ERR_USAGE,
                      "--semantic requires the embedded OmniDecide kernel; "
                      "rebuild with -DOC_EMBED_OMNIDECIDE=ON");
        return OC_ERR_USAGE;
    }
#endif

    /* Walk the sorted arrays; emit introduced/resolved/moved. */
    oc_strbuf b;
    oc_strbuf_init(&b);
    int ok = 1;
    size_t n_introduced = 0, n_resolved = 0, n_moved = 0;
    size_t persistent = 0;

    /* pass 0: persistent + moved count */
    {
        size_t i = 0, j = 0;
        while (ok && (i < nb || j < nc)) {
            int c = 0;
            if (i < nb && j < nc)
                c = strcmp(eb[i].fp, ec[j].fp);
            else if (i < nb)
                c = -1;
            else
                c = 1;
            if (c == 0) {
                size_t cb = eb[i].count, cc = ec[j].count;
                persistent += (cb < cc) ? cb : cc;
                size_t mv = (cb < cc) ? cb : cc;
                for (size_t k = 0; ok && k < mv; k++) {
                    if (eb[i + k].line != ec[j + k].line)
                        n_moved++;
                }
                i += cb;
                j += cc;
            } else if (c < 0) {
                i += eb[i].count;
            } else {
                j += ec[j].count;
            }
        }
    }

    if (oc_strbuf_appends(&b, "{\"schema\":\"omnicheck.reportdiff/v0\",\"tool\":\"omnicheck\","
                              "\"version\":\"") == 0)
        ok = 0;
    if (ok && oc_strbuf_appends(&b, OC_VERSION_STRING) == 0)
        ok = 0;
    if (ok && oc_strbuf_appends(&b, "\",\"semantic\":") == 0)
        ok = 0;
    if (ok && oc_strbuf_appends(&b, want_semantic ? "true" : "false") == 0)
        ok = 0;
    if (ok && oc_strbuf_appends(&b, ",\"baseline\":{\"path\":") == 0)
        ok = 0;
    if (ok) {
        rd_json_string(&b, baseline_path);
        if (oc_strbuf_appendf(&b,
                              ",\"finding_count\":%zu},\"current\":"
                              "{\"path\":",
                              fcb) == 0)
            ok = 0;
    }
    if (ok) {
        rd_json_string(&b, current_path);
        if (oc_strbuf_appendf(&b, ",\"finding_count\":%zu}", fcc) == 0)
            ok = 0;
    }
    if (ok && oc_strbuf_appends(&b, ",\"introduced\":[") == 0)
        ok = 0;
    /* pass 1: introduced — fps only in current (or more often there) */
    if (ok) {
        size_t i = 0, j = 0, emitted = 0;
        while (j < nc) {
            int c;
            if (i < nb)
                c = strcmp(eb[i].fp, ec[j].fp);
            else
                c = 1;
            if (c == 0) {
                size_t cb = eb[i].count, cc = ec[j].count;
                for (size_t k = cb; ok && k < cc; k++) {
                    const rd_entry *e = &ec[j + k];
                    if (emitted > 0 && oc_strbuf_appendc(&b, ',') == 0) {
                        ok = 0;
                        break;
                    }
                    if (ok) {
                        oc_strbuf_appends(&b, "{\"fingerprint\":");
                        rd_json_string(&b, e->fp);
                        oc_strbuf_appends(&b, ",\"rule\":");
                        rd_json_string(&b, e->rule);
                        oc_strbuf_appends(&b, ",\"file\":");
                        rd_json_string(&b, e->file);
                        if (oc_strbuf_appendf(&b, ",\"line\":%ld,\"message\":", e->line) == 0) {
                            ok = 0;
                            break;
                        }
                        rd_json_string(&b, e->message);
                        if (adv_rank && adv_rank[j + k] >= 0) {
                            if (oc_strbuf_appendf(&b,
                                                  ",\"advisory\":{\"rank\":%ld,"
                                                  "\"confidence\":%.4f}",
                                                  adv_rank[j + k], adv_conf[j + k]) == 0) {
                                ok = 0;
                                break;
                            }
                        }
                        if (oc_strbuf_appendc(&b, '}') == 0) {
                            ok = 0;
                            break;
                        }
                        emitted++;
                    }
                }
                i += cb;
                j += cc;
            } else if (c < 0) {
                i += eb[i].count;
            } else {
                size_t cc = ec[j].count;
                for (size_t k = 0; ok && k < cc; k++) {
                    const rd_entry *e = &ec[j + k];
                    if (emitted > 0 && oc_strbuf_appendc(&b, ',') == 0) {
                        ok = 0;
                        break;
                    }
                    if (ok) {
                        oc_strbuf_appends(&b, "{\"fingerprint\":");
                        rd_json_string(&b, e->fp);
                        oc_strbuf_appends(&b, ",\"rule\":");
                        rd_json_string(&b, e->rule);
                        oc_strbuf_appends(&b, ",\"file\":");
                        rd_json_string(&b, e->file);
                        if (oc_strbuf_appendf(&b, ",\"line\":%ld,\"message\":", e->line) == 0) {
                            ok = 0;
                            break;
                        }
                        rd_json_string(&b, e->message);
                        if (adv_rank && adv_rank[j + k] >= 0) {
                            if (oc_strbuf_appendf(&b,
                                                  ",\"advisory\":{\"rank\":%ld,"
                                                  "\"confidence\":%.4f}",
                                                  adv_rank[j + k], adv_conf[j + k]) == 0) {
                                ok = 0;
                                break;
                            }
                        }
                        if (oc_strbuf_appendc(&b, '}') == 0) {
                            ok = 0;
                            break;
                        }
                        emitted++;
                    }
                }
                j += cc;
            }
            if (!ok)
                break;
        }
        n_introduced = emitted;
    }
    if (ok && oc_strbuf_appends(&b, "],\"resolved\":[") == 0)
        ok = 0;
    /* pass 2: resolved — fps only in baseline (or more often there) */
    if (ok) {
        size_t i = 0, j = 0, emitted = 0;
        while (i < nb) {
            int c;
            if (j < nc)
                c = strcmp(eb[i].fp, ec[j].fp);
            else
                c = -1;
            if (c == 0) {
                size_t cb = eb[i].count, cc = ec[j].count;
                for (size_t k = cc; ok && k < cb; k++) {
                    const rd_entry *e = &eb[i + k];
                    if (emitted > 0 && oc_strbuf_appendc(&b, ',') == 0) {
                        ok = 0;
                        break;
                    }
                    if (ok) {
                        oc_strbuf_appends(&b, "{\"fingerprint\":");
                        rd_json_string(&b, e->fp);
                        oc_strbuf_appends(&b, ",\"rule\":");
                        rd_json_string(&b, e->rule);
                        oc_strbuf_appends(&b, ",\"file\":");
                        rd_json_string(&b, e->file);
                        if (oc_strbuf_appendf(&b, ",\"line\":%ld,\"message\":", e->line) == 0) {
                            ok = 0;
                            break;
                        }
                        rd_json_string(&b, e->message);
                        if (oc_strbuf_appendc(&b, '}') == 0) {
                            ok = 0;
                            break;
                        }
                        emitted++;
                    }
                }
                i += cb;
                j += cc;
            } else if (c < 0) {
                size_t cb = eb[i].count;
                for (size_t k = 0; ok && k < cb; k++) {
                    const rd_entry *e = &eb[i + k];
                    if (emitted > 0 && oc_strbuf_appendc(&b, ',') == 0) {
                        ok = 0;
                        break;
                    }
                    if (ok) {
                        oc_strbuf_appends(&b, "{\"fingerprint\":");
                        rd_json_string(&b, e->fp);
                        oc_strbuf_appends(&b, ",\"rule\":");
                        rd_json_string(&b, e->rule);
                        oc_strbuf_appends(&b, ",\"file\":");
                        rd_json_string(&b, e->file);
                        if (oc_strbuf_appendf(&b, ",\"line\":%ld,\"message\":", e->line) == 0) {
                            ok = 0;
                            break;
                        }
                        rd_json_string(&b, e->message);
                        if (oc_strbuf_appendc(&b, '}') == 0) {
                            ok = 0;
                            break;
                        }
                        emitted++;
                    }
                }
                i += cb;
            } else {
                j += ec[j].count;
            }
            if (!ok)
                break;
        }
        n_resolved = emitted;
    }
    if (ok && oc_strbuf_appendf(&b, "],\"persistent\":%zu,\"moved\":", persistent) == 0)
        ok = 0;
    if (ok && oc_strbuf_appends(&b, "[") == 0)
        ok = 0;
    /* pass 3: moved — paired occurrences with differing lines */
    if (ok) {
        size_t i = 0, j = 0, emitted = 0;
        while (ok && (i < nb && j < nc)) {
            int c = strcmp(eb[i].fp, ec[j].fp);
            if (c == 0) {
                size_t cb = eb[i].count, cc = ec[j].count;
                size_t mv = (cb < cc) ? cb : cc;
                for (size_t k = 0; ok && k < mv; k++) {
                    if (eb[i + k].line == ec[j + k].line)
                        continue;
                    if (emitted > 0 && oc_strbuf_appendc(&b, ',') == 0) {
                        ok = 0;
                        break;
                    }
                    if (ok) {
                        oc_strbuf_appends(&b, "{\"fingerprint\":");
                        rd_json_string(&b, eb[i + k].fp);
                        oc_strbuf_appends(&b, ",\"rule\":");
                        rd_json_string(&b, eb[i + k].rule);
                        oc_strbuf_appends(&b, ",\"file\":");
                        rd_json_string(&b, eb[i + k].file);
                        if (oc_strbuf_appendf(&b,
                                              ",\"baseline_line\":%ld,"
                                              "\"current_line\":%ld}",
                                              eb[i + k].line, ec[j + k].line) == 0) {
                            ok = 0;
                            break;
                        }
                        emitted++;
                    }
                }
                i += cb;
                j += cc;
            } else if (c < 0) {
                i += eb[i].count;
            } else {
                j += ec[j].count;
            }
        }
        n_moved = emitted;
    }
    if (ok && oc_strbuf_appendf(&b,
                                "],\"counts\":{\"introduced\":%zu,\"resolved\":%zu,"
                                "\"moved\":%zu,\"persistent\":%zu},"
                                "\"note\":\"fingerprints exclude line numbers; "
                                "moves are persistent, not introduced+resolved "
                                "pairs; line numbers are informational only; the "
                                "kernel never decides identity\"}\n",
                                n_introduced, n_resolved, n_moved, persistent) == 0)
        ok = 0;

    free(adv_rank);
    free(adv_conf);
    free(eb);
    free(ec);
    oc_arena_destroy(&a);

    if (!ok || b.data == NULL) {
        oc_strbuf_destroy(&b);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory writing the diff document");
        return OC_ERR_ALLOC;
    }
    size_t w = fwrite(b.data, 1, b.len, out);
    int werr = (w != b.len) || ferror(out);
    oc_strbuf_destroy(&b);
    if (werr) {
        oc_status_set(st, OC_ERR_IO, "failed to write the diff document");
        return OC_ERR_IO;
    }
    if (semantic_changes)
        *semantic_changes = (n_introduced > 0 || n_resolved > 0) ? 1 : 0;
    return OC_OK;
}
