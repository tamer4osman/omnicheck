# Sprint 3 (M2 Alpha completion) - OC008 duplicate-code, human report, M2 acceptance harness

Committed as one sprint on 2026-11-18. Everything below was verified by
direct execution; see "Session context" and "Scope correction" for the
honesty notes.

## Session context (honesty first)

- NO workspace wipe this session (first time in four sprints) -- the
  Sprint 3 work started from the live tree at `79105e9`. One incident:
  the sandbox recycle flipped the executable bit on all 130 tracked
  files (mode-only changes, zero content changes); restored via
  `git checkout -- .` and verified clean before any work.
- cmake reinstalled via pip (4.4.5) after the recycle; gcc 14.2
  present; **clang still absent (no sudo)** -- the gcc/clang
  byte-stability re-verification stays CARRIED (third session;
  Sprint 0 evidence stands; see docs/M1_GATE_REVIEW.md).

## Scope correction (descope-not-slip applied to my own shorthand)

The Sprint 2 record and the session notes said "next: M2 Alpha (WBS
4.x per plan Table 9)". Reading the committed plan (Table 8/9) shows
that shorthand was WRONG, and it is corrected here rather than silently
followed: M2 Alpha (2027-02-28) is the gate of **phase P1 (WBS 2.x)**,
and exactly one P1 item was still missing -- **2.4 "Duplication
fingerprint analysis"**. WBS 4.x is phase P3 (M4 RC scope). Sprint 3
therefore completes P1 and executes the M2 acceptance criteria; WBS 4.x
items stay descoped to their own phase (see "Explicit descopes").

## M2 gate criteria (plan Table 9) and the reference repository

Gate M2 = "Full pipeline on the reference TS/JS repository with three
analyses enabled; fingerprints stable across ten consecutive runs; JSON
and human output; reference-repo wall time within the 60-second target"
(G3: 100k LOC reference repository, single modern CPU core).

Reference repository PINNED: **eslint**
`21026992e3520869b31ca4500905665942bb2cf0`, scope `lib/` -- measured
**394 files / 107,039 LOC TS/JS** (in the 100k LOC range G3 names;
candidates lodash/date-fns/webpack were measured and out of range or
misfits). The pin and the harness are committed
(`scripts/acceptance/m2_gate.py`), so the gate is replayable.

## Delivered

1. **OC008 duplicate-code (WBS 2.4).** New `src/dup.c` +
   `include/oc/dup.h` + `src/passes/duplicate_code.c`, registered in
   all four packs. Language-agnostic normalized-line duplication:
   seed windows of `OC008.min_block` (default 10) lines bucketed by a
   64-bit rolling hash, every candidate VERIFIED by direct text
   comparison (no hash trust anywhere), lockstep extension (forward and
   backward, all members against the first, relative offsets), identical
   converged spans collapsed, groups = text-verified runs of >= 2
   locations. One finding per location (severity warn); message lists
   up to 8 locations sorted by (path, start), "+N more" beyond.
   Honesty counters in `meta.dup`: `groups`,
   `groups_truncated` (cap `OC008.max_groups`, default 256),
   `files_skipped_oversize` (cap `OC008.max_file_bytes`, default
   1,000,000). Fingerprints fold the location in
   (`dup:<text-hash>:<start>`) so findings never share a fingerprint.
   Contract documented in docs/ARCHITECTURE.md.
2. **Human report (`oc check --format human`, WBS 2.6 completion).**
   Deterministic grouped text report: rule sections sorted by rule id,
   findings in the global (file, line, col, rule) order within each
   section, severity counts, dup-scan line, triage line, parse errors
   as a NAMED section (exit codes unchanged; broken files can never
   look clean in either format). Unknown `--format` values are a hard
   usage error (exit 2). Default remains JSON, byte-compatible.
3. **M2 acceptance harness (`scripts/acceptance/m2_gate.py`).**
   Executes the four gate criteria against the pinned reference repo
   and emits a verdict JSON. Criteria: >= 3 distinct rules with
   analyzed > 0 and zero parse errors; N consecutive runs byte-identical
   (fingerprint stability); human output non-empty, self-describing,
   deterministic; single-core wall time under the 60 s budget
   (taskset when available).

## Test suite: 14 -> 15 tests, all green in both available cells

`tests/test_sprint3.c` (57 checks): OC008 baseline on the new
`fixtures_sprint3/dupes` tree (cross-file 12-line block WITH indentation
variance incl. a tab line; intra-file 10-line block; 9-line block below
threshold stays silent), exact spans (a.js/b.js 6..17, self.js 6..15
and 20..29), shared-message contract, meta.dup counters exact,
`OC008.min_block=13` silences OC008 entirely (exit 0 -- only info
findings remain, threshold contract), `OC008.min_block=9` exposes the
third group (6 findings / 3 groups), byte-identical rerun, human report
content + byte-identical rerun, explicit `--format json` byte-identical
to the default run, unknown format exit 2, human parse-errors section on
fixtures_broken (exit 2 preserved), examples control stays OC008-silent.

15/15 green in gcc Release AND gcc ASan/UBSan. One dev bug caught by
the fixture BEFORE landing: the identical-span collapse (seeds inside
one maximal block converge to the same location) was dropped in the
rewrite -- the a/b block reported 6 locations instead of 2; the fixture
assertions caught it, the collapse pass was restored (5a in dup.c).

## Performance budget (sprint gate 90 s single-core)

10k-file corpus (same generator): **9.76 s** with OC008 live
(10.8% of the gate; Sprint 2 measured 8.13 s without OC008 -- the pass
costs ~1.6 s for ~0.7M windows + a 20k-finding emission), byte-identical
rerun, exactly 10,000 OC002 + 200 OC003 + 20,000 OC008 across 51 dup
groups (the generator's repetitive blocks are REAL duplication --
structurally true, intent-blind).

## Real-repo results

- **chalk @ 7211001** (re-fetched after the /tmp wipe): legacy counts
  EXACT vs the recorded Sprint 2 artifact (OC001 2, OC002 4, OC004 1,
  OC005 9, OC006 16); all 32 findings match one-for-one (rule, file,
  line); OC008 = 0 groups (chalk has no duplicated 10-line normalized
  blocks); parse errors 0.
- **eslint @ 21026992, scope lib/ (107,039 LOC)**: 394 analyzed,
  0 parse errors, 2,361 findings -- OC001 399, OC002 12, OC004 226,
  OC006 15, **OC008 1,709 across 357 groups (256 reported, 101 honestly
  truncated at the default cap)**. eslint's rule implementations share
  large boilerplate blocks; the findings are structurally true and
  exactly the class the OmniDecide layer must rank.
- **M2 gate verdict (gcc Release, taskset -c 0): PASS on all four
  criteria** -- 5 distinct rules, 10 consecutive runs byte-identical,
  JSON + human deterministic, wall time **0.60 s best / 0.61 s median**
  (1% of the 60 s budget). The sanitizer build also passes all four
  (13.87 s median -- sanitizer overhead, still 4x under budget).
- **Cross-build-type determinism on the reference corpus**: JSON and
  human reports byte-identical (cmp) between the gcc Release build and
  the gcc ASan/UBSan build, 2,361 findings / 947,169 human bytes.

## Explicit descopes (descope-not-slip, recorded before they hurt)

- **WBS 4.x (P3, M4 RC scope): 4.1 error-handling integrity, 4.2
  dependency health + secret scanning, 4.5 mutation engine, 4.6
  breaking-change gate.** The committed CRAP-ms v0 (Sprint 2) already
  covers the 4.3/4.4/4.7 substance the verdict report weakened to
  "composition increment"; the mutation-validated form stays behind
  4.5 by plan design.
- **WBS 3.x remainders (P2, M3 Beta scope): 3.3 LSP server, 3.4 MCP
  server, 3.6 GitHub Action/CI gate templates.** 3.1/3.2/3.5 were
  delivered by Sprints 1-2 (packs + sidecar protocol seam).
- **Resolver v3 (Python absolute intra-repo imports)** and
  **package.json exports / node_modules resolution**: unchanged from
  Sprint 2's descopes; now recorded against M3.
- **Istanbul fnMap per-function counters**: unchanged (line anchors
  remain the cross-language-uniform approximation).
- **clang CI cell + gcc-vs-clang byte-stability**: carried a THIRD
  session (no clang, no sudo). Sprint 0 cross-compiler evidence stands;
  the M1 gate review records it as the one carried evidence item.

## Documentation

- docs/ARCHITECTURE.md: duplication contract (new section) + dup module
  row.
- README.md: `--format`, OC008 + config keys, M2 acceptance section
  with the reference pin.
- docs/M1_GATE_REVIEW.md: corrected the criterion-5 live-run finding
  count (six -> the current ten-finding set; the cmp evidence was and
  is unaffected).

## M2 gate status (2027-02-28)

All four acceptance criteria PASS with committed, replayable evidence
(the harness is in the repo; the reference pin is recorded in README
and SPRINT-3). Remaining before the formal M2 date: the carried
gcc-vs-clang re-verification, and a harness re-run near the gate date
to confirm no drift. Next sprint (M3 Beta scope, WBS 3.3/3.4/3.6 +
resolver v3): LSP server, MCP server, CI gate templates.

## Addendum (2026-11-18, evidence backfill): machine-readable M2 verdict committed

The "M2 gate status" section above recorded the PASS verdict inline, but
the machine-readable artifact was never committed -- the same
raw-artifact-loss class as the Sprint 0 honest note. Both verdict JSONs
are now in the repo (`docs/evidence/m2_gate_verdict.json`,
`docs/evidence/m2_gate_verdict_sanitize.json`), produced by re-running
`scripts/acceptance/m2_gate.py` against the re-fetched pinned reference
(`eslint @ 21026992e3520869b31ca4500905665942bb2cf0`, codeload tarball at
the full SHA; the pin reproduces exactly: 394 files / 107,039 LOC in
`lib/`).

Measured today, `taskset -c 0`:

- **gcc Release (canonical, 10 runs): PASS on all four criteria** --
  6 distinct rules, 394 analyzed, 0 parse errors; 10 runs -> 1 distinct
  report hash; human report 1,960,606 bytes, deterministic; 0.66 s best
  / 0.66 s median vs the 60 s budget (1.1 percent). OC008: 256 groups
  reported, 101 honestly truncated -- identical to the Sprint 3 record.
- **Sanitize cell (supplementary, 5 runs): PASS on all four** -- 14.54 s
  best / 14.61 s median (sanitizer overhead, 4x under budget; Sprint 3
  measured 13.87 s on the smaller rule set).
- **Expected drift-forward, honestly noted**: the Sprint 3 run saw 5
  distinct rules; today's tree adds OC011 (secret-pattern, Sprint 5
  supply-chain tier) -- hence 6. The gate criteria are unaffected
  (>= 3 rules; the corpus digest and OC008 group counts reproduce the
  Sprint 3 record exactly).

The formal M2 gate date (2027-02-28) is unchanged: per
`docs/V1_TAG_RITUAL.md` the harness is re-run near the gate date and a
fresh verdict attached. With this backfill every milestone gate now has
a committed machine-readable rehearsal verdict (M1 gate review doc +
M2/M3/M4/M5 verdict JSONs).
