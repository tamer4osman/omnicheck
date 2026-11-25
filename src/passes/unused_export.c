/* OC005 unused-export -- module-surface pass (WBS 3.x).
 * Reports exports that no analyzed file references by name. The
 * precondition is strict: the containing file must itself be imported,
 * so dead files remain OC006's domain and barrels without consumers are
 * not double-flagged. Structurally true, intent-blind (reflection,
 * dynamic use) -- exactly what the OmniDecide triage layer exists for.
 * Fail-open everywhere: wildcard imports, re-exports, require() and
 * dynamic import() mark whole files used rather than guessed. */
#include "passes.h"

#include <stdio.h>

#include "oc/surface.h"

static oc_code run_unused_export(const oc_pass_ctx *ctx, oc_status *st) {
    if (!ctx->surface)
        return OC_OK; /* no surface state: pass stays silent, never guesses */
    const oc_surface *s = ctx->surface;
    const oc_node *file = &ctx->graph->nodes[ctx->file_id];

    /* The file must participate in the module graph for its individual
     * exports to be judged. */
    if (s->in_degree[ctx->file_id] == 0)
        return OC_OK;

    for (size_t i = 0; i < s->exports.n; i++) {
        const oc_export_rec *e = &s->exports.items[i];
        if (strcmp(e->path, file->path) != 0)
            continue;
        if (e->used)
            continue;
        char *msg = oc_arena_sprintf(ctx->arena,
                                     "Export '%s' is never imported by any analyzed "
                                     "file (module surface is a lower bound)",
                                     e->name);
        if (!msg) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
            return OC_ERR_ALLOC;
        }
        oc_finding_spec spec;
        memset(&spec, 0, sizeof(spec));
        spec.rule_id = "OC005";
        spec.rule_name = "unused-export";
        spec.severity = OC_SEV_WARN;
        spec.file = file->path;
        spec.line = e->line;
        spec.col = 1;
        spec.end_line = e->line;
        spec.symbol = e->name;
        spec.message = msg;
        if (!oc_findings_push(ctx->out, ctx->arena, &spec)) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
            return OC_ERR_ALLOC;
        }
    }
    oc_status_clear(st);
    return OC_OK;
}

const oc_pass oc_pass_unused_export = {
    "OC005",
    "unused-export",
    OC_SEV_WARN,
    "Exported symbol never imported by any analyzed file (only in files "
    "that are themselves imported; wildcard/require/dynamic use fails open)",
    run_unused_export,
};
