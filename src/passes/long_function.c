/* OC001 long-function -- cheap AST check (plan capability layer T5).
 * Reports functions whose tracked body exceeds a configurable line
 * budget. Named functions only in v0 (anonymous anchors would weaken
 * fingerprint stability). */
#include "passes.h"

#include <stdio.h>

static oc_code run_long_function(const oc_pass_ctx *ctx, oc_status *st) {
    long long max_lines = oc_config_get_int(ctx->config, "OC001.max_lines", 60);
    const oc_node *file = &ctx->graph->nodes[ctx->file_id];

    for (size_t i = 0; i < ctx->graph->n_nodes; i++) {
        const oc_node *n = &ctx->graph->nodes[i];
        if (n->kind != OC_NODE_FUNCTION)
            continue;
        if (strcmp(n->path, file->path) != 0)
            continue;
        if (n->def_start_line == 0 || n->def_end_line < n->def_start_line)
            continue;
        size_t lines = n->def_end_line - n->def_start_line + 1;
        if ((long long)lines <= max_lines)
            continue;

        char *msg = oc_arena_sprintf(ctx->arena,
                                     "Function '%s' spans %zu lines "
                                     "(threshold %lld); consider splitting",
                                     n->name, lines, max_lines);
        if (!msg) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
            return OC_ERR_ALLOC;
        }
        /* Fold the span size into the fingerprint: the anchor alone
         * (rule, file, symbol) collides for same-named functions in one
         * file (two constructors). The size is config-independent (it
         * does not embed the threshold) and survives code motion.
         * Byte-identical siblings (same name AND same span) still share
         * one fingerprint by design. */
        char fpsize[24];
        snprintf(fpsize, sizeof(fpsize), "%zu", lines);
        oc_finding_spec spec;
        memset(&spec, 0, sizeof(spec));
        spec.rule_id = "OC001";
        spec.rule_name = "long-function";
        spec.severity = OC_SEV_WARN;
        spec.file = file->path;
        spec.line = n->decl_line;
        spec.col = 1;
        spec.end_line = n->def_end_line;
        spec.symbol = n->name;
        spec.message = msg;
        spec.fp_text = fpsize;
        if (!oc_findings_push(ctx->out, ctx->arena, &spec)) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
            return OC_ERR_ALLOC;
        }
    }
    oc_status_clear(st);
    return OC_OK;
}

const oc_pass oc_pass_long_function = {
    "OC001",           "long-function",
    OC_SEV_WARN,       "Function definition exceeds OC001.max_lines (default 60)",
    run_long_function,
};
