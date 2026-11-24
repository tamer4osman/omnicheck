#include "oc/report.h"

#include <stdio.h>
#include <string.h>

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
    if (res->manifests_scanned > 0 || res->osv_snapshot_date) {
        oc_jsonw_key(w, "supply");
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "manifests_scanned");
        oc_jsonw_uint(w, res->manifests_scanned);
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
    oc_jsonw_key(w, "guardrails");
    oc_jsonw_str(w, "rank-only; prose-inputs-only; confidence-exposed; fail-open");
    oc_jsonw_obj_end(w);

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
    if (res->manifests_scanned > 0 || res->osv_snapshot_date)
        fprintf(out, "supply: %zu manifest(s); OSV snapshot %s\n", res->manifests_scanned,
                res->osv_snapshot_date ? res->osv_snapshot_date : "not provided");
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
