# Ritual dress rehearsal (2026-11-18, post-scope)

Every scope sprint of the plan is delivered (Sprint 6 closed the M5 GA
rehearsal). What remained unproven was the RITUAL ITSELF: the chain of
pre-conditions in `docs/V1_TAG_RITUAL.md` had only ever been executed
piecemeal, across different sessions and different tree states. This
rehearsal executed the full chain END-TO-END in one session, against
one tree (`d73129b` at start, `ec5c7d5` after the catch below), so that
each formal gate date (M1 2026-12-31, M2 2027-02-28, M3 2027-05-31,
M4 2027-09-30, M5 + tag 2027-12-31) is a re-run of proven commands, not
a first execution. Formal dates are unchanged; this was an early run,
per the project's rehearse-early convention.

## Headline: the rehearsal caught a real fresh-clone breakage

The M1 drift replay leg clones the repo and rebuilds from scratch. The
first such clone FAILED: `test_sprint4` 2/58 (resolver-v3 import edges
missing) while the main tree was 20/20 green on the same commit.

Root cause: the core-dump ignore pattern `core.*` in `.gitignore` also
matched the test fixture `tests/fixtures_sprint4/pyabs/mypkg/core.py`.
The file existed in the working tree (created during Sprint 4), was
silently never tracked, and every in-tree test run since has been
testing against a file that a fresh clone does not have. Same
invisible-until-clone class as the Sprint-review
`third_party/CMakeLists.txt` catch (`24d1760`).

Fix (`ec5c7d5`): scoped `!` negation in `.gitignore`, fixture tracked,
fresh clone re-verified from the fixed commit: 20/20. The catch is the
rehearsal's main deliverable: the ritual's design goal -- mechanical
steps that cannot be narrated into passing -- is proven, six weeks
before the first formal gate date.

## Ritual pre-condition chain, executed

1. `git status --short` empty on `main`: PASS (before and after every
   leg below).
2. Harness re-runs with fresh verdicts (all five obligations; verdicts
   redirected to `docs/evidence/ritual_rehearsal/` -- the historical
   sprint verdicts stay untouched):
   - **M1 drift replay** (replay block of `docs/M1_GATE_REVIEW.md`):
     main-tree ctest 20/20 (default 11.2 s) and 20/20 (sanitize
     112.7 s); fresh-clone + `vendor.sh` + build + ctest 20/20 (after
     the catch above; honest drift note: the replay text still says
     14/14 -- the Sprint 2 suite count; the tree now carries 20 suites,
     expected forward drift); `oc check tests/fixtures_broken` exit 2
     with stderr warning + named `parse_errors` (the no-silent-partial-
     analysis contract); `examples` report main-vs-fresh-clone
     BYTE-IDENTICAL, sha256 `769a904e...`; 10k perf corpus regenerated
     (10,000 files) and re-scanned single-core: **9.64 s** vs the 90 s
     sprint gate (10.7 percent) and the 60 s M2 target -- faster than
     Sprint 5's 12.43 s on the same grown rule set.
   - **M2 harness** (`m2_gate.py`, eslint @ `21026992...`, scope lib/):
     PASS all four criteria -- 6 rules, 394 analyzed, 0 parse errors,
     10 runs 1 distinct hash, 0.66 s best / 0.67 s median.
   - **M3 harness** (`m3_gate.py`, black @ `c0e143e7...`, scope
     src/black -- pin re-fetched, 25 files / 16,731 LOC reproduced):
     PASS all five criteria (packs manifest, resolver v3 edges, LSP
     session byte-identical, MCP tools listable + callable, OmniDecide
     rank-only guardrail intact).
   - **M4 harness** (`m4_gate.py`): PASS all four criteria + KPIs
     (CRAP leaderboard, mutation survivor + budget, BREAKING/SAFE
     compat verdicts, seeded OC009/OC010/OC011 supply findings, 11
     analyses per pack).
   - **M5 harness** (`m5_gate.py`, FULL -- no `--skip-audit`): PASS all
     criteria + KPIs (details under 4-6 below).
3. Full 4-cell matrix FROM SCRATCH (clang 19.1.7 re-obtained without
   sudo via the recorded `apt-get download` + `dpkg -x` recipe;
   sandbox recycles had wiped `/tmp/clangroot`): configure + build +
   ctest in `default`, `sanitize`, `clang-release`, `clang-sanitize` --
   **20/20 in every cell** (test time: 11.2 / 112.7 / 11.1 / 146.2 s).
4. `determinism_audit.py` on the freshly rebuilt cells: 6 runs /
   4 cells, **1 distinct report digest** (14.9 MB reports
   byte-identical across compilers and presets); release worst
   **12.05 s** single-core vs 90 s gate / 60 s target; sanitize worst
   138.69 s (recorded, not gated). Fresh audit JSON committed.
5. `audit_docs.py`: **24/24 items documented (100.0 percent)**.
6. `package.sh` + npm shim: tarball + sha256 manifest + byte-identical
   report smoke PASS; shim verified through BOTH `bin/native/oc` and
   `OC_BIN` with exact exit passthrough (node v24.22.3).

## Verdict

**CONTINUE -- the ritual is mechanically executable end-to-end and
every obligation re-asserts green on the current tree.** The formal
gate dates stand: the M1 drift re-check executes this same chain at
2026-12-31, and each later gate re-runs its harness near its date, per
`docs/V1_TAG_RITUAL.md`. No v1.0.0 tag was cut and none is due until
2027-12-31.

## Honest drift notes

- The replay block's recorded suite counts (14/14) are stale: the tree
  is at 20 suites. The replay procedure itself is unchanged; only the
  expected counts moved forward.
- Perf numbers moved with the rule set (OC008 dup pass + OC011 source
  retention joined since the 8.13 s Sprint 2 baseline): 9.64 s replay
  / 12.05 s audit-worst, both far inside both budgets.
- The `docs/evidence/determinism_audit.json` committed here is the
  FRESH audit from the from-scratch rebuild (it replaces the Sprint 6
  artifact; git history keeps the old one). Its digest equals the
  Sprint 6 digest -- no drift in output bytes, only in wall time.
- clang had to be re-obtained this session (`/tmp` prefix wiped by the
  sandbox recycle). The recipe in `SPRINT-5.md` /
  `cross_compiler_reverify.json` reproduced clang 19.1.7 exactly --
  recorded as a reminder that the 4-cell matrix depends on that
  reinstall step in any fresh sandbox.
