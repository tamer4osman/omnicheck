/* OC004 complex-function -- CRAP-lite scaffold (plan Table 1 v1.1 and
 * WBS 2.x). Cyclomatic complexity is computed at parse time by the
 * tree-sitter backend and stored on function nodes; this pass reports
 * functions above a configurable threshold and exposes the no-coverage
 * CRAP proxy comp^2 + comp (CRAP(m) = comp^2 * (1 - cov/100) + comp at
 * cov = 0). It is deliberately NOT the flagship CRAP-ms: coverage data
 * arrives via composition (WBS 3.x); nothing here claims verdict power.
 */
#include "passes.h"

#include <stdio.h>

static oc_code run_complexity(const oc_pass_ctx *ctx, oc_status *st) {
    long long max_comp = oc_config_get_int(ctx->config, "OC004.max_complexity", 15);
    const oc_node *file = &ctx->graph->nodes[ctx->file_id];

    for (size_t i = 0; i < ctx->graph->n_nodes; i++) {
        const oc_node *n = &ctx->graph->nodes[i];
        if (n->kind != OC_NODE_FUNCTION)
            continue;
        if (strcmp(n->path, file->path) != 0)
            continue;
        if (n->complexity == 0 || (long long)n->complexity <= max_comp)
            continue;

        size_t crap = n->complexity * n->complexity + n->complexity;
        char *msg = oc_arena_sprintf(ctx->arena,
                                     "Function '%s' has cyclomatic complexity %zu "
                                     "(threshold %lld); no-coverage CRAP proxy "
                                     "comp^2+comp = %zu",
                                     n->name, n->complexity, max_comp, crap);
        if (!msg) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
            return OC_ERR_ALLOC;
        }
        oc_finding_spec spec;
        memset(&spec, 0, sizeof(spec));
        spec.rule_id = "OC004";
        spec.rule_name = "complex-function";
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

const oc_pass oc_pass_complexity = {
    "OC004",
    "complex-function",
    OC_SEV_WARN,
    "Cyclomatic complexity above OC004.max_complexity (default 15); CRAP-lite "
    "scaffold pending coverage composition",
    run_complexity,
};
