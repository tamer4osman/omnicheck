/*
 * T4: OmniDecide brain memory (advisory experience layer).
 *
 * The brain is the kernel's OWN persistence (odk_brain_* snapshot v3):
 * OmniCheck stores the kernel's file verbatim, keyed by finding text
 * under the ONE fixed triage instruction. Guardrails:
 *
 *   advisory-only : the brain NEVER changes severity, existence, verdict
 *                   or report order. It annotates TRIAGED findings with
 *                   a brain_rank/brain_bonus pair -- brain_rank orders
 *                   triaged findings by experience-adjusted confidence
 *                   (adjusted = kernel confidence + OC_BRAIN_WEIGHT *
 *                   bonus, clamped to [0,1]; ties keep the kernel rank
 *                   order). The advisory priority chain is
 *                   explicit flags > brain advise > raw score.
 *   opt-in        : consults happen only with --triage-brain; without
 *                   it, reports stay byte-identical to the base report.
 *   fail-open     : a missing brain file is a no-op; a corrupt one is
 *                   reported honestly and ignored (scans never fail
 *                   because of advisory memory). EXPLICIT `oc brain`
 *                   commands fail loud instead -- they are the human
 *                   interface, not pipeline components.
 *   determinism   : advise() is documented deterministic; no wall-clock
 *                   anywhere; identical brain file + identical scan =>
 *                   identical report bytes.
 *   provenance    : the report's brain block carries the file path,
 *                   kernel epoch, weight and adjusted count; `oc brain
 *                   info` additionally carries the store's sha256.
 *   atomicity     : store writes reuse the kernel's own atomic
 *                   save (write <path>.tmp, fsync, rename) -- the H-4
 *                   crash-safety discipline.
 */
#ifndef OC_BRAIN_H
#define OC_BRAIN_H

#include "finding.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ONE triage instruction, shared by every first-party kernel caller
 * (embedded provider, explain, brain). The external shim
 * tools/omnidecide/oc_odk_provider.c keeps its own mirrored copy --
 * they must stay identical or embedded-vs-sidecar byte equality breaks. */
#define OC_TRIAGE_INSTRUCTION                                                                      \
    "Rank these static-analysis findings from most to least important "                            \
    "for a developer to address first."

/* How strongly experience shifts the advisory confidence (clamped to
 * [0,1] with the kernel confidence). Fixed in v1 for determinism. */
#define OC_BRAIN_WEIGHT 0.5

/* Outcome -> reward mapping for `oc brain record` (kernel clamps to
 * [-1,1]; >= +0.05 counts a win, <= -0.05 a loss). */
#define OC_BRAIN_REWARD_ACCEPTED  1.0
#define OC_BRAIN_REWARD_FIXED     0.5
#define OC_BRAIN_REWARD_DISMISSED (-1.0)

/* Default brain file when OC_BRAIN_FILE is unset (relative to the CWD
 * of the scan -- visible, deterministic, project-local). */
#define OC_BRAIN_DEFAULT_FILE ".oc-brain.json"

typedef struct oc_brain_outcome {
    int requested;    /* --triage-brain given (the report emits the block) */
    int applied;      /* memory consulted and merged into annotations */
    char file[512];   /* brain file path used */
    char reason[192]; /* human explanation when !applied */
    size_t adjusted;  /* findings carrying brain annotations */
    long long epoch;  /* kernel epoch of the loaded memory; -1 unknown */
} oc_brain_outcome;

/* Consult the brain for the triaged findings in v. Rank-only: annotates
 * brain_rank/brain_bonus, never changes severity/order/verdict. NEVER
 * fails the scan: every failure path fail-opens with a named reason and
 * returns OC_OK. Only findings with triaged==1 can be annotated. */
oc_code oc_brain_adjust_findings(oc_finding_vec *v, oc_brain_outcome *out, oc_status *st);

/* `oc brain info [--brain-file F]`: wrapper document around the kernel's
 * own summary payload, plus the store's path, sha256 and kernel epoch.
 * Fails loud on a corrupt file. */
oc_code oc_brain_cmd_info(const char *file, FILE *out, oc_status *st);

/* `oc brain create [--brain-file F]`: create a fresh (empty) brain store.
 * Fails loud when the file already exists. */
oc_code oc_brain_cmd_create(const char *file, FILE *out, oc_status *st);

/* `oc brain record --report R --fingerprint FP --outcome VERDICT`:
 * records one outcome keyed by the finding's message text (resolved
 * from the report by fingerprint) and saves atomically. Fails loud on
 * unknown fingerprints, unknown verdicts, unreadable reports. */
oc_code oc_brain_cmd_record(const char *file, const char *report_path, const char *fingerprint,
                            const char *outcome, FILE *out, oc_status *st);

/* `oc brain advise --report R [--brain-file F]`: the advisory layer as a
 * standalone query -- per-finding experience bonus and the
 * experience-adjusted confidence, printed as a deterministic document.
 * Never mutates the store. */
oc_code oc_brain_cmd_advise(const char *file, const char *report_path, FILE *out, oc_status *st);

/* `oc brain entries [--brain-file F]`: the kernel's full memory listing
 * (keys, wins/losses/draws, visits, ema, bonus), wrapped with the store
 * path + sha256. Fails loud on a corrupt file. */
oc_code oc_brain_cmd_entries(const char *file, FILE *out, oc_status *st);

/* `oc brain discard --brain-file F --index N`: remove the entry at INDEX
 * (see `oc brain entries` for the indexing) and save atomically. Fails
 * loud on an unknown index or a corrupt store. */
oc_code oc_brain_cmd_discard(const char *file, long index, FILE *out, oc_status *st);

/* `oc brain reset [--brain-file F]`: remove the brain file (absent file
 * is an honest no-op, not an error). */
oc_code oc_brain_cmd_reset(const char *file, FILE *out, oc_status *st);

/* `oc brain save [--brain-file F]`: load (or create when absent) and
 * re-save the store atomically -- normalization/explicit flush. */
oc_code oc_brain_cmd_save(const char *file, FILE *out, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_BRAIN_H */
