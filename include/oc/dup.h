/* OC008 duplicate-code -- language-agnostic duplication index (WBS 2.4,
 * plan Table 8 "Duplication fingerprint analysis"). Built ONCE per run
 * from the analyzed file set (like the Sprint 2 surface/coverage global
 * state), attached to the pass context; the per-file pass emits one
 * finding per duplicated location.
 *
 * Contract (determinism first, no hash trust anywhere):
 *  1. Every analyzed file's text is split into lines and NORMALIZED
 *     (CR stripped, leading/trailing whitespace trimmed, internal
 *     whitespace runs collapsed to one space). Blank lines normalize to
 *     the empty string and are kept for span arithmetic.
 *  2. Seed windows of OC008.min_block (default 10) consecutive lines are
 *     fingerprinted per file; equal-fingerprint runs with >= 2 locations
 *     are candidates. Every candidate is verified by direct text
 *     comparison -- fingerprints are only a bucketing device; a 64-bit
 *     collision can never produce a false duplicate.
 *  3. Each verified seed is EXTENDED in lockstep: line t beyond the span
 *     must textually match across ALL members (relative to each member's
 *     own span), forward and backward. All seeds inside one maximal
 *     block converge to the same maximal spans.
 *  4. Extended spans are deduplicated by their exact member location
 *     set; groups with >= 2 locations become dup groups. Groups are
 *     capped at OC008.max_groups (default 256); the overflow is counted
 *     in meta (dup_groups_truncated), never silently dropped.
 *  5. Files larger than OC008.max_file_bytes (default 1,000,000 bytes)
 *     are skipped and counted (dup_files_skipped_oversize).
 *
 * The index allocates exclusively from the caller's arena (no destroy);
 * build-time scratch is freed before return, so LSan-clean by design.
 */
#ifndef OC_DUP_H
#define OC_DUP_H

#include "common.h"
#include "graph.h"
#include "pass.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OC_DUP_DEFAULT_MIN_BLOCK      10LL
#define OC_DUP_DEFAULT_MAX_FILE_BYTES 1000000LL
#define OC_DUP_DEFAULT_MAX_GROUPS     256LL

typedef struct oc_dup_member {
    size_t file_idx; /* index into oc_dup_index.files */
    size_t start;    /* 1-based first line of the duplicated span */
    size_t end;      /* 1-based last line (inclusive) */
} oc_dup_member;

typedef struct oc_dup_group {
    uint64_t text_hash;     /* FNV-1a over the normalized span text */
    size_t span_lines;      /* end - start + 1 of any member (lockstep) */
    oc_dup_member *members; /* arena; sorted by (path, start) */
    size_t n_members;
    const char *message; /* arena; shared by all findings of the group */
} oc_dup_group;

typedef struct oc_dup_file {
    size_t node_id;   /* graph node id of the file node */
    const char *path; /* graph path (arena-owned by the graph) */
} oc_dup_file;

typedef struct oc_dup_index {
    oc_dup_file *files; /* arena; sorted by path */
    size_t n_files;
    oc_dup_group *groups; /* arena; sorted by (files[members[0]].path, start) */
    size_t n_groups;
    size_t files_skipped_oversize;
    size_t groups_truncated;
} oc_dup_index;

/* Build the duplication index over every file node in the graph.
 * Returns NULL only when the graph has no analyzable file content; on
 * hard errors sets *st. Config keys: OC008.min_block, OC008.max_file_bytes,
 * OC008.max_groups. */
oc_dup_index *oc_dup_build(const oc_graph *g, const oc_config *cfg, oc_arena *a, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_DUP_H */
