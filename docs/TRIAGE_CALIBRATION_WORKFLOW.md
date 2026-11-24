# Triage confidence calibration workflow (memo L3, with L4/L7 gates)

Status: DESIGNED WORKFLOW, 2026-11-20. **No outcome data exists yet, so
nothing in this document is a measured claim, and it must stay that way
until real data arrives** (docs/OMNIDECIDE_LEVERAGE.md L3). This is the
feedback edge the memo calls "the missing edge": today
`triage_confidence` is a within-request calibrated probability --
comparable inside a scan, not across scans -- and nothing checks that it
MEANS anything. This document fixes, in advance, the ritual that will
check it, so the check cannot be improvised (or skipped) later.

## 1. What exists already (landed, measured)

- The provider emits a per-finding calibrated probability as
  `triage_confidence` next to the provider id, rank-only, prose-only
  inputs, fail-open (`src/triage.c`; guardrails echoed in every report).
- The vendored kernel's spine ships the calibration machinery unmodified:
  `odk_spine_observation_ingest` (append-only log of Observation records,
  schema-validated against `third_party/omnidecide/schema/omnidecide.schema`,
  which is the single source of truth for the record shape) and
  `odk_spine_calibrate` (predicted confidence vs observed outcome rate,
  per primitive/scheme/tier, binned into ten confidence buckets; two runs
  over the same log are byte-identical -- SPINE-API.md F4).
- The audit surface landed in SPRINT-8 (`tools/omnidecide/oc_odk_explain.c`,
  `OC_OMNIDECIDE_ARTIFACTS_DIR`) records per-run what WAS claimed at scan
  time, which is what the outcome labels will be checked against.

## 2. The outcome log (application-side, JSONL)

One line per triaged finding, appended by whoever touches the finding.
The fields are OmniCheck's own; the ingest step (section 3) translates
them into the kernel's Observation record.

```json
{"fingerprint":"e672e91a81f728a5","file":"lib/binding_web/src/query.ts",
 "rule":"OC005","provider":"oc-odk-provider","model":"omnidecide-c/2.0.0",
 "rank":0,"confidence":0.2544,"outcome":"addressed","decided_at":1763610000000,
 "addressed_within_days":2,"note":""}
```

- `outcome` is exactly one of `addressed` or `ignored` -- a binary label,
  no hedging; `unknown` entries are simply not appended (an absent line
  is an absent label, never a guessed one).
- `addressed_within_days` is the honesty field: it records HOW the label
  was derived, so a monthly review can spot labels that were recorded
  long after the fact.
- `confidence` and `rank` are copied verbatim from the scan report -- the
  predicted value being validated, not re-derived.

## 3. The monthly ritual

1. Collect the outcome lines accumulated since the last run (the JSONL
   file lives next to the triage artifacts; it is documentation, like
   them, and is never an input to any scan).
2. Translate each line into one kernel Observation record
   (`kind:"outcome"`, `subject_ref:<fingerprint>`, `confidence:<as
   predicted>`, `outcome:"success"` for addressed / `"failure"` for
   ignored, `t:<decided_at>`) and feed the log with
   `odk_spine_observation_ingest`.
3. Run `odk_spine_calibrate` over the log. The report is deterministic
   (two runs over the same log are byte-identical); file it with the
   month's evidence.
4. Decide, by the pre-committed rule below, whether the confidence
   channel may keep being exposed in dashboards, and whether L4 may
   un-gate.

**Pre-committed decision rule** (fixed now, before any data exists, so
the threshold cannot be tuned after seeing the numbers): the confidence
channel is trustworthy for dashboard exposure only if the calibration
report's WORST per-bin error is <= 0.10 over >= 100 outcomes. Below that
bar: keep exposing confidence (it is honest -- it says what the kernel
believed), but dashboards must label it "uncalibrated" and L4 stays
locked.

## 4. L4 -- outcome-conditioned ranking via the brain (GATED, unchanged)

The brain's documented brakes (LEARNING.md: no effect on a single
outcome, bonus needs two visits, shrinks by `visits/(visits+4)`,
snapshots load fail-loudly, results without a brain are bit-identical)
make it a nudge, not a decider. Its adoption gate from the memo stands
unchanged: only after section 3 produces a calibrated outcome feed that
PASSES the section 3 rule; only with a small weight (0.2-0.5); the
snapshot kept next to the artifacts (L2) and audited; the default build
stays brain-less. Ranking never changes the report's findings, order or
severities, which is what makes even a nudged rank order acceptable under
constraint 1.

## 5. L7 -- model files (research track, unchanged)

`odk_kernel_load_model` replaces nine hand-set constants; MODEL.md's own
warning is the gate: a model can change which option wins. Until a model
is validated on held-out labeled data (which itself requires the L3
feed), no model ships and the provider keeps no `OC_OMNIDECIDE_MODEL`
passthrough. The vendored kernel's model machinery remains available for
the research track only.

## 6. What would falsify this workflow

- The calibration report shows worst-bin error > 0.10 with >= 100
  outcomes: the confidence channel does not mean what the report implies;
  dashboards drop it and the triage annotation stays advisory-only.
- Outcome labels disagree systematically with the artifacts' recorded
  claims (L2): the prediction pipeline, not the kernel, is broken.
- Either way the verdict logic is untouched: this workflow validates and
  gates EXPOSURE, it never rewrites a finding.
