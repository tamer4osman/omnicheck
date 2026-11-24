# Sprint 9 — Track D wave 1: Go adoption (spike scope)

**Date:** 2026-11-20 · **Base:** `5ba6c7e` (SPRINT-8 README screenshots) ·
**Contract:** `docs/LANGUAGES.md` (written FIRST, recipe step 1) ·
**Gate:** `scripts/acceptance/lang_go_gate.py`, verdict
`docs/evidence/lang_go_gate_verdict.json` (4/4 PASS)

The north-star menu item: the first language beyond the SPRINT-1/2 wave,
following the per-language recipe (§7.2 of the handoff) contract-first.

## What landed

1. **Language contracts (Go + Java).** `docs/LANGUAGES.md` fixes the six
   contract points for Go (adopted this sprint at spike scope) AND Java
   (pre-approved, no code yet -- its adoption sprint starts from an
   approved contract). Contract-before-code, mirroring
   `docs/ARCHITECTURE.md`'s contract style.
2. **Grammar vendoring.** `tree-sitter-grammars/tree-sitter-go` pinned at
   **v0.25.0**. The adoption environment could not reach github over the
   git protocol for new repositories (clone, codeload and CDN mirrors all
   blocked beyond the already-vendored repos), so the vendored sources
   came from the PyPI sdist of the pinned version -- sha256 recorded in
   `THIRD_PARTY_NOTICES.md`, provenance note included, upstream commit
   SHA honestly marked unrecorded. `scripts/vendor.sh` gained a codeload
   tarball fallback (same pinned tag, content-identical) so a normal
   environment re-vendors from upstream. No external scanner.
3. **Parser descriptor.** `OC_LANG_GO` family; function query =
   `function_declaration` + `method_declaration` (func_literal is NOT a
   symbol -- documented descope; its body still counts toward the
   enclosing function's complexity); comment query reuses `(comment)`;
   import query records one raw specifier per `import_spec` (no @ist
   capture: bindings belong with resolver v1-go, never record a promise
   the resolver cannot keep); complexity table = gocyclo-aligned
   if/for/expression_case/type_case/communication_case + `&&`/`||`;
   exports query = capitalized package-level declarations (methods are
   NOT package surface). `oc_resolve_specifier` gained an explicit
   `OC_LANG_GO` branch returning NULL -- the never-guess spike posture,
   documented in code and contract.
4. **Pack + fixtures + tests.** `packs/go/pack.json` (data-only; six
   enabled passes OC001/OC002/OC004/OC006/OC008/OC011, five disabled
   with reasons in the pack notes). Fixtures
   `tests/fixtures/go/` (5 files designed to fire every enabled rule
   deterministically). `tests/test_sprint9.c`: 46 checks -- resolver
   posture (intra-repo module path stays unresolved), pack shape, graph
   honesty (zero imports edges), rule counts, OC006 entry-point claim
   for main.go, byte-identical rerun.
5. **Acceptance gate.** `scripts/acceptance/lang_go_gate.py` -- the
   m2/m3 pattern applied to a language fixture set: parse clean,
   10 runs byte-identical, contracted rule counts, zero imports edges.
   4/4 PASS, best wall 0.06 s.
6. **Docs-gate upgrade (D-fixes).** `scripts/audit_docs.py` now derives
   the registered packs from `packs/*/pack.json` and the first-party LOC
   table from the tree, and FAILS on drift. This exposed and fixed the
   SPRINT-8 stats double-count: `src/passes/` was summed both inside the
   `src/` row and as its own row -- README now carries the derived
   numbers (116 files / 22,307 LOC, src impl excluding passes).
7. **B8 closed.** `gate_day_runner.py` harnesses step records pass=True
   only when EVERY harness due for the gate is present and green
   (per-leg details stay incremental) -- the SPRINT-7 stale-partial-PASS
   incident cannot recur.
8. **Format debt cleared.** The files touched by this sprint are
   clang-format clean under the repo style (clang-format was unavailable
   in prior environments; `scripts/check-format.sh` silently skipped --
   `parser_ts.c` carried 42 pre-existing violations at HEAD, now 0).

## Evidence

- Matrix: 4 cells × **25 tests** (24 + test_sprint9), 100% after the
  SPRINT-9 rebuild (fresh chain bundle re-proves it below).
- Determinism audit at the SPRINT-9 tree: ONE report digest across all
  cells (`eb8af323…9902c`, report 20,109,725 bytes -- the 5-byte growth
  is the go pack joining `meta.packs`), corpus digest unchanged
  (`a4ec9ac9…337ecc`), worst Release wall **11.16 s** (budget 90 s).
- Docs gate: 29/29 items + LOC table match, 100 percent.
- `lang_go_gate.py`: 4/4 PASS (verdict committed).
- lang_go_gate verdict + audit verdict committed; the refreshed
  gate-day bundle is written by the chain re-run at this sprint's HEAD.

## Deferred to the resolver sprint (contract pre-committed in
`docs/LANGUAGES.md`)

- Resolver v1-go: dotted import paths onto discovered package
  directories; no go.mod reading inside the resolver (stdlib and remote
  modules stay unresolved even then).
- OC005 surface bindings (package-import = fail-open wildcard by
  design), OC003 import-cycle.
- OC007 coverage adapter: `go test -coverprofile` line anchors.
- OC009/OC010 manifests: `go.mod`, OSV ecosystem **Go**, pinned-only.
- Entry-point claiming via `package_clause` parsing (pack glob
  "main.go" remains the documented approximation until then).
