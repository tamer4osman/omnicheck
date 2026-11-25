# Sprint 5 (M4 RC scope) - supply-chain tier, V8 adapter + CRAP
# leaderboard, mutation engine, breaking-change gate

Committed as one sprint on 2026-11-18. Everything below was verified by
direct execution. All four M4 gate criteria (plan Table 9, formal date
2027-09-30) are PASS with committed, replayable evidence
(`scripts/acceptance/m4_gate.py`, `docs/evidence/m4_gate_verdict.json`),
and the M4-verified KPIs from plan Table 3 are PASS on measured runs.

## Session context (honesty first)

- NO workspace wipe this session; the sprint started from the live tree
  at `eeca625` (Sprint 4).
- **The carried gcc-vs-clang item is CLOSED.** clang has been absent
  from this workspace for four sessions (no sudo). This session it was
  obtained WITHOUT sudo: `apt-get download clang-19 libclang-cpp19
  libllvm19 libclang-common-19-dev libclang-rt-19-dev` + `dpkg -x` into
  a local prefix (Debian clang 19.1.7). One layout fix was needed: the
  clang-rt package ships `lib/clang/19/lib/linux` while the driver
  expects `lib/clang/19/lib/x86_64-pc-linux-gnu`; a plain copy fixed it
  (the sandbox disallows symlinks). Details:
  `docs/evidence/cross_compiler_reverify.json`; the M1 gate review now
  records the closure.
- The sanitize cell earned its keep again: LSan + ASan caught FOUR real
  defects in the first drafts of this sprint's code before they could
  reach a commit (listed below).

## Delivered

1. **Supply-chain tier (WBS 4.2): `src/supply.c` + three rules.**
   - OC009 `dep-vuln`: pinned dependencies (package.json exact pins,
     requirements.txt `==` pins, pyproject PEP 508 `==` pins) matched
     against a VENDORED OSV snapshot (`--osv-snapshot`, schema
     `omnicheck.osv_snapshot/v0` with a visible `snapshot_date` echoed
     into the report meta -- plan Ch4 4.7 external-data determinism).
     Unpinned requirements are reported (warn) as honestly unmatchable;
     npm range specifiers (`^ ~ *`) are never guessed at. Version
     compare is documented dotted-numeric with prerelease-before-
     release semantics.
   - OC010 `licence`: package.json without a non-empty "license" string
     and pyproject.toml without a `[project] license = "..."` line are
     flagged (warn). requirements.txt is out of scope by design (no
     licence concept). The pyproject reader is a bounded line scanner,
     NOT a TOML parser -- documented honestly.
   - OC011 `secret-pattern`: fixed, deterministic pattern set over
     source text -- AWS `AKIA` + exactly 16 charset chars (charset-run
     boundary enforced so quoted tokens match), PEM private key header,
     GitHub `ghp_` + 36, Slack `xox*` tokens (error severity), and
     generic secret-name assignments with long quoted literals (warn).
   - Manifest-level findings anchor at line 1 with the dependency named
     in the message: the JSON parser does not track line numbers and
     positions are never guessed (requirements.txt DOES carry real line
     numbers; both behaviours are documented).
   - Report meta gains a conditional `supply` block (manifests_scanned,
     osv_snapshot_date); runs without manifests stay byte-identical.
2. **V8 coverage adapter + CRAP leaderboard (WBS 4.3 remainder).**
   - The coverage loader now sniffs three formats: Istanbul, coverage.py
     (both Sprint 3) and V8 (NODE_V8_COVERAGE-style). The V8 adapter
     maps byte-offset ranges to line anchors by reading the url file
     once: a line is an anchor when any function range contains its
     start byte, and covered when the DEEPEST containing range has
     count > 0 -- uncovered inner blocks shadow covered enclosing
     functions, which is exactly V8 block-coverage semantics.
   - `oc check` gains the `crap_leaderboard` block (JSON) and a human
     leaderboard section: top 10 MEASURED functions by CRAP desc (ties
     path asc, line asc), rows carrying comp/cov-pct/crap. Conditional
     on coverage presence -- no coverage, no leaderboard, zero byte
     drift.
3. **Mutation engine (WBS 4.5): `src/mutate.c` + `oc mutate`.** The
   plan's seven operator sets are tree-sitter queries compiled against
   the same pinned grammars the analysis uses: relational swap,
   logical swap, condition negation (!/not removal), constant tweak
   (bool swap, integer +1), statement deletion (bare-string/docstring
   statements excluded -- deleting a no-effect statement is an
   equivalent mutant, not a signal), return-value change (0/None) and
   argument swap. Runner: serial execution in deterministic collection
   order (byte offset asc, op id asc); per-mutant cache keyed by
   content hash + exact edit (any file change invalidates); hard
   timeout via fork/exec + process-group kill; in-place splice with
   guaranteed restore (cmp-verified in tests); pre-flight clean-suite
   run -- a suite that fails unmutated fails LOUD (OC_ERR_USAGE)
   instead of producing a meaningless 100% kill ratio. Statuses:
   killed / survived / timeout / skipped-{cache,coverage,budget};
   survivors exit 1 (CI gate semantics). The survival report is
   `omnicheck.mutation_report/v0` with wall seconds. Interpreter-level
   injection reading: Python/JS mutate at source level with no
   compilation step -- source splice IS interpreter-level for these
   packs; compiled-language strategies stay post-1.0 by plan.
   Coverage-guided selection: with --coverage, only mutants inside
   functions with composed coverage > 0 (test-reachable) run.
4. **Breaking-change gate (WBS 4.6): `src/compat.c` + `oc compat`.**
   Pure public-surface diff of two analysis roots (baseline vs
   current), paths compared relative to each root. BREAKING:
   removed named export, removed public module (a file with exports
   gone entirely), function node gone from a still-existing public
   module. Informational: additions (exports/functions/modules) and
   private-module removal. `omnicheck.compat_report/v0` verdict +
   exit 1 on BREAKING makes it a drop-in CI gate. Signature/parameter
   changes are out of scope for v0 (the graph carries no parameter
   metadata yet; recorded against the post-M4 backlog).
5. **M4 gate harness (`scripts/acceptance/m4_gate.py`).** All four
   Table-9 criteria executable end-to-end against the committed
   fixtures, plus the Table-3 M4 KPIs. Verdict committed:
   `docs/evidence/m4_gate_verdict.json`.

## Carried item closed: gcc-vs-clang (four sessions carried)

- Four-cell matrix, all 19/19 green: gcc 14.2.0 Release; gcc Debug +
  ASan/UBSan/LSan; clang 19.1.7 Release; clang Debug + ASan/UBSan/LSan.
- Cross-compiler byte identity (both Release binaries): examples JSON +
  human, compat/mutation fixtures JSON, 10k corpus JSON (13,302,920
  bytes) -- all `cmp` identical.
- The 10k corpus output is ALSO identical to the pre-Sprint-5 baseline:
  the whole M4 scope added zero byte drift on manifest-free,
  coverage-free trees.
- Evidence: `docs/evidence/cross_compiler_reverify.json`; M1 gate
  review appended.

## Sanitize-cell yield (defects caught before landing)

1. LSan: `oc_manifests_destroy` freed the manifest array but leaked
   each manifest's dependency array.
2. LSan: `oc_osv_destroy` leaked per-record package arrays and
   per-package range arrays.
3. ASan (heap-use-after-free, success path only): the mutation report
   printer read arena-owned mutant strings after `oc_arena_destroy` --
   in the gcc Release build this was silent memory reuse; the sanitizer
   cell made it fatal before it could ship.
4. LSan: `oc mutate` leaked the partially-collected mutants array on
   error paths (now destroyed before status dispatch).

## Test suite: 18 -> 19 test binaries, all green in all four cells

- `tests/test_sprint5.c` (148 checks): version-compare units; OSV
  snapshot load + match (hit / fixed boundary / below-introduced /
  unknown name / unpinned / ecosystem mismatch); manifest load (package
  exact pins only, requirements pins with real line numbers, pyproject
  deps + missing licence); secret scan (boundary-exact AWS, PEM, ghp_,
  xox, generic); V8 adapter (anchors/covered per function span,
  unmeasured file honest miss, Istanbul regression check); mutation
  collection (6 ret-change, 2 rel-swap, 1 stmt-del, 1 arg-swap on the
  fixture, docstring excluded, deterministic byte order, unknown
  grammar hard error); e2e supply run through the real binary (meta +
  rule counts); e2e leaderboard (4 rows desc, integer composition
  formula verified, process_order tops processOrder);
  e2e compat (BREAKING exit 1 with removed-export + removed-function,
  SAFE exit 0); e2e mutation (10 mutants, 9 killed, the designed
  arg-swap survivor, file restored cmp-identical, wall < 30 s, broken
  suite -> exit 2).
- 18 pre-existing binaries green untouched.
- Full matrix: gcc Release, gcc Sanitize, clang Release, clang Sanitize
  -- 19/19 each.

## Performance budget (sprint gate 90 s single-core)

10k-file corpus: 12.43 s (gcc) / 11.65 s (clang), byte-identical to the
Sprint 4 baseline. Sprint 4 measured 8.87 s with the same corpus: the
~3 s delta is the OC011 source-retention cost (raw bytes now kept alive
for the secret scan) plus machine variance. Still ~5x inside the 60 s
M2 target and ~7x inside the 90 s sprint gate.

## M4 gate status (formal date 2027-09-30)

All four criteria PASS with committed, replayable evidence (the
harness re-runs near the gate date like M1/M2/M3). KPIs: analysis
count 11/11 passes enabled on ts-js AND python (>= 10 target);
CRAP coverage holds (every comp >= 10 function carries CRAP in output
under full coverage composition); mutation CI budget measured 0.62 s
on the demo project (target < 10 min; the reference-repository run is
re-measured near the gate date).

## Explicit descopes (descope-not-slip, recorded before they hurt)

- **Mutation survivor triage**: the survival report lists survivors
  deterministically; OmniDecide rank-only annotation of survivors is
  deferred until a real ecosystem consumer needs it (the findings-level
  triage seam is unchanged and already guarded).
- **Compat signature diffs**: no parameter metadata in the graph;
  removed-symbol detection only (recorded post-M4).
- **LSP/MCP do not expose mutate/compat**: both are CI-shaped, batch
  commands; server surfaces were not in the WBS lines.
- **pyproject reader**: bounded line scanner for [project] license and
  dependencies; not a TOML parser; ambiguous shapes are skipped, never
  guessed.
- **requirements.txt licence**: not applicable; OC010 skips it by
  design.
- **package-lock.json / node_modules resolution**: unchanged descope,
  recorded against post-M1 plan reading (package.json exports stay
  descoped from Sprint 2).

## Documentation

- docs/ARCHITECTURE.md: supply tier contract, V8 adapter + leaderboard
  contract, mutation engine contract, compat gate contract, rule
  registry 8 -> 11.
- README.md: `oc mutate` / `oc compat` / `--osv-snapshot` quickstarts,
  M4 acceptance section.
- docs/evidence/cross_compiler_reverify.json + this file.

## Next sprint (M5 GA scope per plan Table 9 / P4)

Determinism audit on the full corpus (byte-identical across machines),
fuzz targets + corpus hardening (WBS 5.1), docs completeness for all
shipped analyses (Table 3 M5: 100 percent), packaged binaries + npm
shim, v1.0 tag ritual. Carried into M5: harness re-runs for M1/M2/M3/M4
near their gate dates (M4 harness re-run near 2027-09-30; M1 drift
re-check near 2026-12-31).
