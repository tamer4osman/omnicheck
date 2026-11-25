# Sprint 13 -- Track D wave 1: C# adoption (full scope, one sprint)

**Date:** 2026-11-20 · **Base:** `7f7c9c0` (SPRINT-11/12 evidence) ·
**Contract:** `docs/LANGUAGES.md` C# section (written FIRST; the shipped
semantics are recorded in it, with the pre-adoption shape facts probed
BEFORE any adoption code -- the recipe's verification step) ·
**Gates:** `scripts/acceptance/lang_csharp_gate.py` (**7/7 PASS**, three
fixture corpora) and `scripts/acceptance/csharp_corpus_gate.py` (**6/6
PASS**, pinned real corpus); verdicts in `docs/evidence/`.

The menu item: make the C# whole process -- the same full-scope,
one-sprint adoption Java got in SPRINT-12. Grammar, descriptor, resolver
v1-csharp, surface bindings, entry facts, coverage adapter, manifest
tier: everything landed in one sprint.

## What landed

1. **Contract first, then vendoring.** The C# section of
   `docs/LANGUAGES.md` was written before the code, and every
   pre-adoption shape guess was probed against the pinned grammar with
   an actual parse (a standalone tree dumper against the vendored
   runtime + grammar) before the descriptor was written. Amendments
   recorded in place: the ternary node is `conditional_expression`
   (the borrowed `ternary_expression` guess would not have compiled),
   modifiers are individual NAMED `modifier` nodes (no Java-style
   `modifiers` wrapper), file-scoped namespaces are a SEPARATE node
   (`file_scoped_namespace_declaration`), `static`/`global`/`equals`/
   `case`/`default`/`::` are anonymous tokens, preprocessor regions
   parse as structured nodes (never errors), top-level statements are
   `global_statement` under `compilation_unit`. Grammar:
   `tree-sitter-grammars/tree-sitter-c-sharp` pinned tag **v0.24.0**.
   Same environment restriction as Go/Java (github git protocol
   unreachable for new repositories): the vendored content is the npm
   package tarball of the pinned version, whose `src/parser.c` is
   byte-identical (sha256 cross-checked) to the PyPI sdist of the same
   version; both sha256s recorded in `THIRD_PARTY_NOTICES.md`. The
   grammar is the first vendored one WITH an **external scanner**
   (`src/scanner.c` -- interpolated strings, optional semicolons);
   `oc_add_grammar`'s scanner branch compiles it automatically.
   `scripts/vendor.sh` gained the CSHARP_REF entry (codeload fallback).
2. **Parser descriptor (`OC_LANG_CSHARP`)**. Symbols:
   `method_declaration`, `constructor_declaration`,
   `local_function_statement`, `operator_declaration` (named through
   the grammar's `operator` token field), `destructor_declaration` --
   bodies `block` or `arrow_expression_clause` (expression-bodied
   members). Interface/abstract methods are honestly NOT symbols (no
   body); property/event accessors are a recorded descope. Comments:
   the shared `comment` query. Complexity: if/for/foreach/while/do +
   `switch_section` WITH an anonymous `case` label only (a default-only
   section is not a decision -- the C-side family hook, Java's
   switch-label lesson carried over) + `switch_expression_arm` +
   `catch_clause` + `catch_filter_clause` (the `when` filter) +
   `conditional_expression` + `&&`/`||`/`??` inside `binary_expression`.
   Exports: top-level class/interface/struct/enum/record declarations
   whose modifier set carries `public`; the verified top-level walk
   climbs namespace (block and file-scoped) and preprocessor wrappers;
   type nesting is not module surface. Members are not module surface
   v1.
3. **Resolver v1-csharp** (`src/imports.c`). The specifier keeps the
   directive's own tokens: plain `A.B.C` (namespace claim), `static
   A.B.C` (type claim), `X = A.B.C` (alias claim -- the one genuinely
   ambiguous form; the alias is surface data and never narrows
   marking). `global::` is stripped (C#'s own root spelling); other
   alias-qualifications and non-identifier paths (generics, tuples)
   stay unresolved. Namespace claims resolve to the namespace
   directory under an analysis root (roots in command-line order),
   direct-children rule, no test-file exclusion (the Java rationale).
   **The corpus discovery amendment:** mono's historical mcs layout
   nests DOTTED single-directory namespaces (`System.Configuration
   .Internal/`), which the pre-adoption single-convention
   dots-to-slashes mapping resolved to ZERO edges on -- the real-corpus
   gate proved the wording wrong (the SPRINT-10 `golang.org` lesson
   repeating), and the resolver now probes the documented
   TWO-CONVENTION LAYOUT LADDER (SDK/VS slash mapping first, the dotted
   single-directory form second; first hit wins, never both; the same
   ladder on the type-file probe). Type claims probe the class file
   (`A/B/C.cs`, ladder `A.B.C.cs`); alias claims probe in the
   documented order (type file first, namespace directory second).
   **The imports relation is a SET for C#** (scoped to the branch --
   plain + static + alias records collapse into distinct edges only;
   the JS/TS/Python/Go/Java pins untouched). Guards: empty/`.`/`..`
   segments, absolute paths, non-identifier paths stay unresolved; the
   filesystem outside the analyzed set is never consulted; NO
   project/MSBuild reading by the resolver (WBS 4.x precedent; the
   decoy csproj is pinned unread by the acceptance gate). OC003
   (file-level Tarjan SCC) and OC005 (static binding marks the type's
   export; namespace and alias claims mark fail-open) flip on with the
   edges.
4. **Entry facts.** Two parse facts, the same class as the Go
   `package_clause` and the Java main fact: (a) a method named `Main`
   whose modifier set carries `static` (accessibility NOT required --
   C# accepts non-public entry points; the fact fires on the
   declaration as parsed); (b) a `compilation_unit` with a
   `global_statement` direct child -- C# 9 top-level statements ARE
   the program entry point by specification. Pack glob:
   `Program.cs`.
5. **.csproj manifest tier** (`src/supply.c`). The pom.xml bounded XML
   reader pattern: comments stripped first (a commented-out
   PackageReference must not be read as real). OC009 dep-vuln against
   the vendored OSV snapshot with ecosystem **NuGet**:
   `<PackageReference Include="X" Version="v"/>` entries (attribute or
   child element) record an `X` pin when the version is an EXACT
   literal (alphanumeric/dot/dash/plus only) -- `$(...)` property
   references, floating `*` versions and ranges are NOT pins (unpinned
   = warn, never guessed). `ProjectReference` entries are in-repo
   project links -- not OC009 material. New `OC_MANIFEST_CSPROJ` kind,
   `OC_ECO_NUGET` ecosystem ("NuGet"), routing NUGET -> the csharp
   pack. OC010 licence, honestly layered by what the format can
   declare: `<PackageLicenseExpression>` first; else
   `<PackageLicenseFile>` naming a licence file that exists among the
   discovered files in the project root; else the Go-convention
   project-root licence-FILE detection (LICENSE, LICENCE, COPYING,
   NOTICE, case-insensitive). `packages.config`/`packages.lock.json`
   remain recorded descopes.
6. **Coverlet Cobertura XML adapter** (`src/coverage.c`). Anchors are
   the `<line number hits>` records under `<class filename="A/B/C.cs">`,
   covered when `hits > 0` (Cobertura's own hit-count semantics; the
   `branch` attribute is consumed exactly as far as the line anchor
   needs it). XML dispatch by the report's ROOT ELEMENT: `<report>` ->
   JaCoCo (SPRINT-12 unchanged), `<coverage>` -> Cobertura, anything
   else falls to the JaCoCo element scan and records nothing (honest).
   Filenames map through the standard exact-then-unique-suffix rule;
   a malformed record fails loud.
7. **Pack + fixtures + tests.** `packs/csharp/pack.json` v0.1.0 -- all
   eleven passes enabled. Fixtures: `tests/fixtures/csharp`
   (structural, the Go-spike shape: OC001/OC002/OC004/OC006/OC008/OC011
   exactly 1/1/1/4/2/1, Program.cs claimed by the static-Main fact),
   `tests/fixtures/csharp_resolver` (10 .cs files + the decoy
   MyApp.App.csproj: 11 contracted edges, the A/B namespace cycle,
   subdirectory exclusion, the alias-namespace claim reaching the
   subdirectory, System.Linq and the external coordinate unresolved),
   `tests/fixtures/csharp_manifest` (csproj tier + Cobertura corpus:
   alpha with a licence expression, beta without; Grade at complexity 6
   uncovered -> CRAP 42), `tests/fixtures/supply/osv_snapshot_csharp`
   (two NuGet records). `test_sprint13.c` (**153 checks**): resolver
   units (namespace/type/alias views, the ladder, guards, fan-out +
   edge-set dedupe), marking units (namespace fail-open, static
   binding, subpackage exclusion, entry facts), csproj units (comment
   stripping, pins vs non-pins, the child-element version form, the
   licence layering), Cobertura units (anchors, hits>0, suffix rule,
   fail-loud, the JaCoCo dispatch still owning `<report>`), and E2E
   over all three corpora with byte-identical reruns. Suite: **29
   tests per cell**; the pack-count pins in test_sprint1/test_cli
   superseded to 7.
8. **Gates.** `lang_csharp_gate.py` -- 7 criteria over THREE corpora
   (structural, resolver, manifest+coverage), mirroring the Java gate
   closeout shape. `csharp_corpus_gate.py` (recipe step 8, m2/m3
   pattern): the pinned corpus is the Debian `mono`
   6.8.0.105+dfsg orig tarball (sha256 in `work/` + sidecar) -- the 26
   mcs/class libraries of >= 40 product .cs files that parse with ZERO
   errors under the pinned grammar at pin time: **3,702 real-world C#
   files**. 6/6 PASS: zero parse errors (the two `.js` AJAX resource
   files sharing the tree are analyzed by the JS pack -- counted, not
   silent), byte-identical reruns, **89,313 imports edges all INSIDE
   the corpus (1,959 distinct targets, zero outside)**, OC003 == 13 --
   REAL compile-time-legal import cycles, OC005 == 0 (the import-driven
   marking surface), OC011 == 4, counts pinned, wall 12.7 s < 60 s
   budget. No github access needed -- the Debian archive ecosystem is
   reachable where the git protocol is not (the same provenance story
   as the JDK's `openjdk-21-source` deb).
9. **Docs.** ARCHITECTURE languages/resolver/surface/coverage sections
   synced; README derived statistics refreshed by `audit_docs.py`
   (127 files / 29,544 LOC, packs 7, 29 tests per cell, 6 languages);
   files touched by this sprint are clang-format clean (clang-format
   19.1.7; the tree-wide pre-existing violations remain the recorded
   debt of SPRINT-11's note).

## Evidence

- `lang_csharp_gate.py`: **7/7 PASS** across the three fixture corpora
  (verdict committed).
- `csharp_corpus_gate.py`: **6/6 PASS** on the pinned mono corpus
  (verdict committed) -- 3,702 files, zero parse errors, byte-identical,
  89,313 intra-corpus edges, OC003 = 13 REAL cycles, counts pinned,
  12.7 s wall.
- Default cell: **29/29** ctest green (including test_sprint13's 153
  checks); the full 4-cell matrix is re-proven by the chain re-run at
  this sprint's HEAD (gate-day bundle below).

## Deferred (recorded descopes, not gaps)

- Property/event accessors as symbols: their bodies belong to their
  property/event declaration (honest non-symbols).
- Same-namespace usage modeling for OC005 (C# needs no using directive
  for same-namespace types): the import-driven marking records what
  the graph can see; call-graph modeling is out of scope v1 (the
  Java same-package precedent). The csharp_manifest corpus PINS the
  consequence (Lib.cs used only in-namespace is OC006 material, not
  OC005).
- `packages.config` / `packages.lock.json` manifest parsing; OpenCover
  XML and MSBuild binary `.coverage` coverage formats: no honest line-
  anchor model for a binary stream here, and the legacy XML schema is
  a recorded descope.
- Conditional-compilation evaluation: both branches of an `#if` are
  parsed and analyzed as written (over-reports rather than hides --
  the Go build-tags class of recorded descope).
