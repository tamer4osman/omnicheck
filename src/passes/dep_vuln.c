/* OC009 dep-vuln -- vendored OSV snapshot matching (WBS 4.2).
 * Runs on manifest records (supply.h): package.json exact pins and
 * requirements.txt == pins are matched against the vendored snapshot;
 * unpinned requirements are reported (warn) because they cannot be
 * matched honestly. Without --osv-snapshot the pass never fires and the
 * report meta says the snapshot was not provided. */
#include "passes.h"

#include <stdio.h>

#include "oc/supply.h"

static oc_code run_dep_vuln(const oc_pass_ctx *ctx, oc_status *st) {
    if (!ctx->manifest || !ctx->osv)
        return OC_OK;
    const oc_manifest *m = ctx->manifest;
    for (size_t i = 0; i < m->n_deps; i++) {
        const oc_dep *d = &m->deps[i];
        int v = oc_osv_match(ctx->osv, d->name, d->version, m->eco);
        const char *sev_str = "warn";
        oc_severity sev = OC_SEV_WARN;
        const char *what;
        if (v == 1) {
            what = "matched the vendored OSV snapshot";
            sev = OC_SEV_ERROR;
            sev_str = "error";
        } else if (v == 2) {
            what = "is present in the OSV snapshot but unpinned (cannot be matched)";
        } else if (!d->pinned) {
            what = "is not pinned; skipped by the OSV match (never guessed)";
        } else {
            continue;
        }
        char *msg = oc_arena_sprintf(ctx->arena, "Dependency '%s'%s%s %s", d->name,
                                     d->version ? " " : "", d->version ? d->version : "", what);
        if (!msg) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
            return OC_ERR_ALLOC;
        }
        oc_finding_spec spec;
        memset(&spec, 0, sizeof(spec));
        spec.rule_id = "OC009";
        spec.rule_name = "dep-vuln";
        spec.severity = sev;
        spec.file = m->path;
        spec.line = d->line;
        spec.col = 1;
        spec.end_line = d->line;
        spec.symbol = d->name;
        spec.message = msg;
        (void)sev_str;
        if (!oc_findings_push(ctx->out, ctx->arena, &spec)) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
            return OC_ERR_ALLOC;
        }
    }
    oc_status_clear(st);
    return OC_OK;
}

const oc_pass oc_pass_dep_vuln = {
    "OC009",
    "dep-vuln",
    OC_SEV_ERROR,
    "pinned dependency matches the vendored OSV snapshot (error); unpinned "
    "requirement cannot be matched (warn); needs --osv-snapshot",
    run_dep_vuln,
};
