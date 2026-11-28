/*
 * T5: decision pins and replay (the spine memory).
 *
 * The kernel spine (odk_spine, F6) records an append-only DecisionPin
 * per pinned decision -- request bytes, decision, confidence, schema
 * fingerprint, kernel version, caller replay class. The spine's store
 * lives in memory only, so OmniCheck persists each pin document in its
 * OWN store file (OC_SPINE_FILE or .oc-spine.json) and replays from
 * there.
 *
 * Contract (C3 resolution):
 *   - pin add    : evaluates the request through the REAL spine
 *                  (odk_spine_pin), appends the pin document to the
 *                  store atomically, fails loud on bad requests.
 *   - replay     : FAILS LOUD when the pin's schema fingerprint differs
 *                  from the running build's -- the kernel or its schema
 *                  changed and old pins are no longer interpretable;
 *                  re-pin the request instead. Otherwise re-derives the
 *                  decision with the current kernel and reports one of
 *                  the REPLAY.md classes:
 *                    exact    same decision, bit-identical confidence
 *                    semantic same decision, different bytes
 *                    mismatch different decision (replay gate: exit 1)
 *                    foreign  recorded lineage only, not re-derivable
 *                  Verdicts exact/semantic/foreign exit 0.
 *   - determinism: no wall-clock anywhere (the caller-supplied pin
 *                  timestamp is 0); ids are store-order counters;
 *                  identical request + identical kernel => identical
 *                  verdict.
 */
#ifndef OC_SPINE_H
#define OC_SPINE_H

#include <stdio.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default pin store when OC_SPINE_FILE is unset (project-local, visible). */
#define OC_SPINE_DEFAULT_FILE ".oc-spine.json"

/* `oc pin add --request <json> [--class exact|semantic|foreign]`: pins
 * the decision and appends it to the store. Fails loud on invalid
 * requests. Prints the pin document on stdout with the save receipt schema:
 * {"schema":"omnicheck.spine.pin/v0","id":"<store-order-pin-id>","file":"<store-path>","saved":true}
 * The id is a deterministic counter (p000000, p000001, ...); file is the
 * store path used; saved is true on successful persistent storage. */
oc_code oc_spine_cmd_add(const char *file, const char *request, const char *replay_class, FILE *out,
                         oc_status *st);

/* `oc replay --id <pin-id>`: replay one pinned decision (see the class
 * contract above). *verdict_out (when non-NULL) receives 0 for
 * exact/semantic/foreign and 1 for mismatch (the CLI gate code). */
oc_code oc_spine_cmd_replay(const char *file, const char *pin_id, FILE *out, int *verdict_out,
                            oc_status *st);

/* `oc pin list`: deterministic listing of the store. */
oc_code oc_spine_cmd_list(const char *file, FILE *out, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_SPINE_H */
