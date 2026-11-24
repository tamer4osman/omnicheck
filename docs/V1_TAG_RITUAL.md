# v1.0 tag ritual (M5 GA, formal gate date 2027-12-31)

The `v1.0.0` tag is cut ONLY after every item below is checked on the
gate date itself. The rehearsal (2026-11-19, `scripts/acceptance/
m5_gate.py` + `docs/evidence/m5_gate_verdict.json`) proves the pipeline;
the ritual below is what turns it into a release. Everything here is
deliberately mechanical: no step requires judgement, so nothing can be
narrated into passing.

## Mechanized execution (2026-11-18, post-scope)

Pre-conditions 1 and 3-6 (and the harness re-runs of item 2, for the
gate being executed) are executed by the committed runner:

    python3 scripts/acceptance/gate_day_runner.py --gate M1   # or M2..M5

It runs the chain step by step, stops at the first red item, records
every step incrementally into
`docs/evidence/gate_day/<gate>_<date>/bundle.json`, and resumes per
step (matrix: per cell) if interrupted -- a gate-day run never
restarts from zero. The runner's own bundle is its one exempted
untracked path (it is written BY the ritual); everything else dirty is
red. The runner never cuts the tag: at M5 it prints the steps 7-9
commands for a human. First full dogfood: gate M1, 2026-11-18, all
steps green (`docs/evidence/gate_day/M1_2026-11-18/bundle.json`);
the dogfood itself caught three runner bugs and the stop path is
proven by a dirty-tree run.

## Pre-conditions (gate day, in this order)

1. `git status --short` is EMPTY on `main` (no local scratch, no
   untracked evidence).
2. Re-run the carried harnesses near their gate dates and attach fresh
   verdicts (each prints its own PASS/FAIL):
   - M1 drift re-check (`docs/M1_GATE_REVIEW.md` replay block) -- due
     2026-12-31;
   - `scripts/acceptance/m2_gate.py` -- due 2027-02-28;
   - `scripts/acceptance/m3_gate.py` -- due 2027-05-31;
   - `scripts/acceptance/m4_gate.py` -- due 2027-09-30;
   - `scripts/acceptance/m5_gate.py` (NO `--skip-audit`) -- due
     2027-12-31.
3. Full 4-cell matrix from scratch: configure + build + ctest in
   `default`, `sanitize`, `clang-release`, `clang-sanitize` -- 20/20
   each, including `test_fuzz`.
4. `scripts/acceptance/determinism_audit.py`: one report digest across
   all cells (the GA criterion, verbatim).
5. `scripts/audit_docs.py`: 100 percent.
6. `scripts/package.sh`: tarball + sha256 manifest + byte-identical
   smoke; npm shim verified through `bin/native/oc` AND `OC_BIN`.

## Tag steps

7. Bump `OC_VERSION_STRING` in `include/oc/common.h` and the three
   version fields in `npm/package.json` to `1.0.0` in ONE commit titled
   `release: v1.0.0`.
8. Re-run `scripts/package.sh`; the artifact is now
   `omnicheck-1.0.0-linux-<arch>.tar.gz` (+ `.sha256`).
9. `git tag -a v1.0.0 -m "OmniCheck 1.0.0 (M5 GA)"` on that commit.
   Annotated tag, never lightweight.
10. Attach the tarball, the sha256 manifest and the five gate verdict
    JSONs to the release. The verdicts are the release notes' evidence
    section; the notes themselves carry NO claims without a verdict
    reference.

## Hard rules

- Any red item above stops the ritual. There is no "tag anyway".
- No v0.x maintainence branch is created; descope-not-slip applies to
  the release content, recorded in the sprint docs BEFORE the tag.
- AI involvement rules stay in force on gate day: no analyzer verdict
  logic may be written or approved by AI, and no verdict is claimed
  without a measured run behind it.
