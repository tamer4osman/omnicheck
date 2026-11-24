/* OC010 licence -- manifests that declare no licence (WBS 4.2).
 * Manifest-level check: package.json needs a non-empty "license" string;
 * pyproject.toml needs a `license = "..."` line inside [project];
 * requirements.txt carries no licence concept and is skipped honestly.
 * See supply.h for the anchoring rule (line 1; no guessed positions). */
#include "passes.h"

#include "oc/supply.h"

static oc_code run_licence(const oc_pass_ctx *ctx, oc_status *st) {
    if (!ctx->manifest)
        return OC_OK;
    const oc_manifest *m = ctx->manifest;
    if (m->kind == OC_MANIFEST_REQUIREMENTS)
        return OC_OK; /* no licence concept in requirements.txt */
    /* SPRINT-14: gradle and the legacy NuGet manifests carry no licence
     * field -- their tiers are OC009-only by contract (the pom.xml/.csproj
     * licence layers are unchanged); skipping them honestly is the same
     * decision as requirements.txt. SPRINT-15: Cargo.lock likewise (the
     * lockfile tier is OC009-only; Cargo.toml carries the layered licence
     * rule and participates). SPRINT-19: composer.lock likewise (the
     * lockfile tier is OC009-only; composer.json carries the `license`
     * field and participates, the package.json conventions). */
    if (m->kind == OC_MANIFEST_GRADLE || m->kind == OC_MANIFEST_PKGS_CONFIG ||
        m->kind == OC_MANIFEST_PKGS_LOCK || m->kind == OC_MANIFEST_CARGO_LOCK ||
        m->kind == OC_MANIFEST_COMPOSER_LOCK)
        return OC_OK;
    if (m->license && m->license[0])
        return OC_OK;
    char *msg = oc_arena_sprintf(ctx->arena,
                                 "%s declares no licence; supply-chain health needs an "
                                 "explicit licence field",
                                 oc_manifest_kind_str(m->kind));
    if (!msg) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
        return OC_ERR_ALLOC;
    }
    oc_finding_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.rule_id = "OC010";
    spec.rule_name = "licence";
    spec.severity = OC_SEV_WARN;
    spec.file = m->path;
    spec.line = 1;
    spec.col = 1;
    spec.end_line = 1;
    spec.symbol = "license";
    spec.message = msg;
    if (!oc_findings_push(ctx->out, ctx->arena, &spec)) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
        return OC_ERR_ALLOC;
    }
    oc_status_clear(st);
    return OC_OK;
}

const oc_pass oc_pass_licence = {
    "OC010",
    "licence",
    OC_SEV_WARN,
    "manifest declares no licence (package.json \"license\" / pyproject.toml "
    "[project] license / composer.json \"license\"); requirements.txt and the "
    "lockfile tiers are out of scope",
    run_licence,
};
