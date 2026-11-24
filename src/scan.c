#include "oc/scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oc/buildtag.h"
#include "oc/coverage.h"
#include "oc/discover.h"
#include "oc/dup.h"
#include "oc/finding.h"
#include "oc/graph.h"
#include "oc/imports.h"
#include "oc/pack.h"
#include "oc/parser.h"
#include "oc/pass.h"
#include "oc/supply.h"
#include "oc/surface.h"
#include "oc/triage.h"

/*
 * The full analysis pipeline, exactly as `oc check` ran it through
 * Sprint 3 (moved verbatim, Sprint 4). One scan, three consumers:
 * `oc check` renders it as JSON/human; `oc lsp` publishes diagnostics
 * and document symbols; `oc mcp` serves findings queries. The pipeline
 * is the single source of analysis truth (plan Ch4).
 */

typedef struct scan_impl {
    oc_parser *parser;
    oc_pack_set packs;
    oc_path_vec files;
    /* SPRINT-21: the routed pack per discovered file (parallel to
     * files.items; NULL where the file was skipped). The pass loop MUST
     * see the same routing the parse loop used -- re-matching by
     * extension would resurrect the .h ambiguity the routing resolved. */
    const oc_pack **routed;
    oc_import_vec imports;
    oc_bind_vec binds;
    oc_export_vec exports;
    oc_hint_vec hints;
    oc_coverage cov; /* loaded only when options carry coverage files */
    int have_cov;
    /* WBS 4.2: raw sources kept alive for OC011 (secret scanning), one
     * entry per DISCOVERED file (NULL when the file was skipped), freed
     * at cleanup. Also the parsed manifests + vendored OSV snapshot. */
    char **sources;
    size_t *source_lens;
    oc_manifest_vec manifests;
    oc_manifest_skip_vec skips;
    oc_osv_snapshot osv;
    int have_osv;
} scan_impl;

/* Internal growable parse-error vec (BUG-1 fix, moved from cmd_check). */
typedef struct parse_err_rec {
    const char *file;
    size_t error_nodes;
    size_t first_line;
    size_t first_col;
} parse_err_rec;

typedef struct parse_err_vec {
    parse_err_rec *items;
    size_t n;
    size_t cap;
} parse_err_vec;

static int parse_err_push(parse_err_vec *v, const char *file, size_t error_nodes, size_t first_line,
                          size_t first_col) {
    if (v->n == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 8;
        parse_err_rec *grown = (parse_err_rec *)realloc(v->items, ncap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = ncap;
    }
    v->items[v->n].file = file;
    v->items[v->n].error_nodes = error_nodes;
    v->items[v->n].first_line = first_line;
    v->items[v->n].first_col = first_col;
    v->n++;
    return 1;
}

/* Counts measurable functions for the coverage meta block. */
static void count_functions(const oc_graph *g, const oc_coverage *cov, size_t *total,
                            size_t *measured) {
    *total = 0;
    *measured = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        const oc_node *n = &g->nodes[i];
        if (n->kind != OC_NODE_FUNCTION)
            continue;
        (*total)++;
        if (!cov)
            continue;
        size_t anchors = 0, covered = 0;
        if (oc_coverage_lookup(cov, n->path, n->def_start_line, n->def_end_line, &anchors,
                               &covered) &&
            anchors > 0)
            (*measured)++;
    }
}

/* CRAP leaderboard (WBS 4.3, M4 criterion 1): every MEASURED function
 * with computed complexity ranked by CRAP desc, ties by (path asc, line
 * asc); top OC_CRAP_LEADERBOARD_TOP rows kept. Entries point into the
 * graph's arena strings -- no copying. Returns 0 on OOM. */
static int build_leaderboard(oc_scan_result *res, const oc_coverage *cov, oc_arena *a) {
    const oc_graph *g = &res->graph;
    size_t n = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        const oc_node *nd = &g->nodes[i];
        if (nd->kind != OC_NODE_FUNCTION || nd->complexity == 0)
            continue;
        size_t anchors = 0, covered = 0;
        if (!oc_coverage_lookup(cov, nd->path, nd->def_start_line, nd->def_end_line, &anchors,
                                &covered))
            continue;
        if (anchors == 0)
            continue;
        n++;
    }
    if (n == 0)
        return 1;
    oc_crap_entry *all = (oc_crap_entry *)malloc(n * sizeof(*all));
    if (!all)
        return 0;
    size_t w = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        const oc_node *nd = &g->nodes[i];
        if (nd->kind != OC_NODE_FUNCTION || nd->complexity == 0)
            continue;
        size_t anchors = 0, covered = 0;
        if (!oc_coverage_lookup(cov, nd->path, nd->def_start_line, nd->def_end_line, &anchors,
                                &covered))
            continue;
        if (anchors == 0)
            continue;
        size_t pct = covered * 100 / anchors;
        oc_crap_entry *e = &all[w++];
        e->path = nd->path;
        e->line = nd->decl_line;
        e->symbol = nd->name;
        e->complexity = nd->complexity;
        e->cov_pct = pct;
        e->crap = nd->complexity * nd->complexity * (100 - pct) / 100 + nd->complexity;
    }
    /* crap desc, then path asc, then line asc (deterministic ties). */
    for (size_t i = 1; i < w; i++) {
        oc_crap_entry tmp = all[i];
        size_t j = i;
        while (j > 0 && (all[j - 1].crap < tmp.crap ||
                         (all[j - 1].crap == tmp.crap && (strcmp(all[j - 1].path, tmp.path) > 0 ||
                                                          (strcmp(all[j - 1].path, tmp.path) == 0 &&
                                                           all[j - 1].line > tmp.line))))) {
            all[j] = all[j - 1];
            j--;
        }
        all[j] = tmp;
    }
    size_t keep = w < OC_CRAP_LEADERBOARD_TOP ? w : OC_CRAP_LEADERBOARD_TOP;
    oc_crap_entry *lb = (oc_crap_entry *)oc_arena_alloc(a, keep * sizeof(*lb));
    if (!lb) {
        free(all);
        return 0;
    }
    memcpy(lb, all, keep * sizeof(*lb));
    free(all);
    res->leaderboard = lb;
    res->n_leaderboard = keep;
    return 1;
}

/* Files-vec index of a path (the vec is sorted+deduped): binary search;
 * (size_t)-1 when absent. */
static size_t files_index(const oc_path_vec *files, const char *path) {
    size_t lo = 0, hi = files->n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(files->items[mid], path);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (size_t)-1;
}

oc_code oc_scan_run(const oc_scan_options *opt, oc_scan_result *res, oc_status *st) {
    /* Initialize EVERYTHING zeroed first, then run the per-field inits
     * that need real work. Field-by-field init alone missed every field
     * added after Sprint 4 (osv_snapshot_date, manifests_scanned,
     * leaderboard...) -- the fuzz harness caught report bytes differing
     * between two in-process scans because an uninitialized
     * osv_snapshot_date was read once as garbage and once as NULL
     * (Sprint 0 BUG-1 lesson, structurally applied this time). */
    memset(res, 0, sizeof(*res));
    /* The arena is heap-owned so the result can hold the REAL final
     * state (a struct copy taken at init would carry a stale block
     * list and leak every later allocation -- LSan, Sprint 4 dev). */
    oc_arena *arena = (oc_arena *)malloc(sizeof(*arena));
    if (!arena) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory allocating scan arena");
        return OC_ERR_ALLOC;
    }
    oc_arena_init(arena);
    res->arena = arena;
    res->_impl = NULL;

    scan_impl *impl = (scan_impl *)calloc(1, sizeof(*impl));
    if (!impl) {
        oc_arena_destroy(arena);
        free(arena);
        res->arena = NULL; /* destroy stays safe */
        oc_status_set(st, OC_ERR_ALLOC, "out of memory allocating scan state");
        return OC_ERR_ALLOC;
    }
    oc_paths_init(&impl->files);
    oc_imports_init(&impl->imports);
    oc_surface_binds_init(&impl->binds);
    oc_surface_exports_init(&impl->exports);
    oc_surface_hints_init(&impl->hints);
    oc_coverage_init(&impl->cov);
    oc_packs_init(&impl->packs, arena);
    oc_manifests_init(&impl->manifests);
    oc_manifest_skips_init(&impl->skips);
    oc_osv_init(&impl->osv);

    oc_graph_init(&res->graph, arena);
    oc_findings_init(&res->findings);
    res->parse_errors = NULL;
    res->n_parse_errors = 0;
    res->files_analyzed = 0;
    res->files_skipped = 0;
    res->build_tag_excluded = 0;
    memset(&res->dstats, 0, sizeof(res->dstats));
    res->functions_total = 0;
    res->functions_measured = 0;
    memset(&res->triage, 0, sizeof(res->triage));
    snprintf(res->triage.provider, sizeof(res->triage.provider), "none");
    snprintf(res->triage.reason, sizeof(res->triage.reason), "triage not requested (--triage)");
    res->backend = "tree-sitter";
    res->packs = NULL;
    res->dup = NULL;
    res->cov = NULL;
    res->_impl = impl;

    parse_err_vec pev;
    memset(&pev, 0, sizeof(pev));

    oc_code rc = OC_OK;

    /* Initialized before any goto cleanup: parse errors are recorded but
     * do not abort the run -- the report must say exactly which files are
     * partial (silent-partial-analysis, plan v1.1). */
    if (oc_packs_load_dir(&impl->packs, opt->packs_dir, st) != OC_OK) {
        rc = st->code;
        goto cleanup;
    }
    res->packs = &impl->packs;

    if (oc_discover_files(opt->paths, opt->n_paths, &impl->packs, arena, &impl->files, &res->dstats,
                          st) != OC_OK) {
        rc = st->code;
        goto cleanup;
    }

    impl->parser = oc_parser_new(arena, st);
    if (!impl->parser) {
        rc = st->code;
        goto cleanup;
    }

    size_t analyzed = 0, skipped = 0;
    /* Raw sources stay alive for OC011 (one entry per discovered file;
     * NULL where the file was skipped). Freed after the pass loop. */
    impl->sources = (char **)calloc(impl->files.n ? impl->files.n : 1, sizeof(char *));
    impl->source_lens = (size_t *)calloc(impl->files.n ? impl->files.n : 1, sizeof(size_t));
    impl->routed =
        (const oc_pack **)calloc(impl->files.n ? impl->files.n : 1, sizeof(oc_pack *));
    if (!impl->sources || !impl->source_lens || !impl->routed) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory tracking file sources");
        rc = OC_ERR_ALLOC;
        goto cleanup;
    }
    for (size_t i = 0; i < impl->files.n; i++) {
        const oc_pack *pack = oc_packs_match_path(&impl->packs, impl->files.items[i]);
        if (!pack) {
            skipped++;
            continue;
        }
        char *data = NULL;
        size_t len = 0;
        if (oc_read_file(impl->files.items[i], &data, &len, st) != OC_OK) {
            rc = st->code;
            goto cleanup;
        }
        /* SPRINT-21: the C/C++ header routing (the approved
         * architecture decision). Cheap extension match ran above; the
         * .h overlap between the c and cpp packs re-routes through the
         * content sniff + project-root default, and the reason is
         * counted for the report's header_routing meta block (never
         * silent). Every other path routes as OC_ROUTE_EXT and keeps
         * the extension match. */
        oc_route_reason reason = OC_ROUTE_EXT;
        pack = oc_packs_route_source(&impl->packs, impl->files.items[i], data, len,
                                     (const char *const *)impl->files.items, impl->files.n,
                                     arena, &reason);
        if (!pack) {
            free(data);
            skipped++;
            continue;
        }
        impl->routed[i] = pack;
        switch (reason) {
        case OC_ROUTE_H_SNIFF_CPP:
            res->h_sniffed_cpp++;
            break;
        case OC_ROUTE_H_SNIFF_C:
            res->h_sniffed_c++;
            break;
        case OC_ROUTE_H_DEFAULT_CPP:
            res->h_default_cpp++;
            break;
        case OC_ROUTE_H_DEFAULT_C:
            res->h_default_c++;
            break;
        case OC_ROUTE_EXT:
            break;
        }
        /* Go build-constraint filter (SPRINT-14, docs/LANGUAGES.md §Go.8):
         * the pack DECLARES the analysis target; a file whose constraint
         * evaluates false leaves the analyzed set (what go build does).
         * Excluded, counted in the report meta -- never silent. The file
         * never joins the graph, so edges/surface/passes see exactly the
         * analyzed set. */
        if (pack->build_goos &&
            oc_go_buildtag_excluded(data, len, pack->build_goos, pack->build_goarch,
                                    pack->build_compiler, pack->build_cgo,
                                    (const char *const *)pack->build_tags, pack->n_build_tags)) {
            free(data);
            res->build_tag_excluded++;
            continue;
        }
        impl->sources[i] = data;
        impl->source_lens[i] = len;
        oc_parser_input in;
        memset(&in, 0, sizeof(in));
        in.path = impl->files.items[i];
        in.source = data;
        in.len = len;
        in.grammar = pack->grammar;
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        out.imports = &impl->imports;
        out.binds = &impl->binds;
        out.exports = &impl->exports;
        out.hints = &impl->hints;
        oc_code prc = oc_parser_parse_file(impl->parser, &in, &res->graph, &out, st);
        if (prc != OC_OK) {
            rc = st->code;
            goto cleanup;
        }
        analyzed++;
        if (out.diag.had_error && !parse_err_push(&pev, impl->files.items[i], out.diag.error_nodes,
                                                  out.diag.first_line, out.diag.first_col)) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording parse errors");
            rc = OC_ERR_ALLOC;
            goto cleanup;
        }
    }
    res->files_analyzed = analyzed;
    res->files_skipped = skipped;

    /* Resolve import records into edges now that every file is a node:
     * an edge therefore always means "target is part of this analysis".
     * Roots bound the tsconfig alias search (resolver v2) and the Python
     * absolute-import search (resolver v3, Sprint 4). The hints carry
     * the declared-fact parse data the Kotlin resolver keys on
     * (SPRINT-22). */
    long n_edges = oc_imports_resolve_ex_hints(&res->graph, &impl->imports, arena, opt->paths,
                                               opt->n_paths, &impl->hints);
    if (n_edges < 0) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory resolving imports");
        rc = OC_ERR_ALLOC;
        goto cleanup;
    }

    /* Coverage composition (CRAP-ms): fail loud on unusable input. */
    if (opt->n_coverage > 0) {
        if (oc_coverage_load(&impl->cov, opt->coverage, opt->n_coverage, arena, st) != OC_OK) {
            rc = st->code;
            goto cleanup;
        }
        impl->have_cov = 1;
        res->cov = &impl->cov;
    }

    /* Module surface: usage marking + entry classification (OC005/006). */
    oc_surface *surface =
        oc_surface_build(arena, &res->graph, &impl->binds, &impl->exports, &impl->hints, opt->paths,
                         opt->n_paths, &impl->packs, st);
    if (!surface) {
        rc = st->code;
        goto cleanup;
    }

    if (impl->have_cov)
        oc_coverage_mark_matches(&impl->cov, &res->graph);

    /* Duplication index (OC008, WBS 2.4): built once from the analyzed
     * file set, attached to the pass context like surface/coverage. A
     * build failure is a hard tool error -- never silence. */
    oc_dup_index *dup = oc_dup_build(&res->graph, opt->cfg, arena, st);
    if (!dup) {
        rc = st->code;
        goto cleanup;
    }
    res->dup = dup;

    count_functions(&res->graph, impl->have_cov ? &impl->cov : NULL, &res->functions_total,
                    &res->functions_measured);

    /* Run enabled passes per file (union resolution happens via pack ids). */
    for (size_t i = 0; i < res->graph.n_nodes; i++) {
        if (res->graph.nodes[i].kind != OC_NODE_FILE)
            continue;
        size_t fi = files_index(&impl->files, res->graph.nodes[i].path);
        /* SPRINT-21: the pass loop sees the ROUTED pack (the parse
         * loop's decision), never a fresh extension match -- the .h
         * routing must be one decision per file for the whole scan. */
        const oc_pack *pack =
            (fi != (size_t)-1 && impl->routed) ? impl->routed[fi]
                                               : oc_packs_match_path(&impl->packs,
                                                                     res->graph.nodes[i].path);
        if (!pack)
            continue;
        oc_doc *doc = oc_graph_doc(&res->graph, (oc_node_id)i);
        oc_pass_ctx ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.graph = &res->graph;
        ctx.file_id = (oc_node_id)i;
        ctx.doc = doc;
        ctx.pack = pack;
        ctx.config = opt->cfg;
        ctx.arena = arena;
        ctx.out = &res->findings;
        ctx.surface = surface;
        ctx.coverage = impl->have_cov ? &impl->cov : NULL;
        ctx.dup = dup;
        if (fi != (size_t)-1) {
            ctx.source = impl->sources[fi];
            ctx.source_len = impl->source_lens[fi];
        }
        for (size_t j = 0; j < pack->n_passes; j++) {
            const oc_pass *pass = oc_pass_lookup(pack->passes[j]);
            if (!pass) {
                fprintf(stderr,
                        "omnicheck: warning: pack '%s' enables unknown pass "
                        "'%s' (ignored)\n",
                        pack->id, pack->passes[j]);
                continue;
            }
            oc_code prc = pass->run(&ctx, st);
            if (prc != OC_OK) {
                rc = st->code;
                goto cleanup;
            }
        }
    }

    /* Supply tier (WBS 4.2): vendored OSV snapshot + manifests found in
     * the analyzed tree. Fail loud on unusable input; OC009/OC010 run
     * through the same pass registry, gated by their ecosystem pack. */
    if (opt->osv_snapshot) {
        if (oc_osv_load(&impl->osv, opt->osv_snapshot, arena, st) != OC_OK) {
            rc = st->code;
            goto cleanup;
        }
        impl->have_osv = 1;
        res->osv_snapshot_date = impl->osv.snapshot_date;
    }
    if (oc_supply_load_manifests_ex(&impl->manifests, &impl->skips,
                                    (const char *const *)impl->files.items,
                                    impl->files.n, arena, st) != OC_OK) {
        rc = st->code;
        goto cleanup;
    }
    res->manifests_scanned = impl->manifests.n;
    /* BUGFIX N-2: expose the recorded manifest skips (named, never
     * silent) to the report meta. Arena-owned parallel arrays. */
    res->manifests_skipped = impl->skips.n;
    if (impl->skips.n > 0) {
        const char **sf = (const char **)oc_arena_alloc(arena,
                                                        impl->skips.n * sizeof(*sf));
        const char **sr = (const char **)oc_arena_alloc(arena,
                                                        impl->skips.n * sizeof(*sr));
        if (!sf || !sr) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording manifest skips");
            rc = OC_ERR_ALLOC;
            goto cleanup;
        }
        for (size_t s = 0; s < impl->skips.n; s++) {
            sf[s] = impl->skips.items[s].path;
            sr[s] = impl->skips.items[s].reason;
        }
        res->manifest_skip_files = sf;
        res->manifest_skip_reasons = sr;
    }
    for (size_t m = 0; m < impl->manifests.n; m++) {
        oc_manifest *man = &impl->manifests.items[m];
        /* Manifests run through the pack that owns their ecosystem:
         * npm -> ts-js, Maven -> java, rubygems -> ruby (SPRINT-16),
         * everything else -> python. The pack supplies the OC009/OC010
         * enablement. */
        const char *pack_id = man->eco == OC_ECO_NPM        ? "ts-js"
                              : man->eco == OC_ECO_MAVEN    ? "java"
                              : man->eco == OC_ECO_RUBYGEMS ? "ruby"
                                                            : "python";
        const oc_pack *mp = oc_packs_find(&impl->packs, pack_id);
        if (!mp)
            continue;
        for (size_t j = 0; j < mp->n_passes; j++) {
            const oc_pass *pass = oc_pass_lookup(mp->passes[j]);
            if (!pass)
                continue;
            if (strcmp(pass->id, "OC009") != 0 && strcmp(pass->id, "OC010") != 0)
                continue;
            if (strcmp(pass->id, "OC009") == 0 && !impl->have_osv)
                continue; /* no snapshot: never guess, meta reports it */
            oc_pass_ctx mctx;
            memset(&mctx, 0, sizeof(mctx));
            mctx.graph = &res->graph;
            mctx.file_id = OC_NODE_INVALID;
            mctx.pack = mp;
            mctx.config = opt->cfg;
            mctx.arena = arena;
            mctx.out = &res->findings;
            mctx.manifest = man;
            mctx.osv = impl->have_osv ? &impl->osv : NULL;
            oc_code prc = pass->run(&mctx, st);
            if (prc != OC_OK) {
                rc = st->code;
                goto cleanup;
            }
        }
    }

    if (impl->have_cov && !build_leaderboard(res, &impl->cov, arena)) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building CRAP leaderboard");
        rc = OC_ERR_ALLOC;
        goto cleanup;
    }

    if (opt->want_triage) {
        oc_code trc = oc_triage_rank_findings(&res->findings, &res->triage, st);
        if (trc != OC_OK) {
            rc = st->code;
            goto cleanup;
        }
    }

    /* Guardrail: sort AFTER triage so report order stays deterministic. */
    oc_findings_sort(&res->findings);

    /* Publish the named parse-error records. */
    if (pev.n > 0) {
        res->parse_errors = (oc_scan_parse_error *)malloc(pev.n * sizeof(oc_scan_parse_error));
        if (!res->parse_errors) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory publishing parse errors");
            rc = OC_ERR_ALLOC;
            goto cleanup;
        }
        res->n_parse_errors = pev.n;
        for (size_t i = 0; i < pev.n; i++) {
            res->parse_errors[i].file = pev.items[i].file;
            res->parse_errors[i].error_nodes = pev.items[i].error_nodes;
            res->parse_errors[i].first_line = pev.items[i].first_line;
            res->parse_errors[i].first_column = pev.items[i].first_col;
        }
    }

cleanup:
    free(pev.items);
    if (rc != OC_OK)
        oc_scan_result_destroy(res);
    return rc;
}

void oc_scan_result_destroy(oc_scan_result *res) {
    if (!res)
        return;
    scan_impl *impl = (scan_impl *)res->_impl;
    if (impl) {
        oc_surface_exports_destroy(&impl->exports);
        oc_surface_binds_destroy(&impl->binds);
        oc_surface_hints_destroy(&impl->hints);
        oc_coverage_destroy(&impl->cov);
        oc_imports_destroy(&impl->imports);
        oc_packs_destroy(&impl->packs);
        free(impl->routed);
        if (impl->sources) {
            /* BEFORE paths_destroy: files.n is the length of the source
             * parallel arrays and paths_destroy resets it to zero. */
            for (size_t i = 0; i < impl->files.n; i++)
                free(impl->sources[i]);
            free(impl->sources);
            free(impl->source_lens);
        }
        oc_paths_destroy(&impl->files);
        oc_manifests_destroy(&impl->manifests);
        oc_manifest_skips_destroy(&impl->skips);
        oc_osv_destroy(&impl->osv);
        oc_parser_free(impl->parser);
        free(impl);
    }
    free(res->parse_errors);
    res->parse_errors = NULL;
    res->n_parse_errors = 0;
    oc_findings_destroy(&res->findings);
    oc_graph_destroy(&res->graph);
    if (res->arena) {
        oc_arena_destroy(res->arena); /* LAST: holds strings freed above */
        free(res->arena);
        res->arena = NULL;
    }
    res->packs = NULL;
    res->dup = NULL;
    res->cov = NULL;
    res->_impl = NULL;
}
