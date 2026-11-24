# Sprint 8 - the memo's L-track adoption: explain auditor, run-artifacts
# channel, replay ritual, budget profiles, calibration workflow (design)

Executed 2026-11-20 (tenth session). Predecessor: `docs/SPRINT-7.md`
(kernel vendoring, decision L8). Scope: the user's "let us finish the
rest" -- execute every finishable adoption step of
`docs/OMNIDECIDE_LEVERAGE.md` (L1-L7) that does not touch verdict
logic and does not require outcome data that does not exist. The three
guardrails, the five hard constraints and the vendored tree's
"do not edit" rule are all untouched; no analyzer judgment logic was
written or approved here.

## A decision discovered mid-sprint: the sibling-identity contract

The explain auditor was first written with a naive "ids are unique"
model -- and it REFUSED the real-corpus evidence run (a duplicate
fingerprint in the request). The refusal was correct for the naive
model but wrong for the seam's actual, documented identity model
(`src/triage.c`, apply loop): **fingerprints deliberately exclude line
numbers, so byte-identical siblings share one identity and must all be
annotated**. The evidence run's instance: OC005 reports
`QueryErrorKind` twice in `third_party/tree-sitter/lib/binding_web/src/
query.ts` (lines 203 and 212) under one fingerprint; the pipeline
annotates all 160 findings consistently (160/160 annotations match the
artifact confidences; `ranked_count` counts distinct findings, not
response entries). The auditor was aligned to the seam contract:
duplicate ids are allowed ONLY when every carrier's text is
byte-identical; one id behind two different texts is the fba5242
defect class and is refused, as is any id appearing more often than the
request carries it. Both directions are test-locked (A9/A10 in
`tests/test_triage_audit`). This is the memo's thesis working again:
using the kernel harder is a defect detector for OmniCheck itself --
this time it caught a CONTRACT mismatch, not a code bug.

## Delivered

### 1. L1 -- explain auditor in-repo (`oc-omnidecide-explain`)

- `tools/omnidecide/oc_odk_explain.c`: dump mode (byte-compatible with
  the pilot tool: per-item probability + fused components, best-first)
  plus a fail-CLOSED check mode. Check mode audits a captured provider
  response against its request and exits 2 with a precise diagnostic
  on: model mismatch, response id not in request, id multiplicity
  violation, carrier disagreement, 4-decimal confidence mismatch,
  confidence increasing along rank order (softmax is monotone in
  fused), rank field out of sequence, or garbage/empty input. An empty
  ranks list is a fail-open response and audits as "nothing to check".
- Root CMake builds it as a first-class target whenever the vendored
  kernel is present; it is an auditor, never a pipeline component.
- Ownership is single-owner (main frees the request tree), verified
  leak-free under ASan+UBSan+LSan.

### 2. L2 -- run-artifacts channel (default OFF)

- `tools/omnidecide/oc_odk_provider.c`: when `OC_OMNIDECIDE_ARTIFACTS_DIR`
  is set, the provider writes `request-<pid>.json` (the byte-exact
  protocol request) and `triage-<pid>.json` (the FULL uncapped ranked
  list + explain components per ranked item + `explain_agreement`).
- Best-effort by contract: stdout is emitted and flushed FIRST, the
  artifacts are documentation and never an input; every artifact
  failure (including a too-long path or a partial write) is silent and
  removes partial files rather than leaving truncated ones.
- `explain_agreement` is measured, not asserted: every ranked score is
  compared against the explained probability at 4 decimals (the
  EXPLAIN.md exactness contract); a disagreement would be recorded,
  never hidden, and would not change the stdout response.
- Cost honesty: the explain pass adds measurable compute -- measured on
  the pinned eslint request (2,398 items): 0.26 s -> 0.38 s provider
  wall (x1.5). The README ties the channel to the CI depth profile.

### 3. L6 -- replay ritual

- The request pin makes every ranking re-derivable:
  `provider < request-pin > replayed` must be byte-identical to the
  captured response. Locked by `tests/test_triage_audit` section C and
  reproduced at real-corpus scale (below). Kernel-level decision pins
  remain available unmodified via the spine API
  (`odk_spine_pin`/`odk_spine_replay`, REPLAY.md).

### 4. L5 -- budget profile matrix documented

- README.md "Budget profiles": interactive 3 s default (fail-open at
  pathological scale is the honest result; 30,200 findings need ~34 s
  of kernel time, measured in the memo's appendix) vs CI depth 60 s
  (~928/2,380 ranked on the pinned eslint corpus). The matrix exists so
  nobody "fixes" the interactive default upwards.

### 5. L3/L4/L7 -- the feedback edge, designed before data exists

- `docs/TRIAGE_CALIBRATION_WORKFLOW.md` (new): the outcome-log schema
  (binary labels only; `unknown` is an absent line, never a guess), the
  monthly ritual over the kernel's own spine API
  (`odk_spine_observation_ingest` + `odk_spine_calibrate`, whose report
  is byte-identical over the same log), and a PRE-COMMITTED exposure
  rule (worst per-bin error <= 0.10 over >= 100 outcomes) fixed BEFORE
  any data exists so the threshold cannot be tuned after seeing the
  numbers. L4 stays gated on a PASSING calibration; L7 stays research
  track. No outcome data exists, so nothing here is a measured claim.

### 6. Tests (24 suites per cell now; 77 + 56 + 134 triage checks)

- `tests/test_triage_audit` (new, 134 checks) in both build cells:
  A) auditor agreement + fail-closed contract (honest response passes;
     unknown id / multiplicity violation / mangled confidence / model
     mismatch / garbage / missing file all exit 2; empty fail-open
     response audits as nothing-to-check; the sibling-identity model
     passes end to end; the ambiguous-join class refuses);
  B) artifacts channel: stdout byte-identical with the channel ON and
     OFF (direct protocol AND e2e `oc check`), the two pin files appear,
     the request pin is byte-exact, artifact confidences match the
     report annotations, a broken artifacts path changes nothing;
  C) replay: the captured pin re-derives the response byte-for-byte.
- Stale-artifact hygiene: the artifacts channel keeps append-only
  history (one pid-named pair per run), so tests that inspect THEIR OWN
  run's pin wipe the directory first -- a stale pin from a previous
  run/corpus must never be picked up.
- Cell results at HEAD: 24/24 default, 24/24 sanitize (two full runs
  each; includes the kernel's own 583 upstream checks in both cells).

## Evidence (real corpus, this HEAD, default cell)

Full pipeline on the vendored tree-sitter C tree (124 files parsed,
160 findings, all 160 annotated by the real provider):

- artifacts ON vs OFF: report bytes IDENTICAL (68,289 bytes), wall
  0.16 s both ways at this scale.
- pins: request pin 29,822 bytes; triage artifact 44,316 bytes; schema
  `oc-triage-artifact/1`; items=160 ranked=160 emitted=160;
  `explain_agreement: true`.
- cross-check: 160/160 pipeline `triage_confidence` annotations agree
  with the artifact confidences at 4 decimals.
- replay: two provider runs on the request pin are byte-identical
  (9,051-byte response); the auditor verifies 160/160 entries against
  the explained probabilities (sibling-model aware).
- The same artifacts channel exercised at pilot scale on the pinned
  eslint request capture (2,398 items, 1,348,133 bytes): stdout capped
  at 928 entries (52,827 bytes) while the artifact carries ALL 2,398
  ranked items with components (668,558 bytes); artifact prefix equals
  the stdout response exactly; the pin byte-equals the source request;
  replay byte-identical; provider wall 0.26 s (OFF) vs 0.38 s (ON).
  The AUDITOR refuses this capture with the fba5242 diagnostic --
  correctly: the pre-fix capture contains exactly 2 ids behind two
  different texts each (`constructor` 62/114 lines, `fix` 90/100
  lines, the historical OC001 fingerprint collisions), 2,398 items
  over 2,374 distinct ids. A live demonstration that the check-mode
  auditor detects the defect class it was built for; the fresh
  real-corpus run above (post-fix fingerprints) audits clean.

## Deliberately unchanged

- The runtime seam (`include/oc/triage.h`, budget + process-group kill +
  fail-open from 193a5f2), the mock provider and its contract tests
  (`tests/test_triage`, 77 checks, provider-agnostic), the unranked
  report bytes, the vendored kernel sources (VENDOR.sha256 gate), the
  brain and model paths (OFF / research), and every analyzer verdict.
- The memo's rejected directions stay rejected (no `noul`/`score`/
  `compare`, no code-context rank inputs, no persistent sidecar
  process, no message-text caps, no AUTO language/profile).

## NEXT

- L3 waits for real outcome data; the workflow doc is the contract until
  then. L4 unlocks only behind a passing calibration; L7 behind held-out
  labeled validation.
- Deferred analyzer upgrades remain user decisions, unchanged from the
  sprint-7 record (TS config parsing scope, unused-exports scope).
- V1_TAG_RITUAL steps 7-9 remain human-executed (2027-12-31).
