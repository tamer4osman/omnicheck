# Sprint 14 -- Track D closeout: the recorded descopes close

**Date:** 2026-11-21 · **Base:** SPRINT-13 final HEAD (`703527d`, the gate-day
closeout) · **User direction:** "finish the rest of previous languages before
we go to new language" -- the recorded descopes of the three adopted
Track D wave 1 languages (Go, Java, C#) close, contract-first, in one
sprint. The descope ledger is the v5 addendum's "Deferred" list.

## Contracts first (one commit, `18a9f68`)

Every closure below landed as a `docs/LANGUAGES.md` amendment BEFORE any
code (the recipe's step 1). Two items were triaged OUT and re-recorded as
DELIBERATE KEEPS with sharpened reasons (C# §7): `#if` evaluation
(evaluating without the real build configuration would GUESS the active
branch -- and a wrong guess HIDES code, the direction the honesty
invariants forbid) and the MSBuild binary `.coverage` format (a
proprietary binary stream with no open spec -- a fabricated parse is
worse than the warn).

## Go -- both §Go.2/§Go.8 descopes close

- **Named function-value assignments are symbols** (§Go.2, the SPRINT-9
  "reserved for the resolver sprint" flag settles). Exactly three shapes,
  each with the `func_literal` as the WHOLE right side: `f := func() {...}`
  (short_var_declaration), `f = func() {...}` (assignment_statement),
  `var f = func() {...}` (var_spec, plain or grouped). The symbol name is
  the assigned identifier; the canonical span is the whole defining
  assignment (OBS-2 unchanged); complexity is counted over the literal
  body and STILL counts inward toward the enclosing function (the standing
  additive-safe discipline). Multi-name left sides, the blank `_`, and
  compound operators (`+=`) are never promoted -- deciding would be a
  guess. Anonymous literals (`go`/`defer`/arguments) stay non-symbols.
- **Build constraints are evaluated** (§Go.8) under the pack-DECLARED
  build context -- `packs/go/pack.json` `"build_context"`
  (linux/amd64/gc/cgo=false, no extra tags): a deterministic, committed
  analysis target, NOT the analysis host's uname (byte-identical reports
  must not depend on the analyzing machine; `go vet`'s host-context
  behavior is the recorded contrast). Both directive forms: the modern
  `//go:build` expression (`||`/`&&`/`!`/parens, first line before the
  package clause wins) and legacy `// +build` lines (space=OR,
  comma=AND, `!` negates; lines OR'ed); `go:build` wins when both exist
  (go1.17 semantics). Tag truth: GOOS/GOARCH vocabularies compare against
  the context, `unix` derives from the declared GOOS, `gc`/`gccgo`
  compare against the declared compiler, `cgo` is the declared bit,
  declared extra tags compare literally; ANY OTHER tag (release tags,
  `ignore`, custom) is FALSE. A file whose constraint is false LEAVES the
  analyzed set (what `go build` does) and the report meta counts it
  (`files_excluded_build_tag`) -- excluded, never silent. A malformed
  expression is NO constraint (over-report; it never hides on a parser
  weakness). FILENAME-suffix constraints (`foo_linux.go`) stay OUT of
  scope (the one documented boundary -- they keep the over-report
  direction). Implementation: new `src/buildtag.c` (+header), pack
  context fields, the scan-pipeline filter, the report meta field.

## Java -- three descopes close

- **Abstract and interface methods are symbols.** The declaration itself
  is the definition extent; complexity exactly 1 (no body, no decisions).
  The query is the negated-field form (`!body`) so a bodyful method can
  never match twice. JaCoCo never carries counters for them (no
  executable lines) -- honestly uncovered, comp-1 CRAP minimal. Members
  stay non-surface. `annotation_type_element_declaration` members remain
  unmodeled (recorded here, not silent).
- **`build.gradle` / `build.gradle.kts` are read** (the "no stable
  schema" descope settles via a BOUNDED reader, the pom.xml discipline
  applied to the DSLs): a dependency is recorded only when a dependency
  configuration keyword (api/implementation/compileOnly/runtimeOnly/
  test*/annotationProcessor and the deprecated compile/runtime) is
  followed -- within the STATEMENT LINE -- by an exact
  `group:artifact:version` literal (one optional `( )` wrapper;
  `platform(...)` BOM nodes skipped honestly) or the complete one-line
  map form (group/name/version, all three quoted). Exact literals are
  pins (LATEST/RELEASE/-SNAPSHOT and ranges are NOT); triplet-shaped but
  non-exact coordinates record UNPINNED (warn per the standing rule);
  anything else records nothing. OC009 ecosystem Maven; OC010 unchanged
  (the pom.xml tier; gradle files may live in subprojects where root
  probing would read the wrong root). Three reader bugs were caught by
  the fixture tests before landing: the pin check ran AFTER the
  name/version cut (every pin was unpinned), the fallback window crossed
  statements (a failed keyword stole a LATER line's dependency), and the
  `<packages>`-style wrapper problem's gradle analogue (keyword boundary
  checks missed uppercase, so `compile` matched inside `compileOnly`).
- **Package-mate OC005 marking.** Same-package types need NO import, so
  the import-driven marking produced FALSE OC005 positives for types used
  only by package-mates. Closed with the standing fail-open discipline: a
  `.java` file's exports are marked used when ANY other `.java` file
  lives in the same directory (same package by the class-file model).
  Over-marking, never fabricating an unused verdict; the OC005
  verdict-set consequence is UNCHANGED (the pass's precondition still
  bounds the set; the gates still assert it).

## C# -- four descopes close, two stay (deliberate)

- **Property/event accessors are symbols** -- and the promotion closes a
  real accounting hole: properties are not symbols and class members have
  no enclosing function, so accessor decisions were previously counted
  toward NOTHING. An `accessor_declaration` with a real body records a
  symbol named `Declaring.get` / `Prop.set` / `Evt.add` / `Evt.remove`
  (the declaring member's `name` field + the accessor's keyword token,
  verified -- the accessor's `name` field IS the keyword); the canonical
  span is the whole accessor declaration; complexity counts the accessor
  body (inclusive discipline unchanged). Auto-property accessors
  (`get;`) stay non-symbols (no honest body). Expression-bodied
  PROPERTIES (`int Q => 1;`) have no accessor_list and stay non-symbols.
- **`packages.config` and `packages.lock.json` are read** (NuGet tiers,
  own manifests wherever discovered): packages.config `<package id="X"
  version="1.2.3">` pins on the exact-literal rule (ranges/absent
  versions record unpinned); the lockfile records the RESOLVED pin --
  the `requested` range is NEVER substituted -- and both
  `type: "Direct"` and `type: "Transitive"` entries (transitive pins are
  real build inputs, the go.mod `// indirect` precedent). Attribute
  lookup is bounded to the tag's own text (an id-less tag must never
  steal a later tag's `id="..."`; the `<packages>` wrapper is not a
  dependency tag). OC010 skips both (no licence field -- the pom/.csproj
  licence layers unchanged).
- **OpenCover XML adapter** (the "different schema" descope settles --
  the schema is open and line-anchored, the Go coverprofile-block shape):
  root `<CoverageSession>` joins the root-element dispatch; anchors are
  the `<SequencePoint>` records' INCLUSIVE `sl..el` line range, covered
  when `vc > 0`; each record joins `<File uid fullPath>` through
  `fileid`; a record whose fileid names no known file records nothing.
  Malformed records fail loud.
- **Namespace-mate OC005 marking** (the Java same-package precedent,
  closed symmetrically): each file's DECLARED dotted namespace(s) are
  recorded as a parse-time hint (the Go package_clause channel -- block
  namespaces join nesting with dots, preprocessor wrappers climbed, a
  file may declare several), and a file's exports are marked used when
  ANY other analyzed file declares the same namespace. Fail-open; the
  verdict-set consequence unchanged; the declared namespace is never
  cross-checked against the directory (unchanged).
- **KEPT (deliberate, sharpened):** `#if` evaluation and MSBuild binary
  `.coverage` -- see the contract §7 text; decisions, not gaps.

## Tests and evidence

- `tests/test_sprint14.c` (the 29th suite; matrix cells now 29 tests):
  the build-constraint evaluator (both directive forms, vocabulary truth
  table, precedence, malformed-over-report, header-region rules), the
  three Go promotion shapes + rejections + complexity, the Java bodyless
  symbols, the C# accessor names/complexity, the gradle/packages.config/
  lock.json tiers, the OpenCover anchors, and both mate markings; plus
  `tests/fixtures_sprint14/` (gotags/gradle/nuget/opencover).
- LSan caught a test-side leak through the sanitize cell (the parser's
  TSQuery heap allocations; fixed by freeing the parser in the three
  parser units) -- the sanitize cell did its job.
- Language gates re-run at the sprint HEAD: `lang_go_gate` 7/7,
  `go_corpus_gate` 6/6 (RE-PINNED: 439 analyzed + **237 build-tag-
  excluded** of the 676 corpus files under the declared context -- the
  exclusion itself is now a pinned criterion; edges 11,719 and targets
  377 unchanged), `lang_java_gate` 8/8, `java_corpus_gate` 6/6 (gson
  counts unchanged under the comp-1 symbols), `lang_csharp_gate` 7/7,
  `csharp_corpus_gate` 6/6 (mono, 3,704 files: accessor symbols +
  namespace-mate marking shift nothing the gate pins).
- Gate-day chains re-recorded FRESH at the sprint HEAD: **M1 7/7**
  (tree-clean, matrix 4x29 from scratch, m1-replay fresh clone +
  `vendor.sh` + ctest + exit-2 contract + 10k perf, harnesses m2-m5,
  audit one digest, docs 31/31 + LOC match 129 files / 31,253 lines,
  package smoke) and **M5 6/6** (matrix 4x29, harnesses, audit, docs,
  package). The README's derived LOC table was re-derived (the docs gate
  caught the drift, as designed).

## Where the ledger stands

The recorded-descopes ledger for Go/Java/C# is now EMPTY except the two
deliberate keeps (C# `#if`, MSBuild binary `.coverage`) and the per-
language boundary notes (Go filename-suffix constraints; Java annotation
elements; C# expression-bodied properties). Track B items B1/B2 still
await an explicit user decision. The next languages (Ruby, Rust) start
from contract tables that do not exist yet -- write the contract first.
