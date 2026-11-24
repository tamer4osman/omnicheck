# Leveraging OmniDecide harder, without compromising the results

Status: proposal memo, 2026-11-20; L-track adoption recorded in
SPRINT-8 the same day. Ground truth (updated 2026-11-20, decision L8
below): the OmniDecide-C kernel (v2.0.0, Apache-2.0) ships IN-REPO,
unmodified, under `third_party/omnidecide/` (integrity-pinned by
`VENDOR.sha256`, provenance in its `PROVENANCE.md`); the protocol-v0
provider is `tools/omnidecide/oc_odk_provider.c`, and the runtime seam
in `src/triage.c` is unchanged (spawn + JSON lines, opt-in, fail-open).
Every proposal below is checked against the five hard constraints.
Nothing in this memo changes analyzer verdict logic.

Relationship to the 2026-11-19 analysis
(`download/OmniCheck_OmniDecide_Leverage.md`): its recommendations 2-5
are refined here as L2/L3/L5/L6; its recommendation 1 (vendor the
kernel) is restated as the open decision L8 rather than a settled
choice; its defect findings (hang, SIGPIPE) landed in commit
`193a5f2`.

## 0. The constraints (unchanged, enforced)

1. **Verdicts stay in OmniCheck.** OmniDecide may rank; it may never
   decide what a finding is, whether it is real, or how severe it is.
   The AI-side rule stands: no analyzer judgment logic is written or
   approved here.
2. **Process boundary.** Integration is `$OC_OMNIDECIDE_BIN` + one JSON
   line in, one JSON line out. Amended by decision L8 (2026-11-20): the
   kernel SOURCE now ships in-repo (vendored, unmodified, integrity-
   pinned -- see L8), but the RUNTIME boundary is unchanged: the analyzer
   only ever talks to a spawned provider process, never links ranking
   logic into its own code.
3. **Guardrails hold.** rank-only annotation; prose-only inputs (finding
   message text is the only per-finding data that crosses); confidence
   exposed next to the provider id; fail-open on any provider failure.
4. **Determinism and honesty.** Byte-identical reruns; honest meta
   counters; fail-open is reported, never hidden.
5. **Evidence process.** Every change ships with tests and a real-corpus
   run; claims are measured, not asserted.

## 1. What is already proven (do not re-litigate)

- The real kernel ranks OmniCheck findings end to end: examples 10/10,
  byte-identical reruns; eslint lib/ 2,380 findings, 928 annotated
  (39%) in 0.94 s total inside the default 3 s budget (commit
  `fba5242` re-measured).
- The seam is hostile-provider-proof: one poll-deadline budget over
  write+read, process-group kill, SIGPIPE blocked (commit `193a5f2`;
  silent-slow and never-exiting providers fail-open AT the budget,
  20/20 suites default and sanitize).
- The kernel is healthy and exact: 373 + 210 = 583 checks green from
  the repo root; `odk_result_rank_items` resolves duplicate texts to
  distinct indices (probe `work_src/probe_rank_items.c`) — the kernel
  was exonerated when the sibling-identity defects turned out to be
  OmniCheck-side.
- Profiling the real provider on the pinned eslint corpus found and
  fixed three real OmniCheck defects (commit `fba5242`): the OC011
  unterminated-quote misfire (18 false secret findings on the corpus),
  OC001/OC011 fingerprint collisions, and the triage apply loop's
  first-match-break. This is the strongest argument for the proposals
  below: **using the kernel harder is also a defect detector for
  OmniCheck itself.**

## 2. Capability map: what the kernel offers vs. what the pipeline uses

| OmniDecide capability | Used today | Pipeline-safe use |
|---|---|---|
| `rank` primitive | yes (the only primitive in the pipeline) | the ONLY primitive that may enter the scan |
| explain API (`odk_explain_*`) | no | read-only introspection; decision-identical by contract (golden corpus byte-identical); audit tooling |
| per-item calibrated probability | yes (emitted as `confidence`) | calibration loop (§ L3) |
| spine F4 calibration (`observation.ingest` + `calibrate`) | no | post-hoc validation of the confidence channel |
| spine F6 pin + replay | no | audit trail for triage runs |
| brain (outcome memory) | no | opt-in, outcome-fed re-ranking bias, gated on L3 |
| model files (scoring constants) | no | opt-in, gated on labeled validation |
| text modes / profiles / languages | fixed EN, ASCII, no context | correct as-is: findings are English prose; AUTO would inject detector variance |
| `choice`/`score`/`noul`/`compare` | no | must stay out: they decide, and deciding is forbidden |

## 3. Proposals

### L1 — Explain sidecar: "why is this finding ranked here" (pilot done)

**Landed 2026-11-20 (SPRINT-8):** the auditor ships in-repo as
`tools/omnidecide/oc_odk_explain.c` (CMake target
`oc-omnidecide-explain`), dump mode plus a fail-CLOSED check mode
(exit 2 on model mismatch, ambiguous ids, 4-decimal confidence
mismatch, broken rank monotonicity or rank-sequence violations), and
`tests/test_triage_audit` locks the agreement on the real provider. On
the real-corpus evidence run the auditor verified 160/160 response
entries against the explained probabilities.

`odk_explain_choice` over the same options reproduces the rank's
calibrated probability bit-for-bit (EXPLAIN.md §4; the capture is
threaded through the REAL scoring functions, changes no arithmetic).
A standalone auditor tool (`work_src/odk_explain_tool.c`) takes the
same protocol-v0 request and emits per-item components (tf_cosine,
jaccard, trigram, whole, best_chunk, contextual, directional, fused,
probability), best-first.

Pilot result: on the examples request, the explain channel agrees with
the provider on all 10 items (order + 4-decimal probability). On the
eslint request (2,398 items pre-fix), the explained fused values match
the ranked scores; residual order differences were 4-decimal ties plus
the (since-fixed) duplicate-fingerprint cases.

Adoption: run the explain tool post-hoc on a captured request
(`oc check --triage` already prints provider + model; capture the
request by pointing `$OC_OMNIDECIDE_BIN` at a tee-style wrapper).
Zero pipeline changes, zero decision changes, fail-closed by design
(an auditor tool may exit 2 on mismatch; the pipeline provider may
not).

### L2 — Optional run-artifacts channel (provider-side, zero seam change)

**Landed 2026-11-20 (SPRINT-8):** `OC_OMNIDECIDE_ARTIFACTS_DIR` in
`tools/omnidecide/oc_odk_provider.c`, default OFF, best-effort by
contract (stdout emitted first, artifacts are documentation and never
an input); request pin byte-exact, full uncapped ranked list + explain
components + `explain_agreement`; byte-identity of the pipeline proven
by tests and by the real-corpus evidence run.

The protocol's 64 KiB response cap means large scans annotate only the
top ~900 findings (928 on eslint; the honest cap, not a defect). When
`OC_OMNIDECIDE_ARTIFACTS_DIR` is set, the provider additionally writes
a sidecar file per run: the FULL ranked list plus the explain
components for every ranked item. OmniCheck stays untouched; CI and
humans diff or audit the file afterwards. This turns the 64 KiB cap
from a ceiling into a "what the report carries" choice, with full data
retained on disk.

Adoption: provider change only, behind an env var, default off; the
artifact file is documentation, never an input to the pipeline.

### L3 — Confidence calibration loop (the missing feedback edge)

**Designed workflow documented 2026-11-20 (SPRINT-8):**
`docs/TRIAGE_CALIBRATION_WORKFLOW.md` fixes the outcome-log schema,
the monthly ritual, and the pre-committed exposure rule (worst per-bin
error <= 0.10 over >= 100 outcomes) BEFORE any data exists. Still a
design, not a measured claim: no outcome data exists yet.

`triage_confidence` is a within-request calibrated probability (the
kernel's softmax over the ranked options): it is comparable within a
scan, not across scans, and nothing today checks that it MEANS
anything. The spine ships the missing edge: `observation.ingest` +
`odk_spine_calibrate` produce a byte-identical report of predicted
confidence vs. observed outcome rate per primitive, per tier, binned
into ten buckets with per-bin calibration error (SPINE-API.md F4).

Workflow: record, per triaged finding, the human outcome ("addressed
within N days" vs. ignored) as an observation; calibrate monthly; the
per-bin error decides whether the confidence channel is trustworthy
enough to expose in dashboards. Requires real outcome data — none
exists yet, so this is a designed workflow, not a measured claim, and
it must stay that way until data arrives.

### L4 — Outcome-conditioned ranking via the brain (opt-in, gated on L3)

**Unchanged, still gated** (SPRINT-8 re-stated the gate in
`docs/TRIAGE_CALIBRATION_WORKFLOW.md` section 4): no calibrated outcome
feed exists, so the brain stays OFF in every shipped path.

The brain shifts scores toward rewarded outcomes, with hard brakes
documented in LEARNING.md: no effect on a single outcome, bonus needs
two visits, shrinks by `visits/(visits+4)`, weight configurable, the
snapshot records its text configuration and loads fail-loudly on
mismatch, and results with no brain are bit-identical. For triage the
outcome signal is real: which findings the team actually addresses.

Adoption gate: only after L3 produces a calibrated outcome feed; only
with a small weight (0.2–0.5, nudge not decide); snapshot kept next to
the artifacts (L2) and audited; the default build stays brain-less
(bit-identical to the plain kernel). Experience overriding evidence is
the kernel's documented design — for a static-analysis triage layer
that is acceptable ONLY because ranking never changes the report's
findings, order or severities.

### L5 — Budget profiles (already shipped; write them down)

**Landed 2026-11-20 (SPRINT-8):** the profile matrix is in README.md
("Budget profiles"), including the artifacts-channel compute note.

The one-budget knob (`OC_OMNIDECIDE_TIMEOUT_S`, 1..600, default 3 s)
supports a documented profile matrix: interactive scans keep 3 s
(fail-open is the honest result at pathological scale — 30,200
findings need ~34 s of kernel time regardless of text caps, measured);
CI depth runs opt into 60 s and rank ~928. Put the matrix into
README/docs so nobody "fixes" the default budget upwards for
interactive use.

### L6 — Pin and replay for triage runs (audit trail)

**Landed 2026-11-20 (SPRINT-8):** the artifacts request pin carries the
request byte-for-byte, the replay ritual is documented in README.md,
and byte-identical re-derivation is test-locked and reproduced on the
real corpus. Kernel-level pins stay available via the spine API
(`odk_spine_pin`/`odk_spine_replay`, vendored unmodified).

The spine's pin record carries `request_json` byte-for-byte, making
replay self-contained: every triage decision becomes re-derivable
(`exact` class requires bit-level identity; the report never fakes a
verdict and reports `registrations_match` honestly). Combined with L2
this gives the project's evidence discipline a third leg: reproduce
any past ranking from its pin without re-running the scan. Adoption:
provider-side or external tool; nothing in OmniCheck changes.

### L7 — Model files (calibrated scoring constants) — research track

**Unchanged, research track** (SPRINT-8 re-stated the gate in
`docs/TRIAGE_CALIBRATION_WORKFLOW.md` section 5): no validated model
exists, so no model ships and the provider keeps no
`OC_OMNIDECIDE_MODEL` passthrough.

`odk_kernel_load_model` replaces the nine hand-set constants; with no
model the kernel is byte-identical to a model-less build. MODEL.md's
own warning is the gate: a model can change which option wins, so
validate on held-out labeled data before use. With L3's outcome data
a trained model could be evaluated against the same corpus; until
then: do not ship a model. The provider can grow an
`OC_OMNIDECIDE_MODEL` env passthrough when (and only when) a validated
model exists.

### L8 — DECIDED: vendored (2026-11-20)

**Decision: vendor under `third_party/omnidecide/`.** The user resolved
the open choice on 2026-11-20 ("Bundle the OmniDecide kernel"). Landed
elements:

- Vendored tree committed unmodified (`third_party/omnidecide/`, 2.0 MB
  pruned: no build/, no 12 MB golden/ replay corpus, no e2e web demo),
  pinned by archive sha256 + per-file `VENDOR.sha256` manifest verified
  by `scripts/vendor.sh`; provenance + re-audit policy in its
  `PROVENANCE.md`.
- Provider shim landed as `tools/omnidecide/oc_odk_provider.c`; root
  CMake builds the upstream subdirectory target `omnidecide` (options
  pinned OFF) and links exactly ONE first-party binary to it:
  `oc-omnidecide-provider` (option `OC_WITH_OMNIDECIDE`, default ON).
- Every build cell now proves triage against the REAL kernel:
  `odk_smoke_test` (373) + `odk_spine_test` (210) run the upstream
  self-checks unmodified from the vendored root -- 583/583 green,
  clean under ASan+UBSan+LSan; `test_triage_real` drives the provider
  through both seams (direct protocol incl. duplicate-text siblings,
  e2e `oc check` rank-only + double opt-in + honest ranked_count +
  byte-identical reruns).
- Equivalence evidence: the landed provider is byte-identical to the
  pre-vendor pilot binary on the recorded examples / eslint (2,398
  items) / perf10k (30,200 items) requests, and the full 2,380-finding
  eslint ranked report is byte-identical modulo the env-supplied
  provider path. The mock stays for provider-agnostic contract tests;
  the seam stays opt-in (`--triage` + `$OC_OMNIDECIDE_BIN`), so
  unranked report bytes are unchanged.
- Runtime boundary retained (see amended constraint 2): spawn + JSON
  lines only; no ranking logic links into OmniCheck.

The original decision record (kept for context):

The source upload made the kernel auditable (Apache-2.0, sha256-pinned
zip, 583/583 checks re-run twice), which retires the old "private
upstream cannot be re-audited" note either way. The remaining choice
is architectural, and both sides are real:

- **Vendor under `third_party/omnidecide/`** (pinned by the zip sha256
  or a tagged upstream commit, re-audited per upstream release): makes
  the real-kernel provider a first-class build target, so every gate
  rerun proves triage against the real kernel instead of the mock.
  Cost: OmniCheck's repo carries a foreign C tree and its build
  matrix; kernel upgrades become manual vendor steps; the strict
  two-repo discipline (OmniCheck owns verdicts, OmniDecide owns
  decisions) blurs at the build level.
- **Keep the pin-and-build seam** (today's state, provider in
  work_src): OmniCheck's tests stay provider-agnostic (mock-based,
  green with or without a kernel), the process boundary keeps provider
  failures boring (fail-open, budget, process-group kill), and OmniDecide
  keeps shipping on its own release cadence. Cost: real-kernel e2e is
  an out-of-tree ritual (the §5 appendix commands), not a gate step.

This memo does not decide it. What the work proved regardless: the
provider, the seam hardening, and the three sibling-identity fixes are
all valid under BOTH options, so nothing here is stranded by the
choice.

## 4. Rejected directions (with the reason, so they stay rejected)

- **`noul`/`score`/`compare` in the pipeline**: each decides or
  re-severities; forbidden by constraint 1, full stop.
- **Code context as rank input**: breaks prose-only inputs; the triage
  layer stays a text ranker over finding messages so it can never
  second-guess code facts.
- **Persistent sidecar process**: kills the per-scan isolation that
  makes provider failures boring; provider cost is already negligible
  (0.94 s total on 2,380 findings including the scan).
- **Message text caps as default**: measured on the eslint corpus,
  a 240-char cap reorders the mid-tail (Kendall tau 0.905 over 739
  common items, top-100 overlap 86/100) while saving 0.1 s that the
  budget does not need; on perf10k (30,200 items) it is rank-identical
  (tau 1.0000) and only 5% faster because cost is dominated by option
  count. Quality risk, no needed payoff.
- **AUTO language / AUTO profile**: findings are English prose emitted
  by OmniCheck; a fixed EN config removes detector variance from the
  pipeline by construction.

## 5. Verification appendix (2026-11-20, HEAD `fba5242`)

- Kernel: `build/smoke_test` 373 checks 0 failures, `build/spine_test`
  210 checks 0 failures (run from the kernel repo root; from `build/`
  the fixture paths miss — that is a CWD contract, not a failure).
- Provider rebuild from `work_src/oc_odk_provider.c` is byte-identical
  in behavior to the committed binary on a smoke request.
- Explain pilot: examples request, 10/10 order+probability agreement;
  eslint request, fused values match, ties explained.
- Cap pilot: eslint 2,398 items full vs 240-char cap: 904/904 ranked,
  top-25 24/25, top-50 48/50, top-100 86/100, tau 0.905, 0.27 s -> 0.17 s;
  perf10k 30,200 items: 928/928, top-100 100/100, tau 1.0000, 35.9 s ->
  34.0 s.
- Scale probe: 30,200 findings, 9.1 MB request, full-text kernel rank
  35.9 s -> honest fail-open at the 3 s default budget by design.
- Fixes: `fba5242` (three sibling-identity defects, 20/20 default and
  sanitize); seam hardening `193a5f2` (silent-slow and hang providers
  fail-open at the budget).
- e2e after fixes: eslint 2,380 findings, 928 annotated in 0.94 s,
  byte-identical reruns, order intact; examples 10/10, byte-identical
  reruns, same finding set, 2/10 fingerprints changed where the OC001
  span is now folded (expected, documented in `fba5242`).
