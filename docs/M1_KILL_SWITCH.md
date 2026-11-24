# M1 kill switch - Walking Skeleton (due 31 December 2026)

Committed 2026-11-18, before any M1-scope work, so the criteria cannot drift
retroactively. This file implements the Layer-3 condition of the empirical
reliability verdict (OmniCheck_Reliability_Verdict, 2026-11-18): fund the
Walking Skeleton and define *now* which measured results at M1 continue the
project and which stop it.

## M1 scope (plan Table 9, gate M1)

Kernel API parses a sample TS/JS repository through tree-sitter; all function
nodes extracted with byte ranges; sanitizer-clean build on CI; demo committed
and reproducible from a fresh clone.

## CONTINUE - all five must hold at the gate review

Measured on the pinned corpus (the committed `examples/` tree plus the
reference TS/JS repository pinned by WBS 1.x):

1. **Fresh-clone reproducibility** - `scripts/vendor.sh` plus the CMake
   presets build the project and pass the full test suite on a clean checkout
   with no local state.
2. **Parse coverage with no silent partial analysis** - every TS/JS file in
   the corpus parses; any unparseable file is a hard, named error carrying
   file and reason. A clean report must never mean "nothing was analyzed".
3. **Function extraction** - all function nodes carry byte ranges, and the
   extracted count matches a hand-verified ground truth on the corpus.
4. **Sanitizer-clean** - zero ASan/UBSan/LSan findings in the sanitizer job.
   Plan risk R2's trigger applies verbatim: any sanitizer finding, anywhere,
   at any time.
5. **Determinism** - reports byte-identical across repeated runs *and*
   across gcc/clang builds. (Proven on the examples corpus on 2026-11-18:
   six findings, byte-identical across gcc 14.2 and clang 19.1.7.)

## STOP - the kill switch fires if any of these hold at the gate

- A sanitizer finding that cannot be fixed inside the window.
- Byte-instability across runs or compilers that resists a fix by
  27 December 2026.
- Parse failures that cannot be surfaced as hard, named errors (i.e. any
  silent partial analysis survives to the gate).
- Fresh-clone reproducibility broken by vendoring drift.

## On STOP

Descope-not-slip applies, per plan Section 5.1: the failing element is cut
from scope by explicit decision, or the program stops. Dates do not silently
move. The decision is recorded in the log below with the measured numbers
attached - no narrative verdicts without runs.

## Status notes

- 2026-11-18 (early gate review, see docs/M1_GATE_REVIEW.md): the
  five-criteria evidence walk was executed early, per Sprint 2's
  recorded next step. Result: all five verdicts CONTINUE on measured
  evidence (entries in the decision log below). The walk caught and
  fixed one real criterion-1 breakage -- the project-owned
  `third_party/CMakeLists.txt` build glue was untracked since Sprint 0
  (blanket ignore rule), so a true fresh clone could not configure;
  commit 24d1760 tracks it and the full clone->vendor.sh->build->test
  flow now passes 14/14 in both presets. Carried into the formal gate
  date: gcc-vs-clang byte-stability re-verification on the current
  tree (clang absent for a third session; Sprint 0 evidence stands),
  plus a repeat of the evidence commands on 2026-12-31 to confirm no
  drift.
- 2026-11-18 (Sprint 2, see docs/SPRINT-2.md): both OPEN observations
  from the Sprint 0 audit are closed. OBS-2 (cross-language OC001 span
  semantics) is fixed by the definition-extent contract -- verified on
  the requests corpus where the two threshold-boundary functions are
  now caught exactly as the unified semantics requires (11 -> 13
  findings, both shifts at the boundary). OBS-1 (silent discovery
  skips) is fixed by counted meta fields
  (files_skipped_dot_entries / files_skipped_ignored_dirs), regression-
  tested. CRAP-ms composition (coverage ingestion for Istanbul +
  coverage.py, OC007) has a mechanism behind criterion #1's flagship
  metric; tsconfig alias resolution (resolver v2) and the module
  surface passes (OC005/OC006) round out WBS 3.x. Performance
  re-verified: 10,000-file corpus in 8.82 s single-core (9.8% of the
  90 s gate). STILL OPEN for the gate: gcc-vs-clang byte-stability
  re-verification (clang unavailable for a second session;
  sprint-0 cross-compiler evidence stands).
- 2026-11-18 (Sprint 1, see docs/SPRINT-1.md): TS/TSX support landed, so
  criterion #2's "every TS/JS file" now has teeth beyond JavaScript --
  vendored tree-sitter-typescript v0.23.2, packs registered declaratively;
  the BUG-1 fix carries over to the new grammars (fixtures_broken covers
  the surfacing contract). Criterion #3 was pre-verified on real TS: all
  10 .ts files of howdoi's VS Code extension parse and extract cleanly.
  Performance pre-verified: 10,000-file synthetic corpus in 12.05 s
  single-core (13% of the 90 s sprint gate; G3's 60 s target is an M2
  item). Import edges + OC003 cycle pass landed; the requests corpus
  yielded one REAL 11-file import cycle (structurally true,
  intent-blind). OPEN for the gate: gcc-vs-clang byte-stability must be
  re-verified (clang missing in this session's environment, gcc-only
  matrix; sprint-0 cross-compiler evidence stands, sprint-1 cross-compiler
  evidence pending).
- TSan is deliberately not yet in the CI matrix: the kernel is
  single-threaded today, so there is nothing for TSan to check. TSan joins
  the matrix when concurrency work begins (WBS 2.x). This note keeps the
  WBS 1.2 wording honest rather than aspirational.
- LSan runs as part of every ASan cell (`detect_leaks=1`).

## Decision log

Early gate review executed 2026-11-18 (see docs/M1_GATE_REVIEW.md for
the full evidence walk). Entries take the form:
`date | criterion | measured result | continue / descope / stop`.

```
2026-11-18 | fresh-clone reproducibility | clone+vendor.sh+presets on clean checkout: configure FAILED (untracked third_party/CMakeLists.txt, commit 24d1760 fixes); re-test: 14/14 both presets | continue
2026-11-18 | no silent partial analysis | fixtures_broken: exit 2, 2 files named w/ error nodes+positions, stderr line, meta counters; live corpus: 20 analyzed / 0 parse errors | continue
2026-11-18 | function extraction | examples hand truth 4/4 with def spans; chalk@7211001 live: 32/32 findings reproduce recorded S2 artifact; recorded audits 711/711 + 87/87 | continue
2026-11-18 | sanitizer-clean | ASan+UBSan+LSan cell 14/14 in restored tree AND fresh clone; 4 real dev bugs caught historically | continue
2026-11-18 | determinism | repeat + independent-build-tree byte-identical (cmp); gcc-vs-clang re-verification CARRIED to next clang-capable session (Sprint 0 evidence stands) | continue
```
