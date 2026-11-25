# Sprint 2 (WBS 3.x) - resolver v2, module surface, CRAP-ms composition

Committed as one sprint on 2026-11-18. Everything below was verified by
direct execution on the restored workspace; see "Session context" for
the recovery honesty note.

## Session context (honesty first)

- Third workspace wipe: `omnicheck/` (tree + .git) destroyed again;
  restored from `download/omnicheck-sprint1.tar.gz` (includes .git and
  the vendored grammars -- the deliberate lifeline). History intact
  through 81cd99b; `git fsck` clean; 13/13 green after restore before
  any Sprint 2 code was written.
- cmake reinstalled via pip (4.4.5) after the wipe. gcc 14.2 present.
  **clang is STILL not installable in this sandbox (no sudo)**: the
  gcc-only matrix carries over, and gcc-vs-clang byte-stability
  re-verification remains CARRIED to the next clang-capable session.
- `empirical/omnidecide-c` and `work/tools/omnidecide-c` were wiped to
  empty dirs and the repo is not publicly clonable (private). The
  omnidecide-c OBS-1 numbers from the Sprint 0 audit stand as recorded
  in `download/omnicheck-validation-artifacts/oc-realrepo-2026-11-18/`;
  OBS-1's FIX is regression-tested by the `dots` fixture (see below)
  and is now visible in every report's meta block.

## Delivered

1. **OBS-2 closed -- unified function-span semantics** (CRAP-ms
   prerequisite). One canonical span for every language: the DEFINITION
   EXTENT (first line of the defining construct through its last line).
   Graph fields renamed `body_start/body_end` -> `def_start/def_end`
   (graph JSON keys too). For 1TBS JavaScript this equals the old
   decl..brace span; for Python the `def` line is now inside the span.
   Passes (OC001, OC004, OC007) and coverage composition all consume the
   same span; the threshold asymmetry named in the Sprint 0 audit is
   closed in the strict direction (functions at the boundary now count
   the signature line).
2. **OBS-1 closed -- counted discovery skips.** Discovery returns
   `oc_discover_stats`; report meta gains
   `files_skipped_dot_entries` and `files_skipped_ignored_dirs`. Silent
   skips became counted skips.
3. **Resolver v2 (tsconfig paths/baseUrl).** New `src/tsconfig.c`:
   nearest tsconfig.json/jsconfig.json per importing file (JSONC
   tolerated via the new lenient JSON parser: comments + trailing
   commas, string payloads never touched), `compilerOptions.paths`
   exact + single-`*` patterns (longest-first), baseUrl default per TS
   legacy rules. The upward search is BOUNDED AT THE ANALYSIS ROOT
   (the CLI root arguments) -- a config outside the analysis is
   invisible, by contract. Bare specifiers without an alias still skip,
   never guessed. Shared resolution context (`oc_resolve_ctx`) with a
   sorted path index (binary search replaces the O(n) node scan).
4. **Module surface (`src/surface.c`, `include/oc/surface.h`).** The
   parser records export candidates and import BINDINGS (names) while
   parsing; `oc_surface_build` resolves bindings post-parse through the
   same resolver and marks exports used. Fail-open everywhere:
   wildcards (`import *`, `from x import *`), side-effect imports,
   `require()` / dynamic `import()`, `export * from`, and default
   imports mark whole files used rather than guessed. Python surface:
   top-level defs/classes (underscore-prefixed excluded as private
   convention); when `__all__` exists it IS the public surface (PEP 8)
   and everything else is demoted. Re-exports mark the SOURCE file's
   exports used.
5. **OC005 unused-export (warn).** An export never referenced by any
   analyzed file, reported ONLY when the containing file is itself
   imported -- dead files stay OC006's domain and barrels without
   consumers are not double-flagged.
6. **OC006 unreferenced-module (info).** Zero inbound import edges and
   no entry-point rule claims it. Entry rules: pack `entry_points`
   globs (now ENFORCED -- the pack schema declared them "from WBS 3.2";
   `**/__init__.py` added to the python pack), shebang first line, and
   narrow test conventions (*.test.*, *.spec.*, test_*.py, *_test.py,
   conftest.py, __tests__ dirs). Severity info on purpose: structurally
   true, intent-blind -- the OmniDecide layer's job.
7. **CRAP-ms v0 (the flagship composition, WBS 3.x).** New
   `src/coverage.c`: loads Istanbul `coverage-final.json` (JS/TS family)
   and coverage.py `coverage.json` (Python), sniffs the format per
   file, unions line sets across sources (the "ms" = multi-source
   increment), matches coverage paths onto analyzed paths by exact
   string then UNIQUE "/"-boundary suffix (ambiguous suffixes match
   nothing, counted honestly). Per-function coverage = covered anchors
   inside the definition span / anchors inside it; OC007
   crap-hotspot (warn) fires at `OC007.max_crap` (default 30) with
   CRAP(m) = comp^2 * (1 - cov/100) + comp (integer floor). `oc check
   --coverage FILE` (repeatable) and `oc graph --coverage FILE` expose
   it; graph function nodes gain coverage_anchors/coverage_covered/
   coverage_pct/crap only when coverage was composed. Unmeasured
   functions (no entry, no anchors) never produce findings. OC004
   remains the no-coverage proxy.
8. **Resolver/surface share one contract:** edges and usage marking use
   identical resolution (roots-aware `oc_imports_resolve_ex`), so
   OC005/OC006 can never see an edge the graph does not have.

## Test suite: 13 -> 14 tests, all green in both available cells

`tests/test_sprint2.c` (95 checks): JSONC lenient parser (comments,
trailing commas, string payloads with `,]` preserved, strict parser
still rejects the same text); entry glob matcher; coverage composition
unit (Istanbul + coverage.py, span restriction, unique-suffix match,
ambiguity rejection); OBS-2 span contract on the graph; OC005/OC006 on
JS and Python fixtures (barrel/shebang/test suppression, wildcard
fail-open, re-export usage, `__init__.py` entry, underscore-private);
tsconfig alias edge (e2e, bare `@lib/util` resolves through
tsconfig.json); OBS-1 counters; CRAP-ms e2e (Istanbul: cold 0% ->
CRAP=6 > 5, warm 100% silent, meta counters exact; coverage.py: 0%
function flagged at max_crap=1); graph --coverage fields; no-coverage
absence contract; byte-identical rerun of the composed run.

14/14 green in gcc Release AND gcc ASan/UBSan. **ASan caught 2 real
bugs during development** (both fixed + regression-tested): the
tsconfig cache was an UNZEROED arena struct (garbage pointer SEGV in
the unit test) and its heap arrays were never freed (LeakSanitizer);
`oc_surface_hint_push` returned success without incrementing the count
(shebang entry points silently inactive). A third class (resolve ctx
index arrays leaking) was fixed by moving them into the arena. Lesson
repeated: in-process unit tests + sanitizers catch what e2e green
masks.

Also fixed during dev, caught by the new tests BEFORE landing: the
test-file convention originally matched bare `tests/` directories,
which classified this repo's entire test tree as entry points and
silenced OC006; the JSONC trailing-comma stripper corrupted commas
inside string payloads; a fixture JSON had an extra closing brace.

## Performance budget (sprint gate was 90 s single-core)

10k-file corpus (scripts/perf_gen.py, same generator as Sprint 1):
**8.82 s single-core** (taskset -c 0), byte-identical rerun, exactly
10,000 OC002 + 200 OC003 findings as encoded -- 9.8% of the 90 s gate,
and FASTER than Sprint 1's 12.05 s despite three new passes, because
the resolver's sorted path index replaced the O(n) per-lookup node
scan. The corpus is cycle-saturated (every file imported), which also
exercises OC005/OC006 state at scale.

## Real-repo re-audit (same pinned commits as Sprint 0/1)

Artifacts: `download/omnicheck-validation-artifacts/sprint2-recheck/`
(gcc Release; every repo byte-identical on rerun). omnidecide-c was
NOT re-run: its local clone was wiped to an empty dir and it is not
publicly clonable (recorded above). Repos re-cloned at the same pins:
chalk 7211001, node-fetch 8b3320d, howdoi 033dd3c, requests 3c7baaa.

| repo | analyzed S1 -> S2 | OC001-004 S1 -> S2 | OC005 | OC006 | import edges |
| --- | --- | --- | --- | --- | --- |
| chalk | 20 -> 20 | 2/4/0/1 -> 2/4/0/1 (exact) | 9 | 16 | 17 -> 17 |
| node-fetch | 25 -> 25 | 11/5/0/8 -> 11/5/0/8 (exact) | 6 | 10 | 41 -> 41 |
| howdoi | 17 -> 17 | 0/0/0/0 -> 0/0/0/0 | 12 | 5 | 12 -> 12 |
| requests | 37 -> 37 | 11/8/1/7 -> **13**/8/1/7 | 36 | 5 | 88 -> 88 |

Reading:

- Legacy pass counts are EXACTLY stable except requests OC001 11 -> 13
  -- the OBS-2 shift, precisely as designed. Both new findings sit
  exactly at the threshold: sessions.py `__init__` (442..503, extent
  62; body-block-only span ended at line 502 = 60 lines = at threshold,
  not flagged before) and utils.py `should_bypass_proxies` (810..870,
  extent 61 vs body 60). The def line is now counted; the strict
  direction is the documented intent.
- howdoi exit 0 -> 1: NOT a regression -- its 12 OC005 findings (unused
  exports in the VS Code extension .ts files) are warn severity, so the
  fail-on-warn exit code fires. OC001-OC004 remain zero; the repo is
  still clean on the cheap-AST layer, and the surface layer is
  intent-blind by contract (triage's job). Honest framing: "true clean
  pass" from Sprint 1 becomes "clean on OC001-OC004; surface findings
  are structural".
- requests OC005 x 36: Python's top-level surface is large and much of
  it is consumed by DOWNSTREAM projects, not intra-repo -- the
  intent-blindness the plan predicted, now measured.
- chalk/node-fetch OC006 x 16/10: mostly example/, test helpers and
  standalone scripts -- structurally true, intent-blind; exactly the
  finding class the OmniDecide layer must rank down.
- Import-edge counts unchanged (17/41/12/88) relative to Sprint 1 --
  resolver v2 adds ALIAS resolution; these repos have no tsconfig
  aliases, and bare-specifier policy is unchanged. Edge counts remain
  lower bounds by design.

## Explicit descopes (descope-not-slip, recorded before they hurt)

- **Clang CI cell + gcc-vs-clang byte-stability**: carried again (no
  clang binary in this sandbox, no sudo). The matrix is gcc-only for
  the second session running.
- **package.json exports / node_modules resolution**: bare npm
  specifiers still skip. tsconfig aliases cover the intra-repo cases
  the plan named for WBS 3.x; package-exports resolution is WBS 4.x.
- **Python absolute intra-repo imports**: still unresolved (v1
  contract). Their targets therefore surface in OC006 (unreferenced)
  more often than ground truth -- visible in requests (5 OC006 where
  absolute imports hide edges). Resolver v3 (package-aware absolute
  resolution) is the honest next step, not a silent guess today.
- **Istanbul fnMap/f per-function counts**: composition uses LINE
  anchors (statementMap/s) uniformly across languages instead of
  Istanbul's per-function counters; documented in ARCHITECTURE.md as
  the cross-language-uniform approximation.
- **Symbol-level export usage inside barrels**: `export {x} from y`
  marks y's export used (source-side); the barrel's own re-exported
  name is surface but not separately usage-tracked.

## M1 gate status (2026-12-31)

All five kill-switch criteria now have mechanisms behind them (see
docs/M1_KILL_SWITCH.md for the standing status). Sprint 2 closes the
two OPEN observations from the Sprint 0 audit; nothing is open against
M1 except the carried clang cell and the general M1 gate review
itself. Remaining pre-M1 work per plan: M1 gate review (walk the five
criteria with evidence), then M2 Alpha (WBS 4.x).
