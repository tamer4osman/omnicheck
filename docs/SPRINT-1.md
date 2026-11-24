# Sprint 1 (WBS 2.x) - TypeScript packs, import graph, graph passes, CRAP-lite scaffold

Committed 2026-11-18. Scope from plan WBS 2.x + the CRAP-lite scaffold,
sprinting toward the M1 gate (2026-12-31). Predecessor: `docs/SPRINT-0.md`.

## Session context (honesty first)

The workspace was wiped AGAIN between sessions (second time; the first wipe
produced commit 3ef33d0's "workspace restore" note). Damage this time:

- `omnicheck/` repo tree AND `.git` destroyed (commits 02befe1, 3ef33d0,
  c5bfd26 lost from disk).
- `empirical/` clones emptied (chalk, node-fetch, howdoi, requests,
  omnidecide-c).
- Toolchain partially reset: cmake and clang gone; gcc 14.2 survived.

Recovery (all verified, nothing hand-waved):

1. Repo restored from `download/omnicheck-sprint0.1.tar.gz` (which had
   captured `.git`: history 02befe1 + 3ef33d0 restored byte-exact, `git fsck`
   clean) plus `download/omnicheck-sprint0.2.tar.gz` (the BUG-1 fix tree,
   re-applied and re-committed as 8afdc14 -- same 12-file, +469/-15 delta).
2. cmake reinstalled via pip (4.4.5). **clang could NOT be reinstalled**
   (no sudo). This session's matrix is therefore gcc-only
   (Release + ASan/UBSan). The gcc-vs-clang byte-stability criterion is
   re-verified at the next session that has clang; sprint-0 evidence stands.
3. Real-repo corpus re-cloned at the SAME pinned commits
   (chalk 72110019, node-fetch 8b3320d2, howdoi 033dd3ce, requests 3c7baaa0,
   omnidecide-c 561fdb8a).

## Delivered

1. **TypeScript + TSX packs (register-not-train)**: vendored
   tree-sitter-typescript v0.23.2 (runtime v0.27.2 accepts language version
   14; grammars report LANGUAGE_VERSION 14). `packs/typescript` (.ts/.mts/
   .cts) and `packs/tsx` (.tsx) ship declaratively; vendor.sh pins the ref.
2. **Import edges + relative resolver** (`include/oc/imports.h`,
   `src/imports.c`): parser records raw specifiers during parse; resolution
   runs post-parse so edges only point at analyzed files. Contract in
   `docs/ARCHITECTURE.md` (relative JS/TS only; explicit-relative Python
   only; fixed probe order; lexical normalization preserving leading `/`).
3. **OC003 import-cycle**: iterative Tarjan SCC over OC_EDGE_IMPORTS, one
   warn finding per multi-file SCC, sorted file chain, computed once from
   the first file node. SCCs above the 512-file collection buffer are
   reported with a truncated, explicitly-marked chain (never silently
   dropped).
4. **OC004 complex-function (CRAP-lite scaffold)**: cyclomatic complexity
   computed at parse time and stored on function nodes (exposed in
   `oc graph` JSON); pass fires above `OC004.max_complexity` (default 15)
   and reports the no-coverage CRAP proxy comp^2+comp. NOT the flagship
   CRAP-ms: that needs the coverage composition (WBS 3.x).
5. **Parse-time complexity tables** ship with the language descriptor
   (register-not-train): JS family = branches/loops/cases/catch/ternaries +
   `&& || ??`; Python = branches/loops/except/boolean operators/ternaries/
   comprehension-ifs.

## Test suite: 12 -> 13 tests, all green in both available cells

`tests/test_sprint1.c`: resolver unit checks (bare/absolute/unresolved
skipped; package `__init__.py`; duplicate edges counted honestly) + 10 e2e
contracts (TS findings, TSX clean parse, edge counts incl. bare-skip,
JS and Python cycles exactly one OC003 each, OC004 + graph complexity,
config override, 4 packs, byte-determinism).

Bugs the harness caught during development (both real, both fixed):

- CSR adjacency in OC003 used a SIZE_MAX sentinel and then incremented it:
  unsigned wraparound wrecked the prefix sums -> out-of-bounds read (ASan
  caught it; segfault reproduced, fixed with an explicit degrees array).
- Resolver path normalization dropped the leading `/` on absolute paths, so
  zero edges resolved in e2e runs (unit tests passed because they used
  relative paths). Fixed; leading slash preserved.

## Performance budget (plan G3 target is 60 s at M2; sprint gate was 90 s)

Synthetic 10,000-file JS corpus (200 dirs x 50 files, ~70 lines each;
deterministic generator committed at `scripts/perf_gen.py`):

- `taskset -c 0 oc check work/perf10k --packs-dir packs`: **12.05 s wall
  (12.13 s rerun)**, single core. ~13% of the 90 s sprint gate.
- 10,000 OC002 (one marker per file, by construction) + 200 OC003 (one
  50-file cycle per directory, by construction) -> exactly the encoded
  ground truth; reruns byte-identical.

## Real-repo re-audit (same pinned commits as the Sprint 0 audit)

Artifacts: `download/omnicheck-validation-artifacts/sprint1-recheck/`.

| repo | analyzed v0.1.0 -> S1 | import edges | findings S1 | notes |
| --- | --- | --- | --- | --- |
| chalk | 16 -> 20 (+4 .d.ts) | 17 | OC001x2 OC002x4 OC004x1 (cx 36) | OC002 2->4: the .d.ts files carry TODOs |
| node-fetch | 23 -> 25 | 41 | OC001x11 OC002x5 OC004x8 (cx max 66) | |
| howdoi | 7 -> 17 (+10 VS Code .ts) | 12 | none (cx max 15) | still a TRUE clean pass, not vacuous |
| requests | 37 -> 37 | 88 | OC001x11 OC002x8 OC003x1 OC004x7 | OC001/OC002 counts match v0.1.0 exactly |
| omnidecide-c | 4 -> 4 | 0 | OC001x2 OC004x1 (cx 16) | |

Honest observations:

- The requests OC003 is a REAL 11-file SCC (requests/__init__.py ->
  _types.py -> ... -> __init__.py). requests intentionally uses deferred
  imports to break it at runtime: the finding is structurally true and
  intent-blind -- the exact gap the OmniDecide triage layer exists for.
- Import-edge completeness on real repos is PARTIAL by design (bare
  specifiers unresolved; tsconfig paths not implemented). Edge counts are
  lower bounds, labeled as such.
- gcc-vs-clang byte-stability could not be re-verified this session (no
  clang binary available; see recovery note). Determinism was verified as
  repeat-run byte-identity on every corpus item incl. the 10k run.

## Explicit descopes (descope-not-slip, recorded before they hurt)

- Clang CI cell: re-verify at next clang-capable session (carried item).
- unused-export pass (snapshot listed it): descoped from Sprint 1 -- it
  requires export tracking + intra-repo usage resolution maturity; with a
  partial resolver it would fabricate false positives. Re-planned behind
  resolver v2.
- Full module resolution (tsconfig paths, package exports): WBS 3.x.
