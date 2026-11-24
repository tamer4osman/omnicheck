/* OC011 secret-pattern -- deterministic credential scan (WBS 4.2).
 * A real per-file pass: ctx.source carries the current file's raw bytes
 * (set by the scan pipeline for every analyzed file). The pattern list
 * lives in supply.c and is fixed and documented; the pass never guesses
 * beyond it. */
#include "passes.h"

#include "oc/supply.h"

static oc_code run_secret(const oc_pass_ctx *ctx, oc_status *st) {
    if (!ctx->source || ctx->source_len == 0)
        return OC_OK;
    return oc_supply_scan_secrets(ctx->graph->nodes[ctx->file_id].path, ctx->source,
                                  ctx->source_len, ctx->arena, ctx->out, st);
}

const oc_pass oc_pass_secret = {
    "OC011",
    "secret-pattern",
    OC_SEV_ERROR,
    "credential-shaped pattern in source (AWS AKIA key id, PEM private key "
    "header, GitHub ghp_ token, Slack xox token: error; generic secret-name "
    "assignment: warn)",
    run_secret,
};
