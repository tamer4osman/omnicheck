# M1 gate review - evidence walk (early, 2026-11-18)

This document records the M1 kill-switch gate review required by
`docs/M1_KILL_SWITCH.md` and scheduled by `docs/SPRINT-2.md` as the next
step after Sprint 2. It was executed on 2026-11-18, before the formal
gate date of 2026-12-31. Running the evidence walk early is deliberate
risk reduction: a criterion that fails in November can still be fixed
inside the window, while a failure discovered on 31 December could not.
The formal gate date does not move; the decision-log entries below are
dated accordingly. Every verdict below is attached to a run executed on
2026-11-18 (session recovered from the fourth workspace wipe via the
`omnicheck-sprint2.tar.gz` lifeline, `git fsck` clean, HEAD `fa13f87`).

## Session baseline (first, so the evidence has a floor)

- Repo restored from the Sprint 2 lifeline tarball (includes `.git` and
  the vendored grammars). `git fsck` clean; history intact through
  `fa13f87`; working tree clean before any review activity.
- Rebuilt and re-run **both gcc cells on the restored tree**: Release
  14/14 and ASan/UBSan 14/14. cmake reinstalled via pip (4.4.5).
- clang is absent again this session (no sudo): the gcc/clang
  cross-compiler re-verification remains CARRIED (see criterion 5).

## Criterion 1 - Fresh-clone reproducibility: PASS (after a real fix)

Definition: `scripts/vendor.sh` plus the CMake presets build the project
and pass the full test suite on a clean checkout with no local state.

The first true `git clone` test of this repository's history **failed to
configure**. Root cause: `add_subdirectory(third_party)` requires the
project-owned glue file `third_party/CMakeLists.txt`, but the blanket
`.gitignore` rule `third_party/` had kept it untracked since Sprint 0.
Every prior verification ran on the working tree (or a tarball), both of
which carried the glue file as an untracked passenger -- so the breakage
was invisible until a clone. This is precisely the "vendoring drift"
class the STOP list names, caught 43 days before the gate.

Fix (commit `24d1760`): `.gitignore` now ignores `third_party/*` except
the glue file, which is committed. Upstream vendored trees remain
untracked, fetched by `vendor.sh` at pinned refs.

Re-test from the fixed commit, executed in order:

1. `git clone` (local clean checkout, no local state, no `third_party/`
   content, glue file present).
2. `sh scripts/vendor.sh` -- network fetch; the four pins resolve and
   clone: tree-sitter `v0.27.2`, tree-sitter-javascript `v0.25.0`,
   tree-sitter-python `v0.25.0`, tree-sitter-typescript `v0.23.2`
   (verified with `git describe --tags` in each vendored tree).
3. `cmake --preset default` && build: clean, no warnings.
4. `ctest --preset default`: **14/14 pass**.
5. `cmake --preset sanitize` && build && `ctest --preset sanitize`:
   **14/14 pass** (ASan/UBSan/LSan, see criterion 4).

Verdict: **continue** (gap found, fixed, re-verified inside the window).

## Criterion 2 - No silent partial analysis: PASS

Definition: every TS/JS file in the corpus parses; any unparseable file
is a hard, named error; a clean report never means "nothing was
analyzed".

Live re-run on the committed broken fixtures
(`./build/default/oc check tests/fixtures_broken`):

- Exit code **2** (dominates the findings exit 1).
- stderr: `omnicheck: warning: 2 file(s) did not parse cleanly; findings
  cover only the parseable portions (see parse_errors)`.
- Report `parse_errors[]` names both files with counts and positions:
  `tests/fixtures_broken/js/broken.js` (2 error nodes, first at line 1
  col 1) and `tests/fixtures_broken/py/broken.py` (1 error node, first
  at line 1 col 12).
- Meta counters populated: `files_with_parse_errors: 2`,
  `parse_error_nodes: 3`, alongside `files_analyzed` and the counted
  skip fields (`files_skipped_dot_entries`,
  `files_skipped_ignored_dirs`) -- the OBS-1 fix is visible in every
  report.

Corpus-level confirmation: the live chalk run (criterion 3) reports
`files_analyzed: 20, files_with_parse_errors: 0` -- an empty
`parse_errors[]` coexists with a non-zero analyzed count, so "zero
errors" is meaningful, not vacuous. The mechanism ships with
regression tests (`test_parse_error.c`, fixtures under
`tests/fixtures_broken/`) and covers all four registered language packs
through the shared parser API.

Verdict: **continue**.

## Criterion 3 - Function extraction with byte ranges: PASS

Definition: all function nodes carry byte ranges; the extracted count
matches a hand-verified ground truth on the corpus.

Live evidence, 2026-11-18:

- `examples/` hand check: the committed corpus contains exactly four
  functions (2 JavaScript, 2 Python). `oc graph examples` extracts
  exactly 4 function nodes, each with `def_start`/`def_end` and
  parse-time complexity (add 7..9 cx1, processOrder 11..84 cx21 in JS;
  add 7..8, process_order 11..79 in Python). Python spans include the
  `def` line per the OBS-2 definition-extent contract.
- Pinned real-repo reproduction: chalk at `7211001`
  (`72110019e147683f60e13fa0ff5c14a0c27cd9fe`, re-fetched today after
  the older raw audit artifacts were lost to a workspace wipe) --
  **32/32 findings reproduce one-for-one** (rule, file basename, line)
  against the recorded Sprint 2 artifact
  `download/omnicheck-validation-artifacts/sprint2-recheck/chalk_gcc.json`;
  only the clone-root path prefix differs, which is correct: reports
  carry the analyzed path.
- Recorded ground truth (committed docs, raw artifacts lost to earlier
  wipes): Sprint 0 real-repo audit -- extraction ast-exact **711/711**
  on requests and **87/87** on howdoi via the oc graph; howdoi's 10
  extension `.ts` files parse and extract cleanly (Sprint 1). The
  chalk reproduction above re-establishes the chain from recorded
  numbers to a live run.

Verdict: **continue**.

## Criterion 4 - Sanitizer-clean: PASS

Definition: zero ASan/UBSan/LSan findings in the sanitizer job; R2's
trigger applies verbatim.

- `ctest --preset sanitize` (Debug + ASan + UBSan, LSan via
  `detect_leaks=1`): **14/14 pass**, executed today in BOTH the restored
  tree and the fresh clone -- two independent sanitizer-clean builds.
- The development history backs the trigger's honesty: ASan/UBSan caught
  four real bugs across Sprints 0-2 (CSR sentinel wraparound, path
  normalizer, unzeroed tsconfig cache, surface-hint count), all fixed
  with regression tests -- the cell finds real defects, it is not a
  rubber stamp.
- TSan remains deliberately out of the matrix (single-threaded kernel;
  documented in `M1_KILL_SWITCH.md`).

Verdict: **continue**.

## Criterion 5 - Determinism: PASS on current evidence; one carried re-verification

Definition: reports byte-identical across repeated runs and across
gcc/clang builds.

Live evidence today:

- **Across independent build trees**: `oc check examples` from the main
  tree build vs the fresh-clone build (separate build directories,
  same compiler) -- byte-identical (`cmp` clean), both exit 1 with the
  identical finding set (10 findings at the post-Sprint-2 tree: 2
  OC001, 4 OC002, 2 OC004, 2 OC006; the Sprint 0 cross-compiler record
  cited in the criterion above was six findings on the smaller
  Sprint 0 corpus).
- **Across repeated runs**: byte-identical reruns are a committed test
  contract (`test_sprint2` asserts the composed run is byte-identical;
  every sprint recheck re-ran and `cmp`'d reports).
- **Cross-compiler**: NOT re-verifiable this session (no clang binary,
  no sudo -- third session running). The Sprint 0 recorded evidence
  stands: byte-identical across gcc 14.2 and clang 19.1.7 on the
  examples corpus (2026-11-18). What is CARRIED is re-verification on
  the current, much-grown tree (TS/TSX packs, resolver v2, surface,
  coverage): due in the next clang-capable session, before
  2026-12-31. Sprint 2's report content is machine-formatted JSON with
  sorted findings; no mechanism introduced since Sprint 0 touches
  float formatting or locale, so the risk is low -- but the gate
  standard is runs, not reasoning, so the re-verification stays on the
  carried list.

Verdict: **continue** (Sprint 0 evidence satisfies the criterion's
letter today; carried re-verification before the formal gate).

## Supporting measurement (not a gate criterion)

Performance budget re-run live: 10,000-file deterministic corpus
(regenerated via `scripts/perf_gen.py`; the surviving `work/perf10k`
from an earlier session turned out truncated at 4,778 files and was
discarded) -- **8.13 s single-core** (`taskset -c 0`), exactly 10,000
OC002 + 200 OC003 findings as encoded. 9.0% of the 90 s sprint gate;
the 60 s M2 target (G3) is an M2 item. This is faster than Sprint 2's
8.82 s and Sprint 1's 12.05 s.

## Decision-log entries (mirrored in M1_KILL_SWITCH.md)

```
2026-11-18 | fresh-clone reproducibility | clone+vendor.sh+presets on clean checkout: configure FAILED (untracked third_party/CMakeLists.txt, commit 24d1760 fixes); re-test: 14/14 both presets | continue
2026-11-18 | no silent partial analysis | fixtures_broken: exit 2, 2 files named w/ error nodes+positions, stderr line, meta counters; live corpus: 20 analyzed / 0 parse errors | continue
2026-11-18 | function extraction | examples hand truth 4/4 with def spans; chalk@7211001 live: 32/32 findings reproduce recorded S2 artifact; recorded audits 711/711 + 87/87 | continue
2026-11-18 | sanitizer-clean | ASan+UBSan+LSan cell 14/14 in restored tree AND fresh clone; 4 real dev bugs caught historically | continue
2026-11-18 | determinism | repeat + independent-build-tree byte-identical (cmp); gcc-vs-clang re-verification CARRIED to next clang-capable session (Sprint 0 evidence stands) | continue
```

## Carried items into the formal gate (2026-12-31)

1. **gcc-vs-clang byte-stability re-verification on the current tree**
   (criterion 5) -- the only carried evidence item. Execute in the next
   clang-capable session.
2. Re-run this review's evidence commands on the gate date to confirm
   nothing drifted between 2026-11-18 and 2026-12-31 (the review
   script below is deterministic and takes minutes).

## Honest notes

- Raw artifacts from the Sprint 0 real-repo audit
  (`oc-realrepo-2026-11-18/`, `bug1-fix-recheck/`) no longer exist on
  disk -- lost to workspace wipes. The numbers survive in committed
  docs and in the session records; today's chalk reproduction
  re-establishes the recorded-to-live chain. Lesson applied: gate
  evidence should live in the repository, which this review does.
- The omnidecide-c re-audit remains impossible (private upstream, local
  clone wiped); its recorded numbers stand as documented in Sprint 2.
- The fourth workspace wipe in this project's history was again survived
  by the tarball lifeline pattern; `omnicheck-m1-gate-review.tar.gz`
  continues it.

## Review replay script

The evidence commands, in order (from the repo root, after
`cmake --preset default && cmake --build --preset default`):

```sh
ctest --preset default                                   # 14/14
ctest --preset sanitize                                  # 14/14 (after configure+build)
git clone . /tmp/oc-fresh && cd /tmp/oc-fresh
sh scripts/vendor.sh && cmake --preset default \
  && cmake --build --preset default && ctest --preset default
cmake --preset sanitize && cmake --build --preset sanitize \
  && ctest --preset sanitize                             # 14/14
cd /home/z/my-project/omnicheck
./build/default/oc check tests/fixtures_broken; echo $?  # 2, named files
./build/default/oc check examples > /tmp/a.json
cd /tmp/oc-fresh && ./build/default/oc check examples > /tmp/b.json
cmp /tmp/a.json /tmp/b.json                              # byte-identical
python3 scripts/perf_gen.py work/perf10k                 # 10k corpus
taskset -c 0 ./build/default/oc check work/perf10k       # ~8 s
```

## Post-review closure of the carried item (Sprint 5, 2026-11-18)

The one carried evidence item from this review -- the gcc-vs-clang
byte-stability re-verification, deferred session after session because
no clang and no sudo existed in the workspace -- was CLOSED during the
Sprint 5 session. clang 19.1.7 was obtained without sudo via
`apt-get download` + `dpkg -x` into a local prefix; all four build
cells (gcc/clang x Release/Sanitize) then ran 19/19 tests green, and
the cross-compiler outputs on examples (JSON + human), the compat /
mutation fixtures and the 10k corpus are `cmp` byte-identical, with the
10k corpus output also identical to the Sprint-4-era baseline. Measured
details live in `docs/evidence/cross_compiler_reverify.json`. The
formal gate date (2026-12-31) and its drift re-check obligation remain
unchanged; the harness re-runs near the gate date.
