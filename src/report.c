#include "oc/report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oc/brain.h"
#include "oc/finding.h"
#include "oc/pack.h"
#include "oc/triage.h"

/*
 * The two renderers below moved verbatim from src/cli/main.c (Sprint 4):
 * `oc check` output stays byte-identical while `oc mcp` omnicheck_scan
 * and any future surface reuse the same report contract.
 */

static int finding_needs_triage_fields(const oc_finding *f) {
    return f->triaged;
}

void oc_report_write_json(oc_jsonw *w, const oc_scan_result *res) {
    const oc_pack_set *ps = res->packs;
    const oc_finding_vec *findings = &res->findings;
    const oc_triage_outcome *triage = &res->triage;
    oc_jsonw_obj_begin(w);

    oc_jsonw_key(w, "meta");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "tool");
    oc_jsonw_str(w, "omnicheck");
    oc_jsonw_key(w, "version");
    oc_jsonw_str(w, OC_VERSION_STRING);
    oc_jsonw_key(w, "schema");
    oc_jsonw_str(w, OC_REPORT_SCHEMA);
    oc_jsonw_key(w, "backend");
    oc_jsonw_str(w, res->backend);
    oc_jsonw_key(w, "packs");
    oc_jsonw_arr_begin(w);
    for (size_t i = 0; i < ps->n_packs; i++)
        oc_jsonw_str(w, ps->packs[i].id);
    oc_jsonw_arr_end(w);
    oc_jsonw_key(w, "files_analyzed");
    oc_jsonw_uint(w, res->files_analyzed);
    oc_jsonw_key(w, "files_skipped_unknown_type");
    oc_jsonw_uint(w, res->files_skipped);
    oc_jsonw_key(w, "files_skipped_dot_entries");
    oc_jsonw_uint(w, res->dstats.skipped_dot_entries);
    oc_jsonw_key(w, "files_skipped_ignored_dirs");
    oc_jsonw_uint(w, res->dstats.skipped_ignored_dirs);
    /* SPRINT-14 (§Go.8): emitted only when the declared build context
     * actually excluded something, so unconstrained runs stay byte-stable. */
    if (res->build_tag_excluded > 0) {
        oc_jsonw_key(w, "files_excluded_build_tag");
        oc_jsonw_uint(w, res->build_tag_excluded);
    }
    /* SPRINT-21: the C/C++ header routing (the approved architecture
     * decision) -- emitted only when at least one .h file was routed by
     * the treatment (both packs registered), so every other scan stays
     * byte-stable. The counts make the routing visible, never silent. */
    if (res->h_sniffed_cpp || res->h_sniffed_c || res->h_default_cpp || res->h_default_c) {
        oc_jsonw_key(w, "header_routing");
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "sniffed_cpp");
        oc_jsonw_uint(w, res->h_sniffed_cpp);
        oc_jsonw_key(w, "sniffed_c");
        oc_jsonw_uint(w, res->h_sniffed_c);
        oc_jsonw_key(w, "default_cpp");
        oc_jsonw_uint(w, res->h_default_cpp);
        oc_jsonw_key(w, "default_c");
        oc_jsonw_uint(w, res->h_default_c);
        oc_jsonw_obj_end(w);
    }
    oc_jsonw_key(w, "files_with_parse_errors");
    oc_jsonw_uint(w, res->n_parse_errors);
    oc_jsonw_key(w, "parse_error_nodes");
    size_t parse_error_nodes = 0;
    for (size_t i = 0; i < res->n_parse_errors; i++)
        parse_error_nodes += res->parse_errors[i].error_nodes;
    oc_jsonw_uint(w, parse_error_nodes);
    oc_jsonw_key(w, "finding_count");
    oc_jsonw_uint(w, findings->n);
    oc_jsonw_key(w, "coverage");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "files_loaded");
    oc_jsonw_uint(w, res->cov ? res->cov->files_loaded : 0);
    oc_jsonw_key(w, "files_matched");
    oc_jsonw_uint(w, res->cov ? res->cov->files_matched : 0);
    oc_jsonw_key(w, "functions_total");
    oc_jsonw_uint(w, res->functions_total);
    oc_jsonw_key(w, "functions_measured");
    oc_jsonw_uint(w, res->functions_measured);
    oc_jsonw_obj_end(w);
    oc_jsonw_key(w, "dup");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "files_skipped_oversize");
    oc_jsonw_uint(w, res->dup ? res->dup->files_skipped_oversize : 0);
    oc_jsonw_key(w, "groups");
    oc_jsonw_uint(w, res->dup ? res->dup->n_groups : 0);
    oc_jsonw_key(w, "groups_truncated");
    oc_jsonw_uint(w, res->dup ? res->dup->groups_truncated : 0);
    oc_jsonw_obj_end(w);
    /* Supply tier (WBS 4.2): emitted only when a manifest was seen or a
     * snapshot was given, so runs without supply data stay byte-stable. */
    if (res->manifests_scanned > 0 || res->osv_snapshot_date || res->manifests_skipped > 0) {
        oc_jsonw_key(w, "supply");
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "manifests_scanned");
        oc_jsonw_uint(w, res->manifests_scanned);
        /* BUGFIX N-2: recorded manifest skips are named here (never
         * silent); the block only appears when something was skipped,
         * so clean runs keep their byte-stable meta. */
        if (res->manifests_skipped > 0) {
            oc_jsonw_key(w, "manifests_skipped");
            oc_jsonw_uint(w, res->manifests_skipped);
            oc_jsonw_key(w, "manifest_skip");
            oc_jsonw_arr_begin(w);
            for (size_t s = 0; s < res->manifests_skipped; s++) {
                oc_jsonw_obj_begin(w);
                oc_jsonw_key(w, "file");
                oc_jsonw_str(w, res->manifest_skip_files[s]);
                oc_jsonw_key(w, "reason");
                oc_jsonw_str(w, res->manifest_skip_reasons[s]);
                oc_jsonw_obj_end(w);
            }
            oc_jsonw_arr_end(w);
        }
        oc_jsonw_key(w, "osv_snapshot_date");
        oc_jsonw_str(w, res->osv_snapshot_date); /* NULL emits null */
        oc_jsonw_obj_end(w);
    }
    oc_jsonw_obj_end(w);

    oc_jsonw_key(w, "findings");
    oc_jsonw_arr_begin(w);
    for (size_t i = 0; i < findings->n; i++) {
        const oc_finding *f = &findings->items[i];
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "rule");
        oc_jsonw_str(w, f->rule_id);
        oc_jsonw_key(w, "name");
        oc_jsonw_str(w, f->rule_name);
        oc_jsonw_key(w, "severity");
        oc_jsonw_str(w, oc_severity_str(f->severity));
        oc_jsonw_key(w, "file");
        oc_jsonw_str(w, f->file);
        oc_jsonw_key(w, "line");
        oc_jsonw_uint(w, f->line);
        oc_jsonw_key(w, "end_line");
        oc_jsonw_uint(w, f->end_line);
        oc_jsonw_key(w, "symbol");
        oc_jsonw_str(w, f->symbol);
        oc_jsonw_key(w, "message");
        oc_jsonw_str(w, f->message);
        oc_jsonw_key(w, "fingerprint");
        oc_jsonw_str(w, f->fingerprint);
        if (finding_needs_triage_fields(f)) {
            oc_jsonw_key(w, "triage_rank");
            oc_jsonw_int(w, f->triage_rank);
            oc_jsonw_key(w, "triage_confidence");
            oc_jsonw_float(w, f->triage_confidence);
        }
        /* T1: the 9 kernel rationale channels, only on --triage-explain
         * runs (runs without the flag stay byte-stable). Fixed field
         * order; advisory data that never affects rank or order. */
        if (f->explained) {
            oc_jsonw_key(w, "explain");
            oc_jsonw_obj_begin(w);
            oc_jsonw_key(w, "probability");
            oc_jsonw_float(w, f->explain.probability);
            oc_jsonw_key(w, "fused");
            oc_jsonw_float(w, f->explain.fused);
            oc_jsonw_key(w, "tf_cosine");
            oc_jsonw_float(w, f->explain.tf_cosine);
            oc_jsonw_key(w, "jaccard");
            oc_jsonw_float(w, f->explain.jaccard);
            oc_jsonw_key(w, "trigram");
            oc_jsonw_float(w, f->explain.trigram);
            oc_jsonw_key(w, "whole");
            oc_jsonw_float(w, f->explain.whole);
            oc_jsonw_key(w, "best_chunk");
            oc_jsonw_float(w, f->explain.best_chunk);
            oc_jsonw_key(w, "contextual");
            oc_jsonw_float(w, f->explain.contextual);
            oc_jsonw_key(w, "directional");
            oc_jsonw_float(w, f->explain.directional);
            oc_jsonw_obj_end(w);
        }
        /* T4: brain advisory annotation (rank-only; --triage-brain opt-in). */
        if (f->brain_annotated) {
            oc_jsonw_key(w, "brain_bonus");
            oc_jsonw_float(w, f->brain_bonus);
            oc_jsonw_key(w, "brain_rank");
            oc_jsonw_int(w, f->brain_rank);
        }
        oc_jsonw_obj_end(w);
    }
    oc_jsonw_arr_end(w);

    /* CRAP leaderboard (WBS 4.3): composed only with coverage, so runs
     * without --coverage stay byte-stable. Top 10 by CRAP desc. */
    if (res->cov && res->n_leaderboard > 0) {
        oc_jsonw_key(w, "crap_leaderboard");
        oc_jsonw_arr_begin(w);
        for (size_t i = 0; i < res->n_leaderboard; i++) {
            const oc_crap_entry *e = &res->leaderboard[i];
            oc_jsonw_obj_begin(w);
            oc_jsonw_key(w, "file");
            oc_jsonw_str(w, e->path);
            oc_jsonw_key(w, "line");
            oc_jsonw_uint(w, e->line);
            oc_jsonw_key(w, "symbol");
            oc_jsonw_str(w, e->symbol);
            oc_jsonw_key(w, "complexity");
            oc_jsonw_uint(w, e->complexity);
            oc_jsonw_key(w, "coverage_pct");
            oc_jsonw_uint(w, e->cov_pct);
            oc_jsonw_key(w, "crap");
            oc_jsonw_uint(w, e->crap);
            oc_jsonw_obj_end(w);
        }
        oc_jsonw_arr_end(w);
    }

    oc_jsonw_key(w, "parse_errors");
    oc_jsonw_arr_begin(w);
    for (size_t i = 0; i < res->n_parse_errors; i++) {
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "file");
        oc_jsonw_str(w, res->parse_errors[i].file);
        oc_jsonw_key(w, "error_nodes");
        oc_jsonw_uint(w, res->parse_errors[i].error_nodes);
        oc_jsonw_key(w, "first_line");
        oc_jsonw_uint(w, res->parse_errors[i].first_line);
        oc_jsonw_key(w, "first_column");
        oc_jsonw_uint(w, res->parse_errors[i].first_column);
        oc_jsonw_obj_end(w);
    }
    oc_jsonw_arr_end(w);

    oc_jsonw_key(w, "triage");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "provider");
    oc_jsonw_str(w, triage->provider[0] ? triage->provider : "none");
    oc_jsonw_key(w, "model");
    oc_jsonw_str(w, triage->model);
    oc_jsonw_key(w, "ranked");
    oc_jsonw_bool(w, triage->ranked);
    oc_jsonw_key(w, "ranked_count");
    oc_jsonw_uint(w, triage->ranked_count);
    oc_jsonw_key(w, "reason");
    oc_jsonw_str(w, triage->reason);
    /* T1: explain meta only when requested (byte-stability). */
    if (triage->explain_requested) {
        oc_jsonw_key(w, "explained_count");
        oc_jsonw_uint(w, triage->explained_count);
        oc_jsonw_key(w, "explain_reason");
        oc_jsonw_str(w, triage->explain_reason);
    }
    oc_jsonw_key(w, "guardrails");
    oc_jsonw_str(w, "rank-only; prose-inputs-only; confidence-exposed; fail-open");
    oc_jsonw_obj_end(w);

    /* T4: the brain block appears ONLY when --triage-brain was requested,
     * so every report without the flag stays byte-identical. */
    if (res->brain.requested) {
        oc_jsonw_key(w, "brain");
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "applied");
        oc_jsonw_bool(w, res->brain.applied);
        oc_jsonw_key(w, "file");
        oc_jsonw_str(w, res->brain.file);
        oc_jsonw_key(w, "epoch");
        if (res->brain.epoch >= 0)
            oc_jsonw_int(w, res->brain.epoch);
        else
            oc_jsonw_str(w, NULL);
        oc_jsonw_key(w, "weight");
        oc_jsonw_float(w, OC_BRAIN_WEIGHT);
        oc_jsonw_key(w, "adjusted");
        oc_jsonw_uint(w, res->brain.adjusted);
        oc_jsonw_key(w, "reason");
        oc_jsonw_str(w, res->brain.reason);
        oc_jsonw_obj_end(w);
    }

    /* T7: the kernel-files provenance block appears ONLY when
     * --triage-lexicon/--triage-model was requested (byte-stability). */
    if (res->kernel_files.requested) {
        oc_jsonw_key(w, "kernel_files");
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "lexicons");
        oc_jsonw_arr_begin(w);
        for (size_t i = 0; i < res->kernel_files.n_lexicons; i++) {
            oc_jsonw_obj_begin(w);
            oc_jsonw_key(w, "path");
            oc_jsonw_str(w, res->kernel_files.lexicons[i].path);
            oc_jsonw_key(w, "sha256");
            oc_jsonw_str(w, res->kernel_files.lexicons[i].sha256);
            oc_jsonw_obj_end(w);
        }
        oc_jsonw_arr_end(w);
        oc_jsonw_key(w, "lexicons_applied");
        oc_jsonw_uint(w, res->kernel_files.lexicons_applied);
        oc_jsonw_key(w, "model");
        if (res->kernel_files.model_requested) {
            oc_jsonw_obj_begin(w);
            oc_jsonw_key(w, "path");
            oc_jsonw_str(w, res->kernel_files.model.path);
            oc_jsonw_key(w, "sha256");
            oc_jsonw_str(w, res->kernel_files.model.sha256);
            oc_jsonw_obj_end(w);
        } else {
            oc_jsonw_str(w, NULL);
        }
        oc_jsonw_key(w, "model_applied");
        oc_jsonw_int(w, res->kernel_files.model_applied);
        oc_jsonw_key(w, "reason");
        oc_jsonw_str(w, res->kernel_files.reason);
        oc_jsonw_obj_end(w);
    }

    oc_jsonw_obj_end(w);
}

void oc_report_write_human(FILE *out, const oc_scan_result *res) {
    const oc_pack_set *ps = res->packs;
    const oc_finding_vec *findings = &res->findings;
    const oc_triage_outcome *triage = &res->triage;
    fprintf(out, "omnicheck %s -- human report\n", OC_VERSION_STRING);

    fputs("packs: ", out);
    for (size_t i = 0; i < ps->n_packs; i++)
        fprintf(out, "%s%s", i ? ", " : "", ps->packs[i].id);
    fputc('\n', out);

    fprintf(out,
            "analyzed: %zu file(s); skipped: unknown-type %zu, dot-entries %zu, "
            "ignored-dirs %zu, oversize-dup-scan %zu; parse errors: %zu file(s)\n",
            res->files_analyzed, res->files_skipped, res->dstats.skipped_dot_entries,
            res->dstats.skipped_ignored_dirs, res->dup ? res->dup->files_skipped_oversize : 0,
            res->n_parse_errors);
    if (res->build_tag_excluded > 0)
        fprintf(out, "build-tag excluded: %zu file(s) (declared build context)\n",
                res->build_tag_excluded);
    if (res->h_sniffed_cpp || res->h_sniffed_c || res->h_default_cpp || res->h_default_c)
        fprintf(out,
                "header routing: sniffed cpp %zu, sniffed c %zu, root-default cpp %zu, "
                "root-default c %zu\n",
                res->h_sniffed_cpp, res->h_sniffed_c, res->h_default_cpp, res->h_default_c);

    if (res->cov)
        fprintf(out, "coverage: composed from %zu file(s); %zu/%zu function(s) measured\n",
                res->cov->files_loaded, res->functions_measured, res->functions_total);
    else
        fputs("coverage: not composed\n", out);

    fprintf(out, "triage: %s (%s)\n", triage->provider, triage->reason);

    size_t n_err = 0, n_warn = 0, n_info = 0;
    for (size_t i = 0; i < findings->n; i++) {
        if (findings->items[i].severity == OC_SEV_ERROR)
            n_err++;
        else if (findings->items[i].severity == OC_SEV_WARN)
            n_warn++;
        else
            n_info++;
    }
    fprintf(out, "findings: %zu total (%zu error, %zu warn, %zu info)\n", findings->n, n_err,
            n_warn, n_info);
    fprintf(out, "dup scan: %zu group(s)%s\n", res->dup ? res->dup->n_groups : 0,
            (res->dup && res->dup->groups_truncated) ? " (truncated)" : "");
    if (res->manifests_scanned > 0 || res->osv_snapshot_date || res->manifests_skipped > 0)
        fprintf(out, "supply: %zu manifest(s); OSV snapshot %s\n", res->manifests_scanned,
                res->osv_snapshot_date ? res->osv_snapshot_date : "not provided");
    /* BUGFIX N-2: skipped manifests are named in the human report too. */
    if (res->manifests_skipped > 0) {
        fprintf(out, "supply: %zu manifest(s) SKIPPED (unreadable/unparseable; named below):\n",
                res->manifests_skipped);
        for (size_t s = 0; s < res->manifests_skipped; s++)
            fprintf(out, "  %s: %s\n", res->manifest_skip_files[s], res->manifest_skip_reasons[s]);
    }
    if (res->cov && res->n_leaderboard > 0) {
        fputs("CRAP leaderboard (top 10):\n", out);
        for (size_t i = 0; i < res->n_leaderboard; i++) {
            const oc_crap_entry *e = &res->leaderboard[i];
            fprintf(out, "  %5zu  %s:%zu %s (comp %zu, cov %zu%%)\n", e->crap, e->path, e->line,
                    e->symbol, e->complexity, e->cov_pct);
        }
    }

    if (findings->n > 0) {
        /* Distinct rule ids (a report can carry every built-in rule;
         * 64 is far above that bound). */
        const char *rules[64];
        size_t n_rules = 0;
        for (size_t i = 0; i < findings->n; i++) {
            const char *r = findings->items[i].rule_id;
            size_t k = 0;
            while (k < n_rules && strcmp(rules[k], r) != 0)
                k++;
            if (k == n_rules && n_rules < 64)
                rules[n_rules++] = r;
        }
        for (size_t i = 1; i < n_rules; i++) { /* insertion sort by id */
            const char *tmp = rules[i];
            size_t j = i;
            while (j > 0 && strcmp(rules[j - 1], tmp) > 0) {
                rules[j] = rules[j - 1];
                j--;
            }
            rules[j] = tmp;
        }
        for (size_t r = 0; r < n_rules; r++) {
            size_t count = 0;
            const oc_finding *first = NULL;
            for (size_t i = 0; i < findings->n; i++) {
                if (strcmp(findings->items[i].rule_id, rules[r]) == 0) {
                    count++;
                    if (!first)
                        first = &findings->items[i];
                }
            }
            fprintf(out, "\n[%s] %s (%s) -- %zu finding(s)\n", rules[r], first->rule_name,
                    oc_severity_str(first->severity), count);
            for (size_t i = 0; i < findings->n; i++) {
                const oc_finding *f = &findings->items[i];
                if (strcmp(f->rule_id, rules[r]) != 0)
                    continue;
                if (f->end_line > f->line)
                    fprintf(out, "  %s:%zu-%zu  %s\n", f->file, f->line, f->end_line, f->message);
                else
                    fprintf(out, "  %s:%zu  %s\n", f->file, f->line, f->message);
                if (f->symbol && f->symbol[0])
                    fprintf(out, "    symbol: %s\n", f->symbol);
                fprintf(out, "    fp=%s%s\n", f->fingerprint, f->triaged ? " [triaged]" : "");
                if (f->triaged)
                    fprintf(out, "    triage: rank=%d confidence=%.4f\n", f->triage_rank,
                            f->triage_confidence);
            }
        }
    }

    if (res->n_parse_errors > 0) {
        fputs("\nparse errors:\n", out);
        for (size_t i = 0; i < res->n_parse_errors; i++)
            fprintf(out, "  %s -- %zu error node(s), first at line %zu column %zu\n",
                    res->parse_errors[i].file, res->parse_errors[i].error_nodes,
                    res->parse_errors[i].first_line, res->parse_errors[i].first_column);
    }
}

/* ------------------------------------------------------------------ T6 -- */

#if defined(OC_EMBED_OMNIDECIDE)
#include <omnidecide.h>

/* Appends the minified prose to the accumulator buffers (raw + minified,
 * newline-joined) and returns the minified copy flattened to ONE line
 * (embedded newlines become single spaces -- the llm render is
 * line-oriented). Malloc'd; static buffer not possible (per-finding). */
static char *llm_minify(const char *text, oc_strbuf *raw_acc, oc_strbuf *min_acc) {
    const char *src = text ? text : "";
    char *min = odk_minify(src);
    if (!min)
        min = (char *)""; /* allocation failure: passthrough, no crash */
    /* Flatten: odk_minify compresses paragraphs and may keep newlines. */
    size_t n = strlen(min);
    char *flat = (char *)malloc(n + 1);
    if (flat) {
        size_t w = 0;
        for (size_t i = 0; i < n; i++) {
            char c = (min[i] == '\n' || min[i] == '\r' || min[i] == '\t') ? ' ' : min[i];
            if (c == ' ' && (w == 0 || flat[w - 1] == ' '))
                continue; /* collapse runs of whitespace */
            flat[w++] = c;
        }
        while (w > 0 && flat[w - 1] == ' ')
            w--; /* no trailing blank before the next pipe */
        flat[w] = '\0';
    } else {
        flat = min; /* OOM: keep the minified text as-is */
        min = NULL; /* ownership moved; do not double-free */
    }
    if (min && min != (char *)"")
        free(min);
    if (raw_acc->len)
        oc_strbuf_appendc(raw_acc, '\n');
    oc_strbuf_appends(raw_acc, src);
    if (min_acc->len)
        oc_strbuf_appendc(min_acc, '\n');
    oc_strbuf_appends(min_acc, flat);
    return flat;
}

void oc_report_write_llm(FILE *out, const oc_scan_result *res) {
    const oc_pack_set *ps = res->packs;
    const oc_finding_vec *findings = &res->findings;
    const oc_triage_outcome *triage = &res->triage;

    oc_strbuf raw_acc, min_acc;
    oc_strbuf_init(&raw_acc);
    oc_strbuf_init(&min_acc);

    /* Header comment: schema, tool identity, backend, packs. */
    fprintf(out,
            "<!-- omnicheck.report.llm/v0 tool=omnicheck version=%s "
            "backend=%s packs=",
            OC_VERSION_STRING, res->backend ? res->backend : "");
    for (size_t i = 0; i < ps->n_packs; i++)
        fprintf(out, "%s%s", i ? "," : "", ps->packs[i].id);
    fprintf(out, " -->\n");
    fprintf(out, "files_analyzed=%zu files_with_parse_errors=%zu findings=%zu\n",
            res->files_analyzed, res->n_parse_errors, findings->n);

    fprintf(out, "\n## findings\n");
    for (size_t i = 0; i < findings->n; i++) {
        const oc_finding *f = &findings->items[i];
        char *min = llm_minify(f->message, &raw_acc, &min_acc);
        fprintf(out, "- %s|%s|%s:%zu:%zu|%s|%s|%s\n", f->rule_id, oc_severity_str(f->severity),
                f->file, f->line, f->col, f->symbol ? f->symbol : "-", min, f->fingerprint);
        free(min); /* llm_minify owns the flattened copy */
        if (f->triaged) {
            fprintf(out, "  rank=%d confidence=%.4f brain_rank=%d brain_bonus=%.4f\n",
                    f->triage_rank, f->triage_confidence, f->brain_annotated ? f->brain_rank : -1,
                    f->brain_annotated ? f->brain_bonus : 0.0);
        }
        if (f->explained) {
            fprintf(out,
                    "  explain p=%.4f fused=%.4f cos=%.4f jac=%.4f trig=%.4f "
                    "whole=%.4f chunk=%.4f ctx=%.4f dir=%.4f\n",
                    f->explain.probability, f->explain.fused, f->explain.tf_cosine,
                    f->explain.jaccard, f->explain.trigram, f->explain.whole, f->explain.best_chunk,
                    f->explain.contextual, f->explain.directional);
        }
    }

    fprintf(out, "\n## triage\n");
    {
        char *min = llm_minify(triage->reason, &raw_acc, &min_acc);
        fprintf(out, "provider=%s model=%s ranked=%d ranked_count=%zu reason=%s\n",
                triage->provider[0] ? triage->provider : "none", triage->model, triage->ranked,
                triage->ranked_count, min);
        free(min); /* llm_minify owns the flattened copy */
        if (triage->explain_requested) {
            fprintf(out, "explained_count=%zu explain_reason=%s\n", triage->explained_count,
                    triage->explain_reason);
        }
    }

    /* T4: brain block only when requested (byte-stability parity with the
     * JSON render). */
    if (res->brain.requested) {
        fprintf(out,
                "brain applied=%d file=%s epoch=%lld weight=%.1f adjusted=%zu "
                "reason=%s\n",
                res->brain.applied, res->brain.file, res->brain.epoch, OC_BRAIN_WEIGHT,
                res->brain.adjusted, res->brain.reason);
    }

    /* Token estimates over the accumulated prose (kernel minifier). */
    fprintf(out, "\n## tokens\n");
    odk_token_report_t tr;
    memset(&tr, 0, sizeof(tr));
    if (raw_acc.len > 0)
        odk_token_report(raw_acc.data, min_acc.data, &tr);
    fprintf(out, "raw_tokens=%d compressed_tokens=%d reduction_pct=%.1f\n", tr.raw_tokens,
            tr.compressed_tokens, tr.reduction_pct);

    oc_strbuf_destroy(&raw_acc);
    oc_strbuf_destroy(&min_acc);
}

int oc_report_llm_available(void) {
    return 1;
}

#else /* !OC_EMBED_OMNIDECIDE */

void oc_report_write_llm(FILE *out, const oc_scan_result *res) {
    (void)out;
    (void)res; /* never called: the CLI refuses --format llm loud */
}

int oc_report_llm_available(void) {
    return 0;
}

#endif /* OC_EMBED_OMNIDECIDE */
