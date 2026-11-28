/*
 * T3: `oc diff` -- introduced/resolved/persistent diff between two
 * OmniCheck reports.
 *
 * Input : TWO omnicheck.report/v0 documents (--baseline, --current).
 * Output: an omnicheck.reportdiff/v0 JSON document on stdout.
 *
 * FINGERPRINT matching (the whole point): findings are compared by
 * FINGERPRINT, and fingerprints deliberately exclude line numbers (they
 * hash rule_id + file + anchor [, normalized snippet]) so a finding that
 * merely MOVED is persistent, not a removed+added pair. The KERNEL never
 * decides identity: classification is pure fingerprint multiset math.
 * Identical byte-identical siblings share one fingerprint, so both sides
 * are compared as MULTISETS: a fingerprint present twice in the baseline
 * and once in the current yields one resolved entry.
 *
 * Output sections (fixed field order, deterministic; entries ordered by
 * fingerprint asc, the sorted view's stable total order):
 *   baseline / current  per-side identity (path, finding_count, schema)
 *   introduced          fingerprints present more often in current
 *                       (delta count only), each with rule/file/line/
 *                       message for humans -- line numbers are NOT part
 *                       of the comparison
 *   resolved            the mirror image in baseline order
 *   moved               same fingerprint, different line: occurrence k in
 *                       baseline paired with occurrence k in current;
 *                       informational only, never a gate failure
 *   persistent          multiset intersection count
 *
 * --semantic (T3 opt-in): each introduced entry additionally carries
 *   "advisory":{"rank":N,"confidence":X.XXXX} -- the embedded kernel's
 *   rank of that finding within the CURRENT report (one rank call over
 *   all current findings). ADVISORY ONLY: it never changes the verdict,
 *   the exit code, or the classification. Requires the embedded kernel;
 *   a build without it refuses loud (an explicit query flag never fails
 *   open).
 *
 * Exit codes (CI gate semantics, like oc compat):
 *   0  no semantic change (introduced == resolved == 0)
 *   1  semantic changes exist
 *   2  tool error: unreadable files, malformed JSON, foreign schemas,
 *      oversized inputs, or a failed --semantic advisory run
 *      (fail loud; a diff never fails open)
 */
#ifndef OC_REPORTDIFF_H
#define OC_REPORTDIFF_H

#include <stdio.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Diff the two reports. want_semantic enables the advisory scores
 * (--semantic). *semantic_changes (when non-NULL) is set to 1 when
 * introduced or resolved entries exist (the CLI maps that to exit 1),
 * 0 otherwise. Returns OC_OK (0) on success or a non-zero oc_code with
 * st set on the fail-loud paths (CLI maps those to exit 2). */
oc_code oc_reportdiff_run(const char *baseline_path, const char *current_path,
                          int want_semantic, int *semantic_changes, FILE *out,
                          oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_REPORTDIFF_H */
