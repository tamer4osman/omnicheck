/* OC006 unreferenced-module -- module-surface pass (WBS 3.x).
 * Reports analyzed files with zero inbound import edges that no
 * entry-point rule claims. Entry rules (deterministic, declared data +
 * cheap parse facts): pack entry_points globs, shebang first line, the
 * parsed package_clause (`package main`, Go, SPRINT-10), and test-file
 * conventions the runner executes directly. Severity is INFO on purpose:
 * the structural claim is exact (nothing in the analyzed set imports this
 * file) but the intent claim is weak -- bin scripts, examples, and plugin
 * modules are legitimately unreferenced. Resolver limits matter here too:
 * module-prefix Go imports and absolute Python imports outside the
 * resolved set stay unresolved by contract, so their targets surface as
 * unreferenced (documented in docs/ARCHITECTURE.md). */
#include "passes.h"

#include <stdio.h>

#include "oc/surface.h"

static oc_code run_unreferenced_module(const oc_pass_ctx *ctx, oc_status *st) {
    if (!ctx->surface)
        return OC_OK;
    const oc_surface *s = ctx->surface;
    const oc_node *file = &ctx->graph->nodes[ctx->file_id];

    if (s->in_degree[ctx->file_id] > 0)
        return OC_OK;
    if (s->entry[ctx->file_id])
        return OC_OK;

    char *msg = oc_arena_sprintf(ctx->arena, "File is never imported by any analyzed file and "
                                             "no entry-point rule claims it (bin scripts, examples "
                                             "and plugin modules look the same -- triage decides)");
    if (!msg) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
        return OC_ERR_ALLOC;
    }
    oc_finding_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.rule_id = "OC006";
    spec.rule_name = "unreferenced-module";
    spec.severity = OC_SEV_INFO;
    spec.file = file->path;
    spec.line = 1;
    spec.col = 1;
    spec.end_line = 1;
    spec.symbol = NULL;
    spec.message = msg;
    if (!oc_findings_push(ctx->out, ctx->arena, &spec)) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
        return OC_ERR_ALLOC;
    }
    oc_status_clear(st);
    return OC_OK;
}

const oc_pass oc_pass_unreferenced_module = {
    "OC006",
    "unreferenced-module",
    OC_SEV_INFO,
    "Analyzed file with no inbound import edges and no entry-point rule "
    "(pack entry_points, shebang, package main, test conventions)",
    run_unreferenced_module,
};
