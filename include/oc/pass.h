/*
 * Analysis pass interface and tiny key/value config.
 *
 * Passes are pure functions over (graph, file doc, pack, config) that
 * append findings to a sink. They never mutate the graph, never do IO,
 * and never decide alone what ships (plan Ch4 determinism contract).
 */
#ifndef OC_PASS_H
#define OC_PASS_H

#include "finding.h"
#include "graph.h"
#include "pack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_config {
    char (*keys)[64]; /* heap-owned arrays of char[64] */
    char (*vals)[64];
    size_t n;
    size_t cap;
} oc_config;

void oc_config_init(oc_config *c);
int oc_config_set(oc_config *c, const char *key, const char *val);
long long oc_config_get_int(const oc_config *c, const char *key, long long fallback);
void oc_config_destroy(oc_config *c);

typedef struct oc_pass_ctx {
    const oc_graph *graph;
    oc_node_id file_id;
    const oc_doc *doc; /* nullable (file without comments) */
    const oc_pack *pack;
    const oc_config *config;
    oc_arena *arena;
    oc_finding_vec *out;
    /* WBS 3.x: precomputed global analysis state. Both nullable when the
     * CLI has nothing to attach (surface is always built; coverage only
     * with --coverage). Passes read them; they never mutate the graph. */
    const struct oc_surface *surface;
    const struct oc_coverage *coverage;
    /* WBS 2.4: precomputed duplication index (OC008). Always built for
     * 'oc check'; NULL when the CLI has nothing to attach. */
    const struct oc_dup_index *dup;
    /* WBS 4.2 (Sprint 5): supply-chain tier. For the manifest-level
     * passes (OC009/OC010) the scan sets manifest to the parsed record;
     * for per-file secret scanning (OC011) source points at the current
     * file's raw bytes. All three are NULL in ordinary graph-pass runs
     * (OC001..OC008 never see them and must not need them). */
    const struct oc_manifest *manifest;
    const struct oc_osv_snapshot *osv;
    const char *source;
    size_t source_len;
} oc_pass_ctx;

typedef struct oc_pass {
    const char *id;   /* "OC001" -- stable across releases */
    const char *name; /* "long-function" */
    oc_severity default_severity;
    const char *description;
    oc_code (*run)(const oc_pass_ctx *ctx, oc_status *st);
} oc_pass;

const oc_pass *oc_pass_lookup(const char *id);
const oc_pass *const *oc_pass_all(size_t *n);
#define OC_PASS_COUNT 11

#ifdef __cplusplus
}
#endif
#endif /* OC_PASS_H */
