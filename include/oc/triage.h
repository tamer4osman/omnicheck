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
    /* T1 (--triage-explain): rationale attach outcome. Emitted in the
     * report meta only when the flag was given, so runs without it stay
     * byte-stable. */
    int explain_requested;
    size_t explained_count;   /* distinct findings carrying rationale */
    char explain_reason[192]; /* "ok" or the named fail-open/n-a reason */
} oc_triage_outcome;

oc_code oc_triage_rank_findings(oc_finding_vec *v, oc_triage_outcome *out, oc_status *st);

/* T2: provider selection. The sidecar (OC_OMNIDECIDE_BIN) keeps ABSOLUTE
 * precedence in both modes; the embedded kernel is the in-process twin
 * of the sidecar shim and speaks the byte-identical protocol-v0 response
 * (asserted byte-for-byte by tests/test_triage_embed.c). */
typedef enum oc_triage_mode {
    OC_TRIAGE_MODE_AUTO = 0,    /* historical --triage: sidecar when
                                 * OC_OMNIDECIDE_BIN is set, else triage
                                 * disabled (fail-open) */
    OC_TRIAGE_MODE_EMBEDDED = 1 /* --triage-embedded: run the in-process
                                 * embedded kernel; a build without it
                                 * refuses loud (OC_ERR_USAGE) */
} oc_triage_mode;

oc_code oc_triage_rank_findings_mode(oc_finding_vec *v, oc_triage_outcome *out, oc_status *st,
                                     oc_triage_mode mode);

/* T1 (--triage-explain): attach the kernel's 9 rationale channels to the
 * ranked findings, in place. Advisory and fail-open: any failure leaves
 * the findings unexplained and names the reason in out->explain_reason;
 * rank arithmetic is untouched (the explain pass is read-only per
 * EXPLAIN.md). Rationale requires the embedded kernel: the sidecar
 * protocol-v0 carries no rationale channel, and that case is named, not
 * silently dropped. */
oc_code oc_triage_explain_attach(oc_finding_vec *v, oc_triage_outcome *out, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_TRIAGE_H */
