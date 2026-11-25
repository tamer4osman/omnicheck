/*
 * OmniDecide triage sidecar (plan Ch4 integration contract).
 *
 * Protocol (v0): spawn $OC_OMNIDECIDE_BIN, write ONE JSON line on stdin:
 *   {"op":"rank","items":[{"id":"<fingerprint>","text":"<message>"}, ...]}
 * read ONE JSON line on stdout:
 *   {"model":"...","ranks":[{"id":"...","rank":0,"confidence":0.42}, ...]}
 *
 * Guardrails, enforced here and asserted by tests:
 *   1. rank-only: triage may annotate findings (triage_rank /
 *      triage_confidence) but NEVER drop, reorder the report, or change
 *      severity. The report remains byte-deterministic.
 *   2. prose-only inputs: items carry finding message text only.
 *   3. confidence exposure: provider + confidence are printed in the
 *      report whenever ranks are applied.
 *   4. fail-open: missing/broken/slow provider leaves the report complete
 *      and unranked, never fails the run. ONE total budget (default 3 s;
 *      override OC_OMNIDECIDE_TIMEOUT_S, 1..600) covers the request write
 *      AND the response read, enforced with poll() deadlines; a timed-out
 *      provider is killed (SIGKILL) and reported as "triage timed out".
 *      A provider dying mid-request yields fail-open (SIGPIPE blocked for
 *      the interaction), never a terminated scan.
 */
#ifndef OC_TRIAGE_H
#define OC_TRIAGE_H

#include "finding.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_triage_outcome {
    char provider[128]; /* resolved binary path or "none" */
    char model[128];    /* provider-reported model id, "" when absent */
    char reason[192];   /* human explanation when ranked == 0 */
    int ranked;
    size_t ranked_count;
} oc_triage_outcome;

oc_code oc_triage_rank_findings(oc_finding_vec *v, oc_triage_outcome *out, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_TRIAGE_H */
