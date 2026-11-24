# Sprint 11 -- Track D wave 1: Java adoption (the whole process)

**Date:** 2026-11-20 · **Base:** SPRINT-10 final HEAD (Go resolver) ·
**Contract:** `docs/LANGUAGES.md` §Java (pre-approved, commit `a6cbb5f`,
written before any code -- recipe step 1) ·
**Gates:** `scripts/acceptance/lang_java_gate.py` (PASS, two fixture
corpora + JaCoCo run, 9 printed criteria) and
`scripts/acceptance/java_corpus_gate.py` (6/6 PASS, pinned real corpus);
verdicts in `docs/evidence/`.

The task: "Let's finish Java completely first" -- the pre-approved Java
contract implemented end to end in ONE sprint, exactly like the Go recipe
but with everything the contract pre-committed landing together: the
resolver v1-java AND the OC007/OC009/OC010 adapters. Java becomes the
first pack with the complete OC001-OC011 set.

## What landed, in recipe order

1. **Contract first** (already done): §Java was written and pre-approved
   before any code (commit `a6cbb5f`); this sprint implemented it
   verbatim. The verification points the contract itself mandates are
   recorded as VERIFIED blocks in §Java.1/§Java.4/§Java.5/§Java.6; the
   contract text above them is unchanged.
2. **Grammar vendored at a pinned ref.** `tree-sitter-java` **v0.23.5**
   (latest tagged at adoption; commit `94703d5a6bed02b98e438d7cad1136c01a60ba2c`,
   verified with `git ls-remote`; PyPI `tree-sitter-java==0.23.5` tracks
   the same tag). **Contract correction recorded:** the grammar lives in
   the MAIN `tree-sitter` org, not `tree-sitter-grammars` (probed -- the
   grammars-org path does not exist). This environment could reach
   upstream again, so the generic codeload-tarball fallback path in
   `scripts/vendor.sh` worked; tarball sha256 in
   `THIRD_PARTY_NOTICES.md`. Vendored shape mirrors Go: `src/parser.c` +
   `src/tree_sitter/parser.h` + LICENSE; **no external scanner**.
3. **Parser descriptor** (`src/parser_ts.c`): `java` entry with function
   (method/constructor/compact-constructor, OBS-2 definition-extent
   spans), comment (`line_comment` + `block_comment` -- Java is the
   first language needing a per-language comment query; the shared
   `(comment) @comment` pattern does not exist in this grammar),
   import (`import_declaration`), and export queries (top-level
   class/interface/enum/record names, PUBLIC modifier decides
   exportedness, checked in C against the `modifiers` child). Java has
   no `package_clause`-style entry claim (contract: file-name
   approximations only; `public static void main` claiming reserved for
   a resolver sprint). Complexity: the §Java.4 kinds, with the
   default-only `switch_block_statement_group` excluded by a
   descriptor-level `cx_filter` -- the first per-language cx refinement;
   still descriptor data, never pass code.
4. **Resolver v1-java** (`src/imports.c`, `include/oc/imports.h`): the
   class-file model -- a single-type or static qualifier resolves to the
   ONE file `a/b/C.java` under an analysis root (roots in command-line
   order, the Go/Python discipline); a wildcard `a.b.*` is a PACKAGE
   CLAIM that fans out to every `.java` file directly inside the
   package directory (including test files -- Java compiles nothing
   selectively; the deliberate contrast with `_test.go`, pinned by the
   resolver corpus where a wildcard targets `StoreTest.java`). Nested
   classes never guess: `a.b.C.D` probes `a/b/C/D.java` only, never
   falls back to `a/b/C.java`. Guards mirror Go: leading `.`/`/`,
   empty/`.`/`..` segments never resolve; JDK, classpath and everything
   outside the analyzed set stays unresolved; NO classpath, build tool
   or manifest is ever read. Surface: wildcard = package-level fail-open
   marking (the Go whole-package branch mirrored); single-type/static
   bind the class name.
5. **Pre-committed adapters.** OC007: the **JaCoCo XML** loader
   (`src/coverage.c`) -- method-level counters onto definition spans
   through the standard path rule, extended with the package-relative
   direction (real JaCoCo reports name sourcefiles package-relative;
   ambiguity still matches nothing); the coverage dispatcher now sniffs
   XML before JSON (the two families never share a prefix). OC009/OC010:
   the **pom.xml** loader (`src/supply.c`, `OC_MANIFEST_POM`,
   `OC_ECO_MAVEN`) -- bounded element-stack walk in the pyproject
   precedent; only the root-level `<dependencies>` (dependencyManagement/
   profiles/build are recorded descopes); exact pins only (`${...}`
   property references never substituted, Maven ranges and LATEST/RELEASE
   never guessed, unpinned = warn); licence from the first
   `<licenses><license><name>` ("the project clause"); OSV snapshot
   records with ecosystem "Maven" now match (`group:artifact` names).
   Manifest→pack dispatch in `src/scan.c` gained the Maven→java leg.
6. **Pack, fixtures, gates, tests.** `packs/java/pack.json` (entry
   points `Main.java` + `**/Application.java`; the full OC001-OC011
   enabled set; reasons in pack notes). Fixtures: structural corpus
   (`tests/fixtures/java`, 9 files -- module-info/package-info caveats,
   text block, both switch forms, record + compact constructor +
   lambda + anonymous class, the complexity-25 `describe` with one
   decision per recorded kind, the OC001/OC002/OC008/OC011 hooks),
   resolver corpus (`tests/fixtures/java_resolver`, 7 files + the
   pom.xml decoy), JaCoCo fixture (`tests/fixtures/java_cov/jacoco.xml`).
   Gates: `lang_java_gate.py` (9 criteria across three corpora) and
   `java_corpus_gate.py` (6 criteria, pinned google/gson
   gson-parent-2.11.0). Unit suite `tests/test_sprint11.c` (100 checks:
   resolver semantics, guards, fail-open marking, pom facts, JaCoCo
   facts, both fixture corpora end to end). Test count 26 → 27
   (`test_cli`/`test_sprint1` pack-list assertions updated, like every
   pack-count change since sprint 1).

## The pinned real corpus (gson, SPRINT-11)

`google/gson` at tag `gson-parent-2.11.0` (codeload tarball sha256-pinned
in `work/`, tag commit `3b7c2ad8ce8dd471449ccbaee6d22f64c9816a38` verified
with `git ls-remote`; Apache-2.0): 249 `.java` files, 202 analyzed under
the two Maven source roots (116 main + 86 test). **202 files, ZERO parse
errors** on the pinned grammar (generics, records, switch expressions,
text blocks, nested classes all present). Resolver realism: **820 imports
edges, all inside the corpus** (77 distinct targets) -- no edge through
classpath, gradle or any external name. Rule honesty: **OC003 == 2** --
Java allows compile-level import cycles and gson genuinely has them (a
30-file cycle around the internal/bind packages and a 2-file cycle), the
measured truth; **OC005 == 0** at scale (the empty verdict set, same
documented consequence as Go); the Surefire convention keeps OC006 off
all runner-collected test classes (0 test-class findings). Wall 0.45 s
single-core.

## Bugs found and fixed (all caught by gates/tests, before pinning)

1. **Self-referential `snprintf` UB** (`src/imports.c`,
   `resolve_java_first`): appending `.java` into the same buffer used as
   its own `%s` argument is undefined behaviour -- the fortified write
   zeroed the buffer first, so EVERY single-type/static import silently
   resolved to NULL while wildcards worked. Caught by the resolver
   fixtures (zero edges); fixed with a second buffer. The minimal probe
   first suggested "segment-count-dependent resolution" -- wrong; only
   the wildcard path masked it.
2. **Surefire suffix matcher** (`src/surface.c`): `strcmp` against a
   mid-string pointer compares the REST of the name (`"Test.java"` vs
   `"Test"` never matches). The resolver fixture alone passed for the
   wrong reason (its `StoreTest.java` had in-degree, so OC006 skipped it
   regardless); the gson corpus gate exposed it as a 112-finding
   test-class flood. Fixed with a byte-exact `memcmp` of exactly the
   suffix length; the gate's zero-flood assertion now pins it.
3. **Coverage path rule direction** (`src/coverage.c`): the existing
   suffix rule assumed report paths LONGER than analyzed paths; JaCoCo
   reports are package-relative (shorter). Extended symmetrically with
   the "/"-boundary package-tail direction (ambiguity still matches
   nothing); `files_matched` went 0 → 1 and OC007 fired on the fixture.
4. **Manifest→pack dispatch** (`src/scan.c`): the hardcoded
   npm→ts-js / everything→python mapping gained the Maven→java leg
   (OC009/OC010 would otherwise never run on pom.xml).

## Evidence

- `lang_java_gate.py`: PASS (criteria 1-6 with sub-criteria; verdict in
  `docs/evidence/lang_java_gate_verdict.json`).
- `java_corpus_gate.py`: 6/6 PASS (verdict in
  `docs/evidence/java_corpus_gate_verdict.json`).
- `test_sprint11`: 100 checks, 0 failures; full ctest 27/27 at the
  sprint HEAD.
- The full gate-day chain re-run at the final HEAD (bundle in
  `docs/evidence/gate_day/`).

## Deferred / descoped (contract-recorded, not sprint drift)

- `build.gradle(.kts)` parsing: recorded descope v1 (declarative DSL,
  no stable schema -- do not guess).
- pom.xml scope: dependencyManagement/profiles/build dependency blocks
  are not project dependencies (recorded descope).
- `public static void main` entry claiming: reserved for a resolver
  sprint (parser sees it; the pack glob does not claim by it yet).
- Go's next-sprint items (OC007 go coverprofile adapter, OC009/OC010
  go.mod) remain queued, unchanged by this sprint.
