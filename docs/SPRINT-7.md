# Sprint 7 - OmniDecide kernel vendoring (decision L8), real-provider
# proof in every build cell

Executed 2026-11-20 (ninth session). Predecessor: `docs/SPRINT-6.md`
(and the OmniDecide leverage session recorded in
`docs/OMNIDECIDE_LEVERAGE.md`). Scope: execute the user's L8 decision
("Bundle the OmniDecide kernel") under the memo's five hard
constraints, with the evidence process intact.

## Decision

`docs/OMNIDECIDE_LEVERAGE.md` L8 resolved as **vendor**. The OmniDecide-C
kernel (v2.0.0, Apache-2.0, upstream commit `561fdb8ae43a4610d3df92cde5ba0a0ac5f63393`,
archive sha256 `cd12263c9e6d9654b519dc7ac330a00d40affa58be6c013a2c70bf4fd59648fd`)
now ships IN-REPO, unmodified, under `third_party/omnidecide/`. The runtime
process boundary is retained unchanged (constraint 2 as amended): the
analyzer never links ranking logic; it only spawns a provider via
`$OC_OMNIDECIDE_BIN`, opt-in via `--triage`, fail-open on any provider
failure. Constraint 1 is untouched: no analyzer judgment logic was written
or approved here; the ranking math is upstream's audited kernel.

## Delivered

### 1. Vendored tree + provenance + integrity manifest

- `third_party/omnidecide/` (97 files, 2.0 MB): `odk-core/`, `odk-brain/`,
  `odk-spine/`, `include/`, `schema/`, `tests/` (incl. fixtures; the two
  golden-replay harnesses excluded with the 12 MB `golden/` corpus they
  consume), `examples/`, `docs/`, upstream top-level build/license files.
  Exclusions and the per-release re-audit policy (full golden replay from
  the pinned archive, outside this repo) are recorded in
  `third_party/omnidecide/PROVENANCE.md`.
- `VENDOR.sha256`: per-file manifest of the vendored tree;
  `scripts/vendor.sh` verifies it (committed-tree integrity gate, catches
  accidental edits of the foreign sources) in addition to fetching the
  grammar trees.
- `.gitignore`: `third_party/*` stays ignored for the fetched grammars;
  `!third_party/omnidecide` re-includes the committed kernel tree.

### 2. Provider shim lands as a first-class build target

- `tools/omnidecide/oc_odk_provider.c` (the pilot shim, byte-identical
  logic; only the header build-note was updated) is now the repo's
  protocol-v0 provider.
- Root `CMakeLists.txt` (option `OC_WITH_OMNIDECIDE`, default ON): adds
  the vendored tree as the upstream CMake subdirectory (`omnidecide`,
  its own options pinned OFF, `-ffp-contract=off` inherited from
  upstream -- the bit-identical-across-compilers flag) and links exactly
  ONE first-party binary against it: `oc-omnidecide-provider`. The
  provider passes a fixed `ODK_LANG_EN` config and never touches the
  adaptive brain: statelessness is the determinism precondition.

### 3. Real-kernel proof in every build cell (23 suites per cell now)

- `odk_smoke_test` (373 checks) + `odk_spine_test` (210 checks): the
  kernel's own self-check binaries, built from the vendored sources,
  ctest-registered with working directory at the vendored root (upstream
  fixture-path contract). **583/583 green in-repo**, default AND sanitize.
- `test_triage_real` (new, 56 checks): drives the REAL provider through
  both seams. Direct protocol: all items ranked, duplicate-text siblings
  resolve to distinct ids (the fba5242 defect class, now regression-locked
  against the real kernel), calibrated confidence in [0,1], response
  byte-identical on rerun. E2E `oc check`: verdicts identical with
  provider on/off/fail-open (same exit code, same fingerprint sequence --
  rank-only), provider id + model + honest `ranked_count` in the report,
  the guardrail string echoed, the double opt-in asserted (provider env
  without `--triage` is NOT triage), and the ranked report byte-identical
  on rerun.
- Hostile-provider fail-open cases stay the mock suite's jurisdiction
  (`tests/test_triage`, 58 checks, unchanged).
- Measured sanitizer hygiene: the vendored kernel passes ASan+UBSan+LSan
  UNMODIFIED in the sanitize cell (no `detect_leaks=0` carve-out needed).
  Matrix after landing: **23/23 default, 23/23 sanitize** (was 20/20).

### 4. Equivalence evidence (pre-vendor pilots reproduced bit-for-bit)

- Raw provider responses: landed provider vs the pilot binary
  (`work_src/oc_odk_provider`, committed-behavior reference) on the
  recorded pilot requests -- examples (10 items, 591 B), eslint (2,398
  items, 1.3 MB request), perf10k (30,200 items, 9.1 MB request) -- all
  BYTE-IDENTICAL; the last two also match the recorded pilot response
  artifacts (`eslint_full_out.json`, `perf10k_full_out.json`).
- Full stack: `oc check` on the pinned eslint corpus (m2 pin) with
  `--triage` produced byte-identical ranked reports from the landed and
  pilot providers (modulo the env-supplied provider path): 2,380
  findings, 928 annotated (39%), 1.13 s scan+triage inside the 3 s
  budget -- the fba5242 re-measurement reproduced exactly.

## Deliberately unchanged

- The runtime seam (`include/oc/triage.h` contract, budget + process-group
  kill + fail-open from commit 193a5f2), the mock provider, the unranked
  report bytes (sidecar stays opt-in; no auto-enable), the npm shim
  package (provider binary is a build artifact, not shipped -- recorded
  as descope), and the vendored sources themselves ("do not edit" rule,
  mechanically enforced by VENDOR.sha256).

## NEXT

- Gate-day chain rerun at the new HEAD -- DONE (fresh bundle
  `docs/evidence/gate_day/M5_2026-11-20/`, all six steps PASS): matrix
  4 cells 23/23 each WITH the vendored-kernel suites in the loop,
  harnesses m2+m3+m4+m5 all PASS, determinism audit 1 distinct digest,
  docs audit 100 percent, package legs PASS (bundle, npm pack, shim,
  real npm install smoke), tag steps 7-9 left to the human per ritual.
  Incident recorded: the interrupted harnesses invocation left a stale
  step-level PASS (recorded after m4, killed during m5) -- the step was
  cleared from the bundle and re-run whole; m1-replay remains an M1-only
  leg, so a future runner improvement could record per-leg completion
  for the harnesses step.
- L-series follow-ups remain as filed in `docs/OMNIDECIDE_LEVERAGE.md`
  (L2 evidence channel, L3 calibration loop gated on outcome data,
  L5 budget profiles, L6 pin+replay audit trail); the brain stays opt-in
  and OFF in every shipped path.
