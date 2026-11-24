# Sprint 12 -- Track D wave 1: Java adoption (full scope, one sprint)

**Date:** 2026-11-20 · **Base:** `65337a7` (SPRINT-11 docs) ·
**Contract:** `docs/LANGUAGES.md` Java section (pre-approved at SPRINT-9;
the shipped semantics were written into it FIRST -- recipe step 1) ·
**Gates:** `scripts/acceptance/lang_java_gate.py` (**7/7 PASS**, three
fixture corpora) and `scripts/acceptance/java_corpus_gate.py` (**6/6
PASS**, pinned real corpus); verdicts in `docs/evidence/`.

The menu item: adopt Java completely so the wave can move on to the next
languages. Unlike Go's spike-then-resolver split, the WHOLE pre-approved
contract landed in one sprint: grammar, descriptor, resolver v1-java,
surface bindings, entry fact, coverage adapter, manifest tier.

## What landed

1. **Contract first, then vendoring.** The Java section of
   `docs/LANGUAGES.md` was updated to the shipped semantics before the
   code: the pre-adoption guesses that needed verification are recorded
   in place -- the ternary node is `ternary_expression` (not
   `conditional_expression`), import declarations carry
   `scoped_identifier` children (not `scoped_type_identifier`), switch
   labels are NAMED `switch_label` nodes whose `case`/`default` head is
   an anonymous token. Grammar: `tree-sitter-grammars/tree-sitter-java`
   pinned tag **v0.23.5**. Same environment restriction as Go (github
   git protocol unreachable for new repositories): the vendored
   `src/parser.c` came from the PyPI sdist of the pinned version, and
   the grammar's own `src/tree_sitter/parser.h` (ABI-14, ships
   `TSFieldMapSlice`) came from the npm package of the SAME version --
   the parser.c sha256 is identical across both provenances; the header
   provenance is recorded in `THIRD_PARTY_NOTICES.md`. No external
   scanner. `scripts/vendor.sh` gained the JAVA_REF entry (codeload
   fallback).
2. **Parser descriptor (`OC_LANG_JAVA`)**. Symbols:
   `method_declaration` (body `block`), `constructor_declaration` (body
   `constructor_body`), `compact_constructor_declaration` (body `block`)
   -- abstract and interface methods are honestly NOT symbols (no
   definition extent). Comments: `line_comment` + `block_comment` (the
   per-language comments-query slot). Complexity: if/for/while/do +
   `switch_rule` + `switch_block_statement_group` WITH a `case` label
   only (a default-only group is not a decision -- checked via the
   named `switch_label` child) + `catch_clause` + `ternary_expression`
   + `&&`/`||` inside `binary_expression`. Exports: top-level
   class/interface/enum/record declarations (the query constrains every
   capture to direct `program` children); the `public` modifier decides
   exportedness; members are not module surface v1.
3. **Resolver v1-java** (`src/imports.c`). Single-type and
   static-qualifier imports map VERBATIM to `a/b/C.java` (exact file
   probe, roots in command-line order); static imports record the
   QUALIFIER (the member segment is stripped in C; a static path
   without a qualifier stays unresolved); package wildcards `a.b.*`
   claim the package directory (whole-package fan-out to every
   direct-child `.java` file -- the direct-children rule, so
   subpackages are never claimed; there is no test-file exclusion
   because Java test trees follow build-layout conventions, not import
   semantics). Guards: leading/trailing dot, empty dot-segment, slash
   inside, absolute and empty paths stay unresolved; nested classes
   probe exactly `a/b/C/D.java` (the class-file model is the only
   mapping v1 supports). **The imports relation is a SET for Java**: one
   file can name the same target through single-type + static qualifier
   + package wildcard, and the three records collapse into ONE edge --
   scoped to the Java branch, the JS/TS/Python/Go pins stay untouched.
   Classpath and build-tool resolution stay out of scope (WBS 4.x
   precedent); the decoy pom.xml is never read by the resolver (pinned
   by the acceptance gate). OC003 (file-level Tarjan SCC) and OC005
   (single-type binding marks the class, wildcard marks the package
   fail-open) flip on with the edges.
4. **Entry fact.** The parsed `public static void main` fact (method
   named `main`, modifiers carrying both `public` and `static`) claims
   the file as an entry point -- the same class of cheap parse fact as
   the Go `package_clause` and the shebang hint. Pack globs:
   `Main.java`, `**/Application.java`.
5. **pom.xml manifest tier** (`src/supply.c`). A bounded XML reader
   (comments stripped FIRST -- a commented-out dependency must not be
   read as a real one): OC010 licence from the first
   `<license><name>` in the project's `<licenses>` block; OC009
   dependencies as `group:artifact` pins when the version is an exact
   literal -- version RANGES, `${property}` references, LATEST/RELEASE
   and `-SNAPSHOT` are NOT pins (unpinned = warn, never guessed). New
   `OC_MANIFEST_POM_XML` kind, `OC_ECO_MAVEN` ecosystem ("Maven"),
   routing MAVEN -> the java pack.
6. **JaCoCo XML adapter** (`src/coverage.c`). Anchors are the
   SOURCEFILE line records (`<line nr mi ci>` under
   `<package>/<sourcefile>`), covered when `ci > 0`; the pre-adoption
   wording "method-level counters map onto definition spans" is
   amended in the contract (method counters carry totals without line
   identities -- the line records are the finest honest granularity).
   Package/sourcefile names join to `a/b/C.java` and map through the
   standard exact-then-unique-suffix rule; comments are stripped first;
   a malformed `<line>` record fails loud.
7. **Pack + fixtures + tests.** `packs/java/pack.json` v0.1.0 -- all
   eleven passes enabled. Fixtures: `tests/fixtures/java` (structural,
   the Go-spike shape: OC001/OC002/OC004/OC006/OC008/OC011 exactly
   once/4/2/1), `tests/fixtures/java_resolver` (9 .java files + the
   decoy pom.xml: 6 contracted edges, a/b cycle, subpackage exclusion,
   the external coordinate unresolved), `tests/fixtures/java_manifest`
   (pom tier + JaCoCo corpus), `tests/fixtures/supply/osv_snapshot_java`
   (two Maven records). `test_sprint12.c` (140 checks): resolver units
   (class-file probe, guards, wildcard views, fan-out + edge-set
   dedupe), marking units (single-type/wildcard/subpackage/main-fact),
   pom units (comment stripping, pins vs non-pins, licence clause),
   JaCoCo units (anchors, ci>0, suffix rule, fail-loud), and E2E over
   all three corpora with byte-identical reruns. Suite: **28 tests per
   cell**; the pack-count pins in test_sprint1/test_cli superseded to 6.
8. **Gates.** `lang_java_gate.py` -- 7 criteria over THREE corpora
   (structural, resolver, manifest+coverage), mirroring the Go gate
   closeout shape. `java_corpus_gate.py` (recipe step 8, m2/m3
   pattern): the pinned corpus is Debian `openjdk-21-source`
   21.0.13+9-1~deb13u2 (sha256 in `work/`; the deb ships the JDK
   `src.zip` which the gate unpacks) -- the **java.base module: 3,514
   real-world JDK library files** (java.lang, java.util, java.io,
   jdk.internal, ...). 6/6 PASS: zero parse errors on the whole module,
   byte-identical reruns, **84,887 imports edges all INSIDE the corpus
   (2,657 distinct targets, zero outside)**, OC003 == 10 -- REAL
   compile-time-legal import cycles (the 2-file SCCs in
   `jdk/internal/classfile` and the giant java.lang/java.util/java.io
   core SCC, >512 files at file granularity -- the exact point of the
   resolver sprint, and the honest contrast to Go where import cycles
   are compile errors), OC011 == 21 (real PEM headers in
   CertificateFactory doc examples and secret-looking assignments),
   counts pinned, wall 24.6 s < 60 s budget.
9. **Docs.** ARCHITECTURE languages section synced; README derived
   statistics refreshed by `audit_docs.py` (123 files / 26,867 LOC,
   packs 6, 28 tests per cell); files touched by this sprint
   clang-format clean (clang-format 19.1.7).

## Evidence

- `lang_java_gate.py`: **7/7 PASS** across the three fixture corpora
  (verdict committed).
- `java_corpus_gate.py`: **6/6 PASS** on the pinned java.base module
  (verdict committed) -- 3,514 files, zero parse errors, byte-identical,
  84,887 intra-corpus edges, OC003 = 10 REAL cycles, counts pinned,
  24.6 s wall.
- Default cell: **28/28** ctest green; the full 4-cell matrix is
  re-proven by the chain re-run at this sprint's HEAD (gate-day bundle
  below).

## Deferred (recorded descopes, not gaps)

- `build.gradle(.kts)` manifest parsing: declarative DSL, no stable
  schema for dependency pins -- do not guess it (standing descope).
- Same-package usage modeling for OC005 (Java needs no imports for
  intra-package use): the import-driven marking records what the graph
  can see; call-graph modeling is out of scope v1.
- Abstract/interface methods as symbols: no body, no definition extent
  (honest non-symbols).
