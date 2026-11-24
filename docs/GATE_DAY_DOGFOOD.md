# Gate-day dogfood (2026-11-19): the ritual ran five times, gate by gate

The dress rehearsal (2026-11-18, `RITUAL_DRESS_REHEARSAL.md`) executed
the full pre-condition chain ONCE, on one tree. This session executed
each gate's OWN chain, gate by gate, through the mechanized runner:

```
python3 scripts/acceptance/gate_day_runner.py --gate M<N> --step tree-clean|matrix|harnesses|audit|docs|package
```

One evidence bundle per gate:
`docs/evidence/gate_day/<GATE>_2026-11-19/bundle.json`. Harness
obligations follow the plan cadence (`HARNESS_DUE`): M1 has no harness,
M2 is the first harness-bearing gate, M5 re-runs ALL FOUR harnesses
(m2 + m3 + m4 + m5). Formal gate dates are unchanged; this was an early
rehearsal per the project's rehearse-early convention.

## Headline: the ritual caught four real defects in its own machinery

Each was fixed the same day, with the fix committed BEFORE the affected
gate was re-proven green. None was narrated away.

1. **Intermittent LSan leak in `test_sprint3` (M2 dogfood).** The
   sanitize cell failed 3/10 loop runs reporting a 131,120-byte leak.
   Ten-run reproduction classified it as a REAL defect, not sandbox
   noise: `main()` never destroyed the report arena (`oc_arena_destroy`
   present in every sibling test binary, absent in this one). LSan's
   stale-stack reachability is why it looked intermittent -- a fresh
   process can leave the lost pointer reachable on the stack, masking
   the leak. Fix: destroy the arena before `TEST_SUMMARY` (`90b1dc3`);
   re-tested 0/20 failures.

2. **Runner crash on the first harness-bearing gate (M2 dogfood).**
   `ensure_pin(*PINS["m2"])` unpacked a 3-tuple into a 2-argument
   function. The M1 dogfood could never exercise the harness path (M1
   has no harness), so the bug shipped in the runner's first committed
   version and the ritual caught it on its first real use. Fixed at
   both call sites, m2 and m3 (`7da3ff7`).

3. **Determinism audit "2 distinct digests" (M4 dogfood).** NOT a
   determinism break: the sandbox cleaner deleted the audit corpus
   under `/tmp` MID-RUN; `oc check` on the missing path exits 2 with an
   EMPTY report, and the empty string's sha256 was counted as a second
   digest while the three real cells were byte-identical (14.9 MB).
   Reproduced exactly with `oc check /tmp/does-not-exist`. Fix: the
   audit corpus moved to `work/` (gitignored, survives recycles) and an
   empty scan report now FAILS LOUD instead of feeding sha256
   (`4d05564`). Re-run: 1 distinct digest, PASS.

4. **Raw TABs leaked into mutation-report JSON (carried-item session,
   M4/M5 scope).** The Sprint-5 budget re-measure on the PINNED
   reference repos caught it: the hand-rolled JSON emitter escaped only
   quote/backslash/newline, and eslint's indentation style put 427,240
   raw 0x09 bytes into one report's `original`/`replacement` strings --
   every strict JSON parser rejected the report. Fix: shared
   `mut_json_string()` (`\n \t \r` shorthands, `\uXXXX` for all other
   control chars) used for every string field and the cache map, plus a
   regression test (`test_e2e_mutate_escaping`). M4 was then RE-PROVEN
   green end-to-end on the fixed tree (`ba67355`).

Also fixed: the `tree-clean` step now exempts the ritual's OWN gate dir
-- writing the bundle makes the tree dirty by design (`6ebd659`).

## Carried item CLOSED during the same session

**Mutation budget re-measure on the pinned reference repos** (carried
from Sprint 5, `a643c3c`,
`docs/evidence/mutation_budget_remeasure.json`, `all_pass: true`):

- demo fixture with the REAL suite (`python3 test_calc.py`): 0.62 s for
  10 mutants, 9 killed, 1 designed survivor (arg-swap) -- the Sprint-5
  claim re-asserted against the 600 s CI budget.
- Engine cost on the pins with a MINIMAL test command (isolates engine
  cost; real-suite execution on huge repos stays descoped per Sprint 5
  and the budget math is recorded in the verdict): black @ pin
  0.188 s/mutant (9,565 mutants enumerated), eslint @ pin 0.330
  s/mutant (25,989 mutants enumerated). A real suite costing T seconds
  affords `floor(600/T) - 1` mutants per CI run inside the 600 s
  target.

## Result: five gate chains, green end-to-end

| Gate | tree-clean | matrix | harnesses | audit | docs | package |
| --- | --- | --- | --- | --- | --- | --- |
| M1 | PASS | PASS | n/a (no harness due) | PASS | PASS | PASS |
| M2 | PASS | PASS | m2 PASS | PASS | PASS | PASS |
| M3 | PASS | PASS | m3 PASS | PASS | PASS | PASS |
| M4 | PASS | PASS | m4 PASS | PASS | PASS | PASS |
| M5 | PASS | PASS | m2+m3+m4+m5 PASS | PASS | PASS | PASS |

Matrix cells rebuilt FROM SCRATCH in every chain: gcc Release, gcc
ASan/UBSan/LSan, clang 19.1.7 Release, clang ASan/UBSan/LSan -- 20/20
tests per cell (suite count after Sprint 6's `test_fuzz`). Audit:
single-core `taskset -c 0`, 1 distinct sha256 across all cells and
reruns, release worst wall 13.12 s vs the 90 s gate / 60 s M2 target.
Docs completeness: 24/24 derived items. Package: tarball + sha256
manifest, byte-identical smoke. The `determinism_audit.json` committed
with the M5 bundle is the fresh audit from the final chain.

## M5 tag steps are LEFT TO THE HUMAN (by ritual design)

The runner mechanizes steps 1-6 of `docs/V1_TAG_RITUAL.md` and
explicitly stops there (`bundle.harnesses.tag_left_to_human: true`).
Steps 7-9 -- the `release: v1.0.0` version-bump commit, the package.sh
re-run producing `omnicheck-1.0.0-linux-<arch>.tar.gz`, and the
annotated `git tag -a v1.0.0` -- are the human's, on the formal GA gate
date 2027-12-31, after the M5 harness re-run. No v1.0.0 tag was cut and
none is due before then.

## Honest drift notes

- `/tmp` is UNRELIABLE in this sandbox: two incidents in one session
  (the clang prefix and the audit corpus were wiped mid-run by the
  recycle/cleaner). All runtime artifacts now live under `work/`
  (gitignored); the recorded no-sudo clang recipe
  (`apt-get download` + `dpkg -x`, then `lib/clang/19/lib/linux` ->
  `x86_64-pc-linux-gnu` copy) reproduces clang 19.1.7 exactly whenever
  a recycle removes it.
- Wall times in the bundles are fresh measurements, not history: the
  M5 audit's release worst (13.12 s) differs from the rehearsal's
  (10.88 s) purely by machine load; the DIGEST is the invariant and it
  did not move.
- The M4 chain was run twice by design: once before the mutation-JSON
  fix was known (bundle superseded), once after (`ba67355`) -- the
  committed bundle is the post-fix one; git history keeps the earlier
  green bundle for the pre-fix tree.
