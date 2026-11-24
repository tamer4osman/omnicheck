/* OC008 duplicate-code -- per-file emission of the precomputed
 * duplication index (WBS 2.4, plan Table 8 "Duplication fingerprint
 * analysis"). The index (src/dup.c) is built once per run and attached
 * to the pass context like the surface/coverage global state; this pass
 * only emits findings, one per duplicated location of the current file.
 * Severity warn: structurally true, intent-blind -- the OmniDecide
 * triage layer decides what it means for the repo at hand.
 */
#include "passes.h"

#include "oc/dup.h"

#include <stdio.h>

static oc_code run_duplicate_code(const oc_pass_ctx *ctx, oc_status *st) {
    const oc_dup_index *dup = ctx->dup;
    if (!dup)
        return OC_OK; /* no index attached (e.g. graph-only consumers) */

    const oc_node *file = &ctx->graph->nodes[ctx->file_id];

    for (size_t gi = 0; gi < dup->n_groups; gi++) {
        const oc_dup_group *grp = &dup->groups[gi];
        for (size_t m = 0; m < grp->n_members; m++) {
            const oc_dup_member *mem = &grp->members[m];
            if (dup->files[mem->file_idx].node_id != ctx->file_id)
                continue;

            char fp_text[64];
            snprintf(fp_text, sizeof(fp_text), "dup:%016llx@%zu",
                     (unsigned long long)grp->text_hash, mem->start);

            oc_finding_spec spec;
            memset(&spec, 0, sizeof(spec));
            spec.rule_id = "OC008";
            spec.rule_name = "duplicate-code";
            spec.severity = OC_SEV_WARN;
            spec.file = file->path;
            spec.line = mem->start;
            spec.col = 1;
            spec.end_line = mem->end;
            spec.symbol = NULL;
            spec.message = grp->message;
            spec.fp_text = fp_text;
            if (!oc_findings_push(ctx->out, ctx->arena, &spec)) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
                return OC_ERR_ALLOC;
            }
        }
    }
    oc_status_clear(st);
    return OC_OK;
}

const oc_pass oc_pass_duplicate_code = {
    "OC008",
    "duplicate-code",
    OC_SEV_WARN,
    "Normalized line block (OC008.min_block, default 10) duplicated verbatim "
    "in 2+ locations; one finding per location, text-verified (WBS 2.4)",
    run_duplicate_code,
};
