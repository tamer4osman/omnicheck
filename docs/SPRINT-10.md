# Sprint 10 -- Track D wave 1: Go resolver v1-go

**Date:** 2026-11-20 · **Base:** `c1ebd6b` (SPRINT-9 Go adoption) ·
**Contract:** `docs/LANGUAGES.md` §Go.5/§Go.6 (pre-committed at SPRINT-9,
marked SHIPPED here -- recipe step 1 updated first) ·
**Gates:** `scripts/acceptance/lang_go_gate.py` (5/5 PASS, two fixture
corpora) and `scripts/acceptance/go_corpus_gate.py` (6/6 PASS, pinned
real corpus); verdicts in `docs/evidence/`.

The recommended menu item: the pre-committed resolver sprint for Go.
Everything in `docs/LANGUAGES.md` §Go.5/§Go.6 marked "reserved for the
resolver sprint" landed here, in contract order.

## What landed

1. **Contract first.** `docs/LANGUAGES.md` §Go.5/§Go.6/§Go.7 rewritten to
   the shipped semantics BEFORE the code (the adoption-status table row
   updated). §Go.2's func-literal sentence is honestly amended: promoting
   named-literal assignments to symbols did NOT ship with the resolver
   sprint and remains a recorded descope (candidate: surface/coverage
   sprint).
2. **Resolver v1-go** (`src/imports.c`, `include/oc/imports.h`). An import
   path resolves to the PACKAGE DIRECTORY it names under an analysis
   root: roots probed in command-line order (the Python absolute-import
   discipline), DIRECT-CHILDREN rule, whole-package semantics. Guards:
   relative / absolute / `.`/`..`-segment / empty-segment forms never
   resolve; the filesystem outside the analyzed set is never consulted;
   **go.mod is never read** and module-prefix stripping stays out of
   scope, so a module-prefix specifier stays unresolved even when its
   target directory exists -- pinned by a decoy manifest in the fixture
   corpus (`go.mod` declares `module mycorp`, `cmd/main.go` imports
   `"mycorp/store"`, `store/` exists, resolution stays NULL).
   Single-target view: `oc_resolve_specifier` returns the package's
   lexicographically-first non-test direct child.
   - **Mapping precision (measured, then amended):** the SPRINT-9
     wording "dotted import paths" was ambiguous. The first
     implementation used a Python-style dot-to-slash conversion; the
     REAL-corpus gate (item 9) resolved ZERO edges with it -- Go's own
     mapping is VERBATIM (the import path equals the GOPATH-src-relative
     directory; "golang.org" is a literal directory name). Fixed to
     verbatim, amendment recorded in §Go.5; a dotted-host fixture
     (`example.org/app/store`) pins it.
   - **Test-file exclusion (measured, then amended):** the fan-out
     initially targeted every direct-child `.go` file; on the real
     corpus that fabricated FOUR false OC003 SCCs (same-directory test
     files that import their own package become mutual edge targets).
     Go compiles a package import WITHOUT the directory's `_test.go`
     files, so the fan-out now targets non-test children only; OC003 is
     0 on the real corpus, as Go's compiler demands. Contract and
     ARCHITECTURE amended; a unit assertion pins that a test file is
     never an edge target.
3. **Whole-package fan-out and marking.** The edge pass fans one resolved
   record out to every non-test direct-child `.go` file (Go compiles
   whole packages, so a package file's in-degree reflects package
   imports); the surface build marks that whole package used on a
   package import -- fail-open wildcard by contract, the recorded alias
   never narrows it. Documented, asserted consequence: at whole-package
   granularity OC005's verdict set for Go is EMPTY by construction.
   OC005 is enabled because its data pipeline is live and its semantics
   honest -- the alternative (name-level marking from aliases) would
   guess.
4. **Package_clause parse fact + bindings** (`src/parser_ts.c`,
   `src/parser_internal.h`). Per-file `package_clause` hint (grammar
   v0.25.0 declares NO `name` field -- verified against the pinned
   grammar, not assumed); one import binding per `import_spec` (alias
   when aliased, NULL for plain/blank/dot). **Latent SPRINT-9 bug
   fixed:** grouped `import ( ... )` blocks never recorded specifiers --
   the SPRINT-9 query anchored `import_spec` directly under
   `import_declaration`, so only the single-import form was recorded.
   Invisible while the spike resolver resolved nothing (zero edges
   either way); surfaced immediately by the resolver fixtures.
5. **Entry claiming + test conventions** (`src/surface.c`). Entry rules
   now include the parsed package clause: a `package main` file is an
   entry point exactly like a shebang script (the SPRINT-9 documented
   approximation "package main files NOT named main.go are not claimed"
   is closed). `*_test.go` joins the language-blind test-convention list
   (`go test` executes them directly -- same rationale as
   `conftest.py`). OC006's description string documents both.
6. **Pack flip.** `packs/go/pack.json` v0.2.0 enables **OC003** (file-level
   Tarjan SCC over the fan-out edges; package cycles appear at file
   granularity) and **OC005** (see 3). Disabled stays OC007 (no coverage
   adapter), OC009/OC010 (no manifest support) -- reasons in pack notes.
7. **Fixtures + tests.** New corpus `tests/fixtures/go_resolver/` (10 Go
   files + the decoy `go.mod`): whole-package fan-out (store = 2 non-test
   direct children; the package's own `store_test.go` is NOT a target),
   aliased import, the verbatim dotted-host edge
   (`example.org/app/store`), a/b package cycle, lonely package for
   OC006, `cmd/serve.go` (package main, not main.go) for parse-fact
   claiming, `store/store_test.go` for the convention. `test_sprint10.c`
   (66 checks): unit semantics (package dir, verbatim dotted hosts,
   subpackages, fan-out edges, test-file exclusion, root order, guards,
   marking construction) + E2E edge-pair assertions via the `oc graph`
   JSON + contracted finding counts + byte-identical rerun.
   `test_sprint9.c` updated to the contract that NOW holds (its header
   records the supersession; spike-era wording lives in git history).
   Suite: **26 tests per cell**.
8. **Gate upgrade.** `lang_go_gate.py` now gates TWO corpora: corpus A
   (structural, SPRINT-9, criteria 1-4) and corpus B (resolver, criterion
   5: the six contracted edge pairs, decoy ignored, verbatim dotted-host
   edge, test-file exclusion, OC003 exactly one 2-file cycle, OC005
   zero, OC006 exactly lonely.go, 10 files, zero parse errors,
   byte-identical reruns). **5/5 PASS** (best corpus-A wall 0.06 s
   single-core).
9. **Go reference corpus pinned (recipe step 8, m2/m3 pattern).**
   `scripts/acceptance/go_corpus_gate.py` + verdict
   `docs/evidence/go_corpus_gate_verdict.json`: the corpus is
   golang.org/x/net v0.27.0 (Debian source package `1:0.27.0-2`,
   sha256-pinned in `work/`; the git protocol is unreachable for new
   repos here, the Debian archive ecosystem is not) -- 676 real-world
   .go files, 24 packages, GOPATH layout. **6/6 PASS**: integrity (deb
   sha256 + corpus digest `50897955...b82273`), parse clean (676 files,
   ZERO parse errors), byte-identical reports, resolver realism (exactly
   11,719 imports edges, 377 distinct target files, all INSIDE the
   corpus, zero test-file targets), rule honesty (OC003 = 0 on real Go
   code -- import cycles are compile errors, so the resolver must never
   fabricate one; OC005 = 0 at scale; the remaining counts pinned at
   their measured values), wall 2.14 s against the 60 s budget. This
   gate is what caught BOTH resolver defects (the dot-conversion mapping
   and the test-file fan-out) before anything was pinned.
10. **Docs.** `docs/ARCHITECTURE.md` import-resolution + module-surface
   sections updated to the shipped semantics; README derived statistics
   refreshed by `audit_docs.py` (117 files / 23,176 LOC -- the gate
   enforces the match); files touched by this sprint clang-format clean.

## Evidence

- `lang_go_gate.py`: **5/5 PASS** across both fixture corpora (verdict
  committed).
- `go_corpus_gate.py`: **6/6 PASS** on the pinned golang.org/x/net
  corpus (verdict committed) -- parse clean, byte-identical, 11,719
  intra-corpus edges, OC003/OC005 zero, counts pinned, 2.14 s wall.
- Default cell: 26/26 ctest green (24 first-party suites + 2 vendored
  kernel self-checks); full 4-cell matrix re-proven by the chain re-run
  bundle at this sprint's HEAD.
- Docs gate: 29/29 items + LOC match, 100 percent.

## Deferred (contract items, unchanged)

- OC007 coverage adapter: `go test -coverprofile` line anchors
  (pre-committed §Go.6).
- OC009/OC010 manifests: `go.mod` (OC010 licence from the module clause;
  OC009 dep-vuln, OSV ecosystem **Go**, module path pins only),
  pre-committed §Go.6.
- func_literal promotion to symbols: recorded descope (§Go.2, amended
  this sprint), candidate for the surface/coverage sprint.
- Build tags / GOOS-conditional analysis: standing descope (§Go.8).
