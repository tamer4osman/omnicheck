/* OC007 crap-hotspot -- the flagship CRAP-ms composition (WBS 3.x).
 * CRAP(m) = comp^2 * (1 - cov/100) + comp, with comp computed at parse
 * time and cov composed from OTHER TOOLS' coverage reports (Istanbul,
 * coverage.py) mapped onto the unified graph's per-function definition
 * spans. The increment over fallow-style single-input CRAP is the
 * multi-source composition, not a new formula.
 *
 * Determinism notes: cov percentage is an integer floor;
 * unmeasured functions (no coverage entry, no anchors in span) never
 * produce findings; OC004 remains the no-coverage proxy. */
#include "passes.h"

#include <stdio.h>

#include "oc/coverage.h"

static oc_code run_crap_hotspot(const oc_pass_ctx *ctx, oc_status *st) {
    if (!ctx->coverage)
        return OC_OK; /* no coverage composed: OC004 carries the proxy */
    long long max_crap = oc_config_get_int(ctx->config, "OC007.max_crap", 30);
    const oc_node *file = &ctx->graph->nodes[ctx->file_id];

    for (size_t i = 0; i < ctx->graph->n_nodes; i++) {
        const oc_node *n = &ctx->graph->nodes[i];
        if (n->kind != OC_NODE_FUNCTION)
            continue;
        if (strcmp(n->path, file->path) != 0)
            continue;
        if (n->complexity == 0)
            continue; /* not computed: never guess */
        size_t anchors = 0, covered = 0;
        if (!oc_coverage_lookup(ctx->coverage, n->path, n->def_start_line, n->def_end_line,
                                &anchors, &covered))
            continue; /* unmeasured: out of scope, honestly */
        if (anchors == 0)
            continue; /* empty body: not measurable */
        size_t pct = covered * 100 / anchors;
        size_t crap = n->complexity * n->complexity * (100 - pct) / 100 + n->complexity;
        if ((long long)crap <= max_crap)
            continue;
        char *msg = oc_arena_sprintf(ctx->arena,
                                     "Function '%s' CRAP=%zu (complexity %zu, "
                                     "coverage %zu%% = %zu/%zu anchors; threshold %lld)",
                                     n->name, crap, n->complexity, pct, covered, anchors, max_crap);
        if (!msg) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
            return OC_ERR_ALLOC;
        }
        oc_finding_spec spec;
        memset(&spec, 0, sizeof(spec));
        spec.rule_id = "OC007";
        spec.rule_name = "crap-hotspot";
        spec.severity = OC_SEV_WARN;
        spec.file = file->path;
        spec.line = n->decl_line;
        spec.col = 1;
        spec.end_line = n->def_end_line;
        spec.symbol = n->name;
        spec.message = msg;
        if (!oc_findings_push(ctx->out, ctx->arena, &spec)) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
            return OC_ERR_ALLOC;
        }
    }
    oc_status_clear(st);
    return OC_OK;
}

const oc_pass oc_pass_crap_hotspot = {
    "OC007",
    "crap-hotspot",
    OC_SEV_WARN,
    "CRAP(m) = comp^2 * (1 - cov/100) + comp above OC007.max_crap "
    "(default 30); coverage composed from Istanbul / coverage.py reports",
    run_crap_hotspot,
};
