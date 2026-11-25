# Language contracts (Track D, "register, not train")

This file holds the per-language adoption contracts for the north-star goal
(analyze most popular programming languages). A language lands by the fixed
recipe (`OmniCheck_Handoff.md` §7.2): **contract first** (this file, mirroring
the contract style of `docs/ARCHITECTURE.md`), then vendored grammar at a
pinned ref, parser descriptor, `pack.json`, fixtures, acceptance -- in that
order, never code ahead of its contract. Every never-guess policy of
`docs/ARCHITECTURE.md` applies to every language here unchanged.

Each language contract fixes the same six points:

1. **Grammar** -- upstream repo + pinned ref, vendoring provenance, external
   scanner or not. Vendored sources are never edited (same rule as
   `third_party/omnidecide/`).
2. **Functions & spans** -- which node kinds are symbols, and the ONE canonical
   span (definition extent, OBS-2 contract, `docs/ARCHITECTURE.md`).
3. **Comments** -- comment node kinds (OC002 consumes them verbatim).
4. **Complexity** -- the decision-kind table shipped with the descriptor
   (register-not-train), plus operator tokens counted inside
   `binary_expression`-shaped nodes. Nested closures count inward (inclusive).
5. **Imports & resolver semantics** -- what the parser records raw, and which
   specifiers the resolver may turn into edges. An edge always means "the
   target is part of this analysis" -- a specifier that cannot be resolved
   INSIDE the analyzed file set stays unresolved, never guessed.
6. **Surface, entry points, coverage, manifests** -- export candidates and
   import bindings (module-surface feeds OC005/OC006), pack `entry_points`
   globs, the coverage adapter (OC007; OC004 stays the no-coverage proxy until
   one exists), and manifest support (OC009/OC010, OSV ecosystem mapping,
   pinned-only).

Adoption status (updated per sprint):

| Language | Wave | Status |
| --- | --- | --- |
| JavaScript / TypeScript / TSX / Python | 0 (Sprints 1-5) | shipped, contracts in `docs/ARCHITECTURE.md` |
| **Go** | 1 | **adopted SPRINT-9 (spike), resolver v1-go SPRINT-10, coverage + manifests SPRINT-11: all eleven passes enabled (OC001-OC011); `go test -coverprofile` adapter and `go.mod` manifests (OSV ecosystem Go) shipped; nothing pre-committed remains** |
| **Java** | 1 | **adopted SPRINT-12 (full scope in one sprint): grammar v0.23.5 vendored, OC_LANG_JAVA descriptor, resolver v1-java (single-type -> class file, wildcard -> package-dir fan-out), OC003/OC005 with the bindings, JaCoCo XML adapter (OC007), pom.xml manifests (OC009 OSV Maven / OC010 licence clause), entry points as contracted file-name approximations; all eleven passes enabled** |
| **C#** | 1 | **adopted SPRINT-13 (full scope in one sprint): grammar v0.24.0 vendored (external scanner), OC_LANG_CSHARP descriptor, resolver v1-csharp (namespace -> dir fan-out, static -> type file, alias -> ordered probe), OC003/OC005 with the bindings, Coverlet Cobertura XML adapter (OC007), .csproj manifests (OC009 OSV NuGet / OC010 licence), `static Main` + top-level-statements entry claiming; all eleven passes enabled** |
| **Rust** | 1 | **adopted SPRINT-15 (full scope in one sprint): grammar v0.24.0 vendored (external scanner), OC_LANG_RUST descriptor, resolver v1-rust (mod-tree channel -> module file, `crate::`/`self::`/`super::` use-channel -> anchor probe with item-reading pop ladder), OC003/OC005 with the bindings, LCOV adapter (OC007), Cargo.toml/Cargo.lock manifests (OC009 OSV crates.io / OC010 licence layering), `fn main` + pack-glob entry claiming; all eleven passes enabled** |
| **Ruby** | 2 | **adopted SPRINT-16 (full scope in one sprint): grammar v0.23.1 vendored (external scanner), OC_LANG_RUBY descriptor, resolver v1-ruby (require_relative channel -> requiring file's directory, bare require -> documented load-root conventions), OC003/OC005 with the bindings, SimpleCov JSON adapter (OC007), Gemfile/Gemfile.lock manifests (OC009 OSV rubygems / OC010); all eleven passes enabled** |
| **PHP** | 2 | **adopted SPRINT-19 (full scope in one sprint): mixed php+html grammar v0.24.2 vendored (external scanner), OC_LANG_PHP descriptor, resolver v1-php (composer.json PSR-4/PSR-0 use channel + require/include static-string channel), OC003/OC005 with the bindings, Clover XML adapter (OC007), composer.json/composer.lock manifests (OC009 OSV packagist / OC010 licence field), bodyless interface/abstract symbols, index.php + **/bin/* entry claiming; all eleven passes enabled** |
| **C** | 2 | **adopted SPRINT-20 (full scope in one session, wave-2): grammar v0.23.4 vendored (NO external scanner), OC_LANG_C descriptor (declarator-chain name resolution, prototypes are surface not symbols), resolver v1-c (quoted include -> including file's directory, angle system include never resolves, macro path records nothing), OC003/OC005 with the bindings, gcovr Cobertura XML through the existing reader (OC007), NO manifest tier (the honest empty supply tier), `main`-definition parse-fact entry claiming, .c + .h extensions (the known future boundary: content sniffing landed with the cpp pack SPRINT-21); all eleven passes enabled** |
| **C++** | 2 | **adopted SPRINT-21 (full scope in one session, wave-2 close-out): grammar v0.23.4 vendored (external scanner; the C grammar inlined at generation time), OC_LANG_CPP descriptor (the C declarator-chain walk extended: field_identifier, qualified_identifier name field, destructor/operator names as-written; templates/namespaces climb; anonymous namespaces are internal linkage), resolver v1-cpp (the v1-c include channel shared verbatim; the using family records NOTHING in v1), OC003/OC005 with the bindings, gcovr Cobertura through the existing reader (OC007), NO manifest tier in v1 (vcpkg/conan are not canonical pins -- the honest empty supply tier), free-standing `main`-definition parse-fact entry claiming, the .h CONTENT SNIFF + project-root default with the header_routing report block, .cpp/.cc/.cxx/.c++/.hpp/.hh/.hxx/.h extensions; all eleven passes enabled** |
| **Kotlin** | 3 | **adopted SPRINT-22 (full scope in one session, wave-3 opens): grammar fwcd v0.3.8 vendored (external scanner; the canonical community grammar), OC_LANG_KOTLIN descriptor (function_declaration WITH a body; body-less abstract/interface members are NOT symbols; accessors with a body ARE symbols named Prop.get/Prop.set -- the C# precedent), resolver v1-kotlin (the DECLARED-FACT resolver: imports resolve against the RECORDED package_header clauses and top-level declaration names of the analyzed set -- wildcard fan-out, single requires the declared symbol, the ambiguous member shape keeps a NULL bind; without hints everything stays unresolved), OC003/OC005 with the bindings, gcovr Cobertura through the existing reader (OC007; Kover's own XML is a recorded descope), NO manifest tier in v1 (gradle files are not pins -- the honest empty supply tier), top-level `main`-definition parse-fact entry claiming, .kt/.kts/.ktm extensions; all eleven passes enabled** |

---------------------------------------------------------------------------


## Go language contract (adopted SPRINT-9 spike; resolver v1-go SPRINT-10; coverage + manifests SPRINT-11)

### 1. Grammar

- Upstream: `tree-sitter-grammars/tree-sitter-go`, pinned tag **v0.25.0**
  (the latest tagged grammar at adoption time; PyPI binding package
  `tree-sitter-go==0.25.0` tracks the same tag).
- Vendored at `third_party/tree-sitter-go/` (sources: `src/parser.c` +
  `src/tree_sitter/parser.h`; MIT licence). Provenance recorded in
  `THIRD_PARTY_NOTICES.md`, including the fetch path: this build
  environment cannot reach `github.com` over the git protocol for new
  repositories, so the vendored content was taken from the PyPI sdist of
  the pinned version (sdist sha256 recorded there) -- same upstream
  sources, honestly labelled. `scripts/vendor.sh` gained a codeload
  tarball fallback so a normal environment re-vendors from the pinned tag
  directly. Never edited after vendoring.
- **No external scanner** (the Go grammar is a single generated `parser.c`).

### 2. Functions & spans

- Symbol kinds this sprint: `function_declaration` (name: `identifier`) and
  `method_declaration` (name: `field_identifier`). Both are OC_NODE_FUNCTION
  symbols; the canonical span is the whole defining construct (definition
  extent) exactly like every other language.
- **Named function-value assignments ARE symbols** (closed SPRINT-14: the
  SPRINT-9 "reserved for the resolver sprint" flag, then a RECORDED descope,
  is settled). A `func_literal` assigned to a NAME is a named defining
  construct the graph can honestly attach: the symbol name is the assigned
  identifier, the canonical span is the whole defining assignment (the
  `short_var_declaration` / `assignment_expression` / `var_spec` -- the
  definition extent, OBS-2 unchanged). Exactly three shapes, all deterministic:
  `f := func() {...}`, `f = func() {...}`, `var f = func() {...}`. A
  multi-name left side (`a, b := ...`) is NEVER promoted -- deciding which
  name carries the function would be a guess -- and neither is the blank
  `_` (discarded, not a name), nor a compound operator (`f += func(){}`
  is not a definition). Anonymous literals (`go func()
  {...}()`, `defer`, call arguments) stay non-symbols. Complexity: the
  literal body's decisions count toward the NEW symbol, and the enclosing
  function's inclusive walk still counts them (the standing additive-safe
  discipline -- the literal is inside the enclosing body). OC001/OC004/OC007
  see the promoted symbol like any other function.
- Anonymous `func_literal` bodies (no assigned name) are NOT symbols: no
  name the graph could honestly attach. Their bodies still count toward the
  ENCLOSING function's complexity (inclusive-by-design, additive-safe).
- Generic functions (`[T any]` type parameters) parse cleanly and follow the
  same span/complexity rules; no special-casing.

### 3. Comments

- Node kind `comment` covers both `//` line and `/* */` block comments;
  doc comments are plain comments (no godoc-tree model -- OC002 marker scan
  applies verbatim, same as every language).

### 4. Complexity (cyclomatic, gocyclo-aligned, register-not-train)

Decision kinds shipped with the descriptor:

- `if_statement` (Go's `else if` is a nested `if_statement`, so each branch
  increments by itself; the grammar has no `else_clause` node),
- `for_statement` (Go's only loop form; `range` is inside it),
- `expression_case`, `type_case`, `communication_case` (one per case clause
  of expression switches, type switches and select statements),
- inside `binary_expression`: the operator tokens `&&` and `||`.

No ternary exists in Go. The count is `1 + decisions`, computed over the
function body exactly as for the JS family (nested func literals count
inward). OC004 threshold semantics and the no-coverage CRAP proxy
(`comp^2+comp`) are unchanged and language-blind.

### 5. Imports & resolver semantics (v1-go shipped, SPRINT-10)

- The parser records one raw specifier per `import_spec`
  (`path: interpreted_string_literal`, outer quotes stripped) AND one
  import binding per `import_spec`: the alias when aliased, NULL otherwise
  (plain, blank `_` and dot `.` imports). The alias is surface data -- it
  does NOT narrow marking (see below).
- **Resolver v1-go (SPRINT-10, shipped):** a specifier resolves iff the
  package directory it names exists among the analyzed file set. Exact
  semantics, all deterministic:
  - the import path maps VERBATIM onto a slash path below an analysis
    root: the candidate directory is `<analysis-root>/<import-path>`
    exactly as written, dots included. That is Go's own mapping -- the
    import path equals the GOPATH-src-relative directory, and dotted
    hosts ("golang.org") are literal directory names. (Precision over
    the SPRINT-9 wording "dotted import paths", which was ambiguous:
    a Python-style dot-to-slash conversion would destroy "golang.org"
    and resolved ZERO edges on the real-corpus gate; the amendment is
    recorded here and in the resolver's contract block.) Roots are
    probed in command-line order (first root whose candidate contains
    discovered `.go` files wins -- the same root-order discipline as
    the Python absolute-import resolver);
  - DIRECT-CHILDREN rule: the package is the discovered NON-TEST `.go`
    files directly inside that directory. Subdirectories are separate
    packages and are never claimed by the parent's specifier, and the
    directory's `_test.go` files never join an import -- Go compiles a
    package import without them (they belong to the package's own test
    binary), and an external test package (`package foo_test` sharing
    `foo/`) is not the imported package. Measured, not assumed: letting
    the fan-out target test files fabricated SCCs between same-directory
    test files that merely import their own package -- four false OC003
    findings on the real-corpus gate, fixed before pinning; the amended
    rule yields zero OC003 findings there, as Go's compiler demands;
  - the target of resolution is the WHOLE PACKAGE: every non-test
    direct-child `.go` file receives an `imports` edge from the importing
    file (Go compiles whole packages, so "the target is part of this
    analysis" holds file-wise; a package file's in-degree reflects
    package imports);
  - never-guess guards: specifiers that start with `.` or `/`, contain
    `.`/`..` or empty SEGMENTS stay unresolved (no path escapes); the
    filesystem outside the analyzed file set is never consulted;
  - single-target view: `oc_resolve_specifier` returns the package's
    lexicographically-first discovered file (graph order is sorted, so
    this is deterministic); the edge pass and the surface build enumerate
    the whole package instead.
- **No manifest reading** (WBS 4.x precedent, unchanged): nearest-`go.mod`
  module-prefix stripping is EXPLICITLY out of scope for v1, and the
  resolver never reads `go.mod`. Consequence, pinned by the acceptance
  gate: for a module whose declared path is `mycorp`, the specifier
  `mycorp/store` stays unresolved even when `store/` exists in the tree
  -- stripping the prefix would require the manifest. Stdlib (`fmt`) and
  remote (`github.com/...`) specifiers point outside the analyzed set and
  stay unresolved for the same reason.
- **OC003 (import-cycle) flips on** with the edges: file-level Tarjan SCC
  over the fan-out edges; a package cycle appears at file granularity
  (every file of both packages joins the SCC), one finding per SCC.
- **OC005 (unused-export) flips on** with the bindings and the
  pre-committed marking: a package-level export is USED when the package
  it belongs to is imported by any analyzed file (whole-package fail-open,
  the package import is the wildcard by design; the recorded alias never
  narrows it). Documented consequence, asserted by the acceptance gate:
  at v1 whole-package resolution granularity the verdict set is EMPTY by
  construction -- a file with in-degree > 0 has its whole package marked
  used, and a never-imported package fails the pass's own precondition
  (the file must itself be imported). OC005 is enabled because its data
  pipeline is live and its semantics honest; verdicts appear if
  resolution granularity ever narrows below the package. The alternative
  (name-level marking from aliases) would guess.

### 6. Surface, entry points, coverage, manifests

- **Exports** (recorded this sprint): package-level declarations that are
  direct children of `source_file` with an exported (upper-case first rune)
  name -- `function_declaration`, `type_declaration` (`type_spec` names),
  `const_spec` and `var_spec` names. Methods (`method_declaration`) are NOT
  package surface: they hang off receiver types, and Go's exportedness rule
  is about the declared identifier's case, which the method name does not
  decide alone.
- **Import bindings** (shipped with resolver v1-go): one binding per
  `import_spec` -- the alias when aliased, NULL otherwise. Marking is
  package-wide fail-open (§Go.5); the alias is recorded as surface data
  for the module surface record, never as a narrowing predicate.
- **Entry points**: pack `entry_points` = `["main.go"]` (the pack-declared
  glob matches the basename per `surface.c`) PLUS `package_clause`
  claiming, shipped SPRINT-10: a file whose parsed `package_clause`
  declares `main` is claimed as an entry point (a cheap parse fact, same
  class as the shebang hint -- the SPRINT-9 documented approximation
  "package main files NOT named main.go are not claimed" is closed).
  Non-`main` packages are not claimed by the parse fact; the glob still
  claims any file literally named `main.go` regardless of package name.
- **Go test files**: `*_test.go` joins the language-blind test-convention
  list of `surface.c` (SPRINT-10) -- `go test` executes them directly, so
  a zero in-degree is expected and is never OC006 material (the same
  rationale as `conftest.py` and the JS spec suffixes).
- **Coverage (shipped SPRINT-11)**: the `go test -coverprofile` adapter
  (`name.go:startLine.startCol,endLine.endCol numStmts count` records;
  `mode: set`, `count` and `atomic` headers are the same data -- count > 0
  is covered). Anchor semantics: the inclusive line range of each record
  block is anchored, covered when count > 0 -- a block IS the statement
  unit Go's profiler measures, so its whole line range is the honest line
  projection (documented here; narrower per-line anchors would guess).
  Files map through the standard exact-then-unique-suffix path rule of
  `docs/ARCHITECTURE.md`. OC004 remains the no-coverage CRAP proxy when
  no profile is passed.
- **Manifests (shipped SPRINT-11)**: `go.mod`. OC009 dep-vuln against the
  vendored OSV snapshot with ecosystem **Go**: `require` entries (single
  and grouped; `// indirect` included -- they are real build inputs)
  carry exact semantic versions, so every require is a pin; version
  ranges do not exist in go.mod, unpinned = warn per the standing rule
  (no snapshot provided -> OC009 never fires, meta reports it). OC010
  licence: go.mod carries NO licence field -- the SPRINT-9 shorthand
  "licence from the module clause" is honestly amended here to what the
  format can support: the module clause identifies the module root (the
  go.mod directory), and OC010 detects a licence FILE (LICENSE, LICENCE,
  COPYING, NOTICE; case-insensitive basename) directly inside that root
  among the discovered files -- the same convention `go` tooling uses.
  Licence text is never parsed. No licence file -> warn, same as the
  npm/pyproject manifests. Main module, `go`/`toolchain` directives,
  `exclude`/`replace`/`retract` statements are not dependencies.

### 7. Enabled passes (pack data, not code)

`packs/go/pack.json` enables exactly the passes whose semantics are fully
computable under the shipped contract items: **OC001-OC011, all eleven**
(SPRINT-10 added OC003 and OC005 with the resolver and the bindings, see
§Go.5 for the OC005 empty-verdict-set consequence, which is asserted, not
assumed; SPRINT-11 added OC007 with the coverprofile adapter and OC009/
OC010 with the go.mod manifest tier). OC007 fires only when a coverage
profile is passed and OC009 only when a vendored OSV snapshot is passed
-- without their inputs they produce nothing and the report meta says so
(same behavior as every language).

No pass is disabled for Go any more; there is nothing left on the Go
contract's pre-committed list. The SPRINT-9/10 standing descopes are both
CLOSED (SPRINT-14): §Go.2 named function-value assignments are symbols,
§Go.8 build constraints are evaluated under the declared build context.
Filename-suffix constraints remain the one documented boundary (above).

### 8. Honesty invariants carried over

- Parse diagnostics: ERROR/MISSING nodes surface in the report (never
  silent) -- cgo shims or vendored snippets that confuse the parser show up
  as parse errors, not as clean-but-empty.
- **Build constraints are EVALUATED** (closed SPRINT-14: the §Go.8 descope
  is settled). The analysis scope is the DECLARED build context in
  `packs/go/pack.json` (`"build_context"`: `goos`, `goarch`, `compiler`,
  `cgo`, extra `tags`) -- a deterministic, committed analysis target, not
  the analysis host's uname (byte-identical reports must not depend on the
  machine that runs the analysis; `go vet`'s host-context behavior is the
  noted contrast, and pack data is the honest deterministic choice).
  Both directive forms are evaluated: the modern `//go:build EXPR`
  (identifiers joined by `||` / `&&`, `!`, parentheses; the first such
  line before the `package` clause wins) and the legacy `// +build` lines
  (comma = AND within a line, space = OR across options, `!` negation;
  multiple lines are OR'ed). When BOTH forms are present the `//go:build`
  line wins (the go1.17+ semantics). Tag truth: a GOOS or GOARCH literal
  from the standard vocabularies compares against the declared context,
  `unix` derives from the declared GOOS, the compiler tag (`gc`/`gccgo`)
  compares against the declared compiler, `cgo` is the declared `cgo`
  bit; ANY OTHER tag (release tags `go1.x`, custom `-tags`, `ignore`)
  is FALSE. A file whose constraint evaluates false LEAVES the analyzed
  file set entirely (what `go build` does) and the report meta counts it
  (`build_tag_excluded`) -- excluded, never silent. Files WITHOUT any
  constraint are analyzed as before. FILENAME-suffix constraints
  (`foo_linux.go`, `foo_windows.go`) stay OUT of scope: the name rules
  carry per-arch spelling variants the grammar cannot decide; they keep
  the over-report direction (a name-constrained file analyzed on a
  foreign platform over-reports, never hides).
- Unresolved specifiers stay recorded-and-unresolved; nothing silently
  degrades; honesty counters unchanged.

---------------------------------------------------------------------------

## Java language contract (pre-approved; adopted SPRINT-11, full)

No Java code existed when this contract was written FIRST (recipe step 1,
commit a6cbb5f); the adoption sprint (SPRINT-11) implemented it verbatim and
verified the point the contract itself flags for verification against the
pinned grammar. The verification records appear as `VERIFIED` blocks inside
the subsections below; the contract text is unchanged above them.

### 1. Grammar

- Upstream: `tree-sitter-grammars/tree-sitter-java`; pin the exact tag at
  adoption time (vendor via `scripts/vendor.sh`; codeload fallback
  available). No external scanner expected -- verify at adoption; never
  edit vendored sources.
- Caveat to verify at adoption: `module-info.java` and `package-info.java`
  parse but carry no function symbols; text blocks and switch expressions
  must parse cleanly on the pinned grammar (fixtures will pin both).

> **VERIFIED at adoption (SPRINT-11).** The grammar lives in the MAIN
> `tree-sitter` org, not the grammars org named above (probed:
> `tree-sitter-grammars/tree-sitter-java` does not exist; the main-org repo
> is the one the PyPI binding package tracks). Pinned tag **v0.23.5**
> (the latest tagged grammar at adoption time; commit
> `94703d5a6bed02b98e438d7cad1136c01a60ba2c`, verified with
> `git ls-remote`; PyPI `tree-sitter-java==0.23.5` tracks the same tag).
> Vendored at `third_party/tree-sitter-java/` (sources: `src/parser.c` +
> `src/tree_sitter/parser.h`, MIT licence; provenance in
> `THIRD_PARTY_NOTICES.md` -- the codeload tarball path worked in this
> environment, sha256 recorded there). **No external scanner** (verified
> against the pinned tag's src/ listing: a single generated `parser.c`).
> Both caveats hold and are pinned by fixtures: `module-info.java` and
> `package-info.java` parse with ZERO function symbols; text blocks and
> switch expressions (statement and arrow forms) parse cleanly.

### 2. Functions & spans

- Symbol kinds: `method_declaration`, `constructor_declaration`,
  `compact_constructor_declaration` (records). Lambda bodies
  (`lambda_expression`) and anonymous classes count toward the ENCLOSING
  method's complexity (inclusive-by-design); lambdas are not symbols.
- **Abstract and interface methods ARE symbols** (closed SPRINT-14: the
  "honestly NOT symbols" adoption note is settled -- the declaration itself
  is a definition extent). A `method_declaration` WITHOUT a `body` (an
  interface method, an `abstract` method) records a symbol whose canonical
  span is the whole declaration construct (definition extent, OBS-2) and
  whose complexity is exactly 1 (no body, no decisions -- the count is
  `1 + 0`). They are never module surface (members are reachable through
  their type, unchanged), and JaCoCo never carries counters for them (no
  executable lines), so OC007 coverage marks them uncovered at worst; the
  comp-1 CRAP proxy value is minimal by construction. The query pattern is
  the negated-field form (`!body`), so a bodyful method can never match it
  twice. What stays NON-symbol: `annotation_type_element_declaration`
  members (annotation defaults are a different construct, still unmodeled
  -- recorded here, not silent).
- Spans: definition extent (declaration line through the closing brace of
  the whole member declaration), same OBS-2 one-span contract.

### 3. Comments

- Node kind `line_comment` and `block_comment` (the Java grammar splits
  them; OC002 consumes both verbatim). Javadoc is plain comments -- no
  Javadoc model.

### 4. Complexity (cyclomatic, register-not-train)

Decision kinds: `if_statement`, `for_statement`, `while_statement`,
`do_statement`, `switch_rule` and each `switch_block_statement_group` with
a `case` label (verify exact node kinds against the pinned grammar's
node-types at adoption and record them here), `catch_clause`,
`conditional_expression` (ternary); operator tokens `&&` and `||` inside
`binary_expression`.

> **VERIFIED against tree-sitter-java v0.23.5 node-types.json and
> recorded here (SPRINT-11), exactly as this section mandates:**
> - the ternary kind is named **`ternary_expression`** (v0.23.5 has no
>   `conditional_expression` node);
> - v0.23.5 has NO `switch_statement` node -- both statement and
>   expression switch shapes parse as **`switch_expression`**; the two
>   counting kinds are the fallthrough **`switch_block_statement_group`**
>   (counted ONLY when it carries a `case`-labelled `switch_label` -- a
>   `default`-only group adds no path; implemented as a descriptor-level
>   `cx_filter` on the Java language entry, never pass code) and the
>   arrow-form **`switch_rule`** (counted unconditionally, `default ->`
>   rules included -- the contract wording; pinned by fixtures);
> - the classic **`for_statement`** and the for-each
>   **`enhanced_for_statement`** are two separate node kinds in v0.23.5;
>   both are decision kinds here (Java cyclomatic counts every loop);
> - `if_statement` / `while_statement` / `do_statement` /
>   `catch_clause` / `binary_expression` exist exactly as contracted;
>   the `&&` / `||` operator tokens are counted inside
>   `binary_expression` like the JS family.
> Lambdas (`lambda_expression`) and anonymous classes (a `class_body`
> child of `object_creation_expression` -- there is no
> `anonymous_class_expression` node) are not symbols; the inclusive body
> walk counts their decisions toward the ENCLOSING method, and a method
> declared inside an anonymous class is its own symbol (additive-safe,
> same shape as the JS family). Fixture: `describe` in
> `tests/fixtures/java/com/example/util/TextUtil.java` carries exactly 24
> decisions (complexity 25), one per recorded kind.

### 5. Imports & resolver semantics (v1-java, contract pre-committed)

- The parser records raw specifiers from `import_declaration`
  (`scoped_type_identifier` / `identifier` heads) and `static_import`.
  Single-type imports (`import a.b.C;`) and static imports
  (`import static a.b.C.f;`) record the qualifier path; wildcard imports
  (`import a.b.*;`) record the package path with the trailing `.*` kept --
  the resolver treats them as package claims, never as file claims.
- Resolution (an edge always means "target is part of this analysis"):
  a qualifier path resolves when the discovered file set contains a file
  whose path matches the qualifier's dotted-to-slash mapping with the
  class-name file (e.g. `a.b.C` -> `a/b/C.java`) under any analysis root;
  `a.b.*` resolves ONLY when the package directory `a/b` exists among
  analyzed files, and marks package-level surface fail-open (wildcard
  semantics, same discipline as `export * from`).
- Nested classes (`a.b.C.D`) never resolve against `a/b/C/D.java` -- the
  class-file model (`a/b/C.java`) is the only mapping v1 supports; anything
  else stays unresolved, never guessed.
- Classpath / build-tool dependency resolution is OUT OF SCOPE (WBS 4.x
  precedent, same as node_modules): external packages stay unresolved.

> **VERIFIED against the pinned grammar and recorded here (SPRINT-11):**
> v0.23.5 has NO separate `static_import` node and imports carry
> `scoped_identifier` (or bare `identifier`) heads -- a static import is
> an `import_declaration` with an anonymous `static` token, a wildcard
> carries the named `asterisk` child. The raw records match the contract
> exactly: single-type and static qualifiers record the qualifier path
> (the member segment is stripped from static imports at parse time),
> wildcards record the package path with the trailing `.*` kept.
> NESTED CLASSES: `import a.b.C.D` probes `a/b/C/D.java` and NOTHING
> else -- when D is genuinely a nested class of C the file does not
> exist, so the import stays unresolved; v1 never falls back to
> `a/b/C.java` (deciding that D is nested in C would be a guess). When
> `a/b/C/D.java` DOES exist (a top-level type in a subpackage -- legal
> Java layout), the uniform mapping resolves it; both shapes are pinned
> by resolver fixtures. Wildcard fan-out claims EVERY .java file
> directly inside the package directory (no test-file exclusion: the
> class-file model compiles nothing selectively -- the deliberate
> contrast with §Go.5, pinned by the resolver corpus where a wildcard
> targets `StoreTest.java`). No manifest, build file or classpath is
> ever read by the resolver.

### 6. Surface, entry points, coverage, manifests

- **Exports**: the Java model is one public top-level type per file
  (`public class C` in `C.java`). Export candidates: top-level
  `class_declaration` / `interface_declaration` / `enum_declaration` /
  `record_declaration` names; the public modifier decides exportedness.
  Members are not module surface v1 (methods are reachable through their
  type). Import bindings: single-type imports bind the class name;
  wildcard imports bind nothing (fail-open package-level marking).
- **Entry points**: pack `entry_points` = `["Main.java"]` plus
  `"**/Application.java"` (Spring Boot convention) -- documented
  file-name approximations, never-guess; `public static void main` parsing
  is reserved for the resolver sprint (the parser sees it, the pack glob
  does not claim by it yet).
- **Coverage**: pre-committed adapter `JaCoCo XML` (method-level counters
  map onto definition spans through the standard path rule); OC004 remains
  the no-coverage proxy until it lands.
- **Manifests**: pre-committed `pom.xml` (OC010 licence from the project
  clause; OC009 dep-vuln, OSV ecosystem **Maven**, `group:artifact` pins,
  version ranges never guessed, unpinned = warn).
  **`build.gradle` / `build.gradle.kts` ARE read** (closed SPRINT-14: the
  "no stable schema" descope is settled by a BOUNDED reader that never
  guesses -- the pom.xml bounded-XML-reader discipline applied to the
  Groovy/Kotlin DSLs): a dependency is recorded ONLY when the reader finds
  a dependency configuration keyword (`api`, `implementation`, `compileOnly`,
  `runtimeOnly`, `testImplementation`, `testCompileOnly`, `testRuntimeOnly`,
  `annotationProcessor`, and the deprecated `compile`/`runtime`) followed
  by an EXACT literal coordinate string -- a quoted
  `"group:artifact:version"` triplet (both quote styles; one optional
  `(` / `)` wrapper for the Kotlin DSL; a `platform(...)` /
  `constraints { ... }` wrapper is skipped honestly -- a BOM/platform node
  is not a versioned dependency), or the map form with all three of
  `group: 'g', name: 'a', version: 'v'` as exact literals ON ONE
  STATEMENT LINE (the reader's window is the statement line; a wrapped
  multi-line map form is skipped honestly, never guessed across lines).
  `${property}`
  references, variable references, dynamic/`latest`/`SNAPSHOT` versions and
  anything else are NOT pins -- unpinned = warn per the standing rule, and
  a coordinate missing any of the three segments records nothing (never
  guessed). OC009 ecosystem Maven; OC010 is UNCHANGED (pom.xml carries the
  licence clause; a gradle tier adds no licence model -- gradle files may
  live in subprojects where root probing would read the wrong root).
- **Enabled passes at adoption** (mirror the Go spike decision, reasons
  carried in pack notes): structural set OC001, OC002, OC004, OC006, OC008,
  OC011 from day one; OC003 lands WITH resolver v1-java in the same sprint
  (Java has real cycles -- OC003 is the point of the resolver sprint);
  OC005 with the binding semantics above; OC007/OC009/OC010 with their
  adapters.

> **VERIFIED at adoption (SPRINT-11):** all adapters landed IN the same
> sprint, so the java pack enables the COMPLETE set OC001-OC011 (the
> first language pack to do so). Binding semantics as contracted,
> plus one recorded clarification: a STATIC import binds the CLASS NAME
> of its qualifier (the last segment after the member is stripped) --
> a static import does use the class, and the fail-open direction (more
> `used` marking, never fabricating an unused verdict) is the honest
> reading of "single-type imports bind the class name". Consequence,
> asserted by both gates: OC005's verdict set for Java is EMPTY BY
> CONSTRUCTION at this granularity -- every resolved import marks its
> target's export used (single-type/static must bind the class name
> Java's class-file model requires; wildcards fail-open the whole
> package) -- the same honest outcome documented for §Go.5. Entry
> points are the contracted file-name approximations
> ("Main.java", "**/Application.java"). Test-file conventions: the
> Maven Surefire runner defaults (Test*.java, *Test.java, *Tests.java,
> *TestCase.java) are registered as language-blind runner data, same
> discipline as `_test.go`/`conftest.py` -- the gson corpus gate caught
> a byte-exactness bug in that matcher live (strcmp-vs-memcmp) and the
> fix is pinned by the gate's zero test-class flood assertion.
> OC007 JaCoCo XML: method-level counters map onto definition spans
> through the standard path rule, extended with the package-relative
> direction (the report path may be the analyzed path's "/"-boundary
> tail -- real JaCoCo reports name sourcefiles package-relative;
> ambiguity still matches nothing). OC009/OC010 pom.xml: OSV ecosystem
> **Maven**, `group:artifact` names, exact pins only (property
> references `${...}` are never substituted, Maven ranges and
> LATEST/RELEASE never guessed, unpinned = warn); only the ROOT-LEVEL
> `<dependencies>` is read (dependencyManagement/profiles/build are
> recorded descopes); `build.gradle(.kts)` parsing SHIPPED SPRINT-14
> (the bounded reader contracted above; the v1 descope is closed).
>
> **Same-package OC005 marking (SPRINT-14).** The import-driven marking
> alone under-reports: same-package types need NO import directive, so a
> package-mate's usage was invisible to the graph, and a class used only
> by its package-mates produced a FALSE OC005 positive once a mate (but
> not the class) was imported. Closed with the standing fail-open
> discipline: a `.java` file's exports are marked used when ANY other
> `.java` file lives in the same directory (same package by the
> class-file model; same-directory = same-package is Java's own rule).
> Over-marking, never fabricating an unused verdict -- the identical
> direction as the Go whole-package and Java wildcard marks. The OC005
> verdict-set consequence is UNCHANGED (the pass's own precondition --
> the file must itself be imported -- still bounds the verdict set), and
> the acceptance gates still assert it.

## C# language contract (written FIRST at SPRINT-12 close; adopted SPRINT-13, full scope)

The contract was written FIRST (recipe step 1) so the adoption sprint
started from an approved contract, exactly like Go and Java. The
pre-adoption guesses that needed verification against the pinned
grammar were probed BEFORE the adoption code (the recipe's
verification step; the probe results are recorded in place below).

### 1. Grammar

- Upstream: `tree-sitter-grammars/tree-sitter-c-sharp`, pinned tag
  **v0.24.0** (the latest tagged grammar at adoption time; the npm
  binding package `tree-sitter-c-sharp@0.24.0` and the PyPI sdist
  `tree_sitter_c_sharp-0.24.0` track the same tag).
- Vendored at `third_party/tree-sitter-c-sharp/` (sources: `src/parser.c`
  + `src/scanner.c` + `src/tree_sitter/` headers; MIT licence).
  Provenance recorded in `THIRD_PARTY_NOTICES.md`: the git-protocol
  restriction of the previous sprints applied again for new
  repositories, so the vendored content is the npm package tarball of
  the pinned version, whose `src/parser.c` is byte-identical (sha256
  cross-checked) to the PyPI sdist of the same version; both sha256s
  are recorded there. `scripts/vendor.sh` carries the generic codeload
  fallback for normal environments. Never edited after vendoring.
- **EXTERNAL scanner** (verified at adoption: the pinned grammar ships
  `src/scanner.c` -- interpolated-string and optional-semicolon
  tokens; the `oc_add_grammar` wiring compiles it when present). The
  generated parser declares `LANGUAGE_VERSION 15` (the vendored
  runtime v0.27.2 accepts it).
- Shape facts VERIFIED against the pinned grammar at adoption (each
  one probed with an actual parse, not assumed):
  - the root node is `compilation_unit`; top-level statements are
    `global_statement` nodes directly under it (C# 9 support ships in
    the grammar);
  - the ternary node is named **`conditional_expression`** -- the
    pre-adoption guess `ternary_expression` (borrowed from Java's
    verification note) would not have compiled; the amendment is the
    verification step doing its job;
  - there is NO `modifiers` wrapper: modifiers are individual NAMED
    `modifier` nodes whose text is the keyword itself (`public`,
    `static`, ...);
  - file-scoped namespaces are a SEPARATE node,
    `file_scoped_namespace_declaration` (block namespaces are
    `namespace_declaration` with a `declaration_list` body);
  - `static`, `global`, `equals`, `case`, `default` and `::` are
    anonymous tokens (`node_has_anon_child_text` applies);
  - preprocessor regions parse as structured nodes (`preproc_if`,
    `preproc_elif`, `preproc_else`, ...), NOT as errors.

### 2. Functions & spans

- Symbol kinds: `method_declaration`, `constructor_declaration`,
  `local_function_statement`, `operator_declaration`,
  `destructor_declaration` -- every named defining construct with a
  real body. Bodies are `block` or (expression-bodied members) the
  grammar's `arrow_expression_clause`; both count as the body and the
  canonical span is the whole declaration (definition extent, OBS-2).
- Interface and abstract methods have no body -- the query requires a
  body node, so they are honestly NOT symbols (no definition extent).
- **Property and event accessors ARE symbols** (closed SPRINT-14: the
  "bodies belong to the property" descope is settled -- and it closed a
  real accounting hole, because properties are not symbols and class
  members have no enclosing function, so accessor decisions were counted
  toward NOTHING). An `accessor_declaration` with a real body (`block` or
  `arrow_expression_clause`) inside an `accessor_list` records a symbol
  whose name is the DECLARING member's simple name joined to the
  accessor's own keyword by a dot -- `Prop.get`, `Prop.set`, `Evt.add`,
  `Evt.remove` (the one unambiguous honest name; the declaring name is
  the `property_declaration` / `event_declaration` `name` field, the
  accessor keyword is its anonymous token child; when either cannot be
  read nothing is recorded -- never guessed). The canonical span is the
  whole `accessor_declaration` (definition extent, OBS-2); complexity is
  counted over the accessor body (inclusive discipline unchanged).
  Auto-property accessors (`get;` / `set;` with no body) stay non-symbols
  -- a compiler-generated backing-field shape has no honest body to span.
- Lambda bodies, anonymous methods and plain local statements count
  toward the ENCLOSING function's complexity (inclusive-by-design);
  `local_function_statement` IS a symbol (it carries a name and a
  definition extent).

### 3. Comments

- Node kind `comment` (line, block and XML doc comments are all
  `comment` -- no doc-tree model; OC002 marker scan applies verbatim).

### 4. Complexity (cyclomatic, register-not-train)

Decision kinds, VERIFIED against the pinned grammar at adoption:
`if_statement` (the grammar has no `else_clause` -- `else if` is a
nested `if_statement`, so each branch increments by itself, the same
discipline as Go), `for_statement`, `foreach_statement`,
`while_statement`, `do_statement`, `switch_section` WITH a `case`
label only (the label is an anonymous `case` token checked in C; a
default-only section is not a decision), `switch_expression_arm`
(each arrow-form arm of a switch EXPRESSION is one branch),
`catch_clause`, `catch_filter_clause` (the `when` filter is a second,
C#-specific decision in the same construct), `conditional_expression`
(the ternary -- the verified name, see 1), operator tokens `&&`, `||`
and `??` inside `binary_expression` (the JS-family discipline: a
null-coalescing default is a branch). Lambdas and local statements
count inward (inclusive). OC004 threshold semantics and the
no-coverage CRAP proxy (`comp^2+comp`) are unchanged and
language-blind.

### 5. Imports & resolver semantics (v1-csharp, shipped SPRINT-13)

- The parser records one raw specifier per `using_directive`, encoded
  with the directive's OWN tokens (everything between `using` and `;`):
  - plain `using A.B.C;` records `A.B.C` -- a NAMESPACE claim (a plain
    using cannot legally name a type, so no type probe is made);
  - `using static A.B.C;` records `static A.B.C` -- a TYPE claim
    (C#'s own `static` token kept in the specifier);
  - `using X = A.B.C;` records `X = A.B.C` -- an ALIAS, the one
    genuinely ambiguous form (the target may be a namespace OR a
    type); the alias name stays surface data and never narrows
    marking (the Go alias discipline);
  - the `global::` prefix is stripped (it is C#'s own spelling of
    "from the global namespace root", so the verbatim mapping IS the
    root-relative mapping); any OTHER alias-qualified name (`X::Y.Z`,
    extern aliases) stays unresolved;
  - paths that are not identifier-qualified names (generics
    `A.B.C<int>`, tuples, pointer types) stay unresolved -- never
    re-mapped.
- Resolution (an edge always means "the target is part of this
  analysis"), all deterministic and filesystem-free:
  - a namespace claim `A.B.C` maps to a namespace directory under an
    analysis root (roots probed in command-line order -- the shared
    root-order discipline) and resolves iff that directory holds at
    least one discovered `.cs` file DIRECTLY inside it; the target is
    the WHOLE NAMESPACE: every direct-child `.cs` file receives an
    `imports` edge from the importing file (the whole-package
    discipline of v1-go/v1-java). There is NO test-file exclusion: C#
    test trees follow build conventions, not import semantics (the
    Java rationale);
  - the LAYOUT LADDER (amended at adoption -- the real-corpus gate
    proved the pre-adoption single-convention wording wrong, the
    SPRINT-10 `golang.org` lesson repeating, and the amendment is the
    verification step doing its job): the C# ecosystem ships TWO
    directory layout conventions, probed in a DOCUMENTED ORDER (first
    hit wins, never both -- the JS suffix-ladder discipline):
    (1) dots mapped to slashes, `A/B/C` -- the SDK/VS convention the
    .NET tooling mandates for new projects; (2) the dotted single-
    directory form, `A.B.C` as one directory name -- mono's historical
    mcs layout (real-world trees ship it: the pinned corpus's
    libraries nest `System.Configuration.Internal/` etc.). The same
    ladder applies to the type claim's file probe (`A/B/C.cs` first,
    then `A.B.C.cs`). Both conventions are just conventions -- the
    probe order is deterministic, the analyzed file set stays the only
    universe, and a specifier matching NEITHER stays unresolved;
  - a type claim `static A.B.C` probes the class-file model, one
    `.cs` appended (the v1-java mapping), through the layout ladder
    above;
  - an alias claim `X = A.B.C` probes in a DOCUMENTED ORDER: the type
    file first (ladder: `A/B/C.cs`, then `A.B.C.cs`), then the
    namespace directory (ladder: `A/B/C`, then the dotted
    single-directory form) -- first hit wins, never both (in
    compile-legal code the two readings cannot coexist, so the probe
    resolves the unique honest target);
  - never-guess guards: empty dot-segments, leading dots, `.`/`..`
    segments, absolute and empty paths stay unresolved (no path
    escapes); the filesystem outside the analyzed file set is never
    consulted; SDK/framework usings (`System.*`) point outside the
    analyzed set and stay unresolved; NO project/MSBuild reading by
    the resolver (WBS 4.x precedent) -- the namespace directory tree
    is the only universe;
  - the namespace declaration of a file is NEVER cross-checked
    against its directory (csc's job, not the resolver's -- the
    javac discipline carried over).
- **The imports relation is a SET for C#** (scoped to the C# branch;
  the JS/TS/Python/Go/Java pins stay untouched): plain + static +
  alias records can name the same target, and the duplicates collapse
  into one edge.
- **OC003 (import-cycle) flips on** with the edges: file-level Tarjan
  SCC, one finding per SCC. **OC005 (unused-export) flips on** with
  the bindings: a static binding marks the target file's export
  matching the BINDING NAME (the type simple name); a namespace claim
  marks every public top-level type of the namespace's direct-child
  `.cs` files used (fail-open, `export * from` discipline); an alias
  claim marks fail-open as well (the alias never narrows).
  **Same-namespace OC005 marking (closed SPRINT-14).** Same-namespace
  types need NO using directive, so the import-driven marking alone
  under-reports: a type used only by its namespace-mates produced a
  FALSE OC005 positive. Closed with the standing fail-open discipline:
  each file's DECLARED dotted namespace(s) are recorded as a parse-time
  hint (the Go `package_clause` channel -- block namespaces join their
  nesting with dots; preprocessor wrappers are climbed; a file may
  declare several), and a file's exports are marked used when ANY OTHER
  analyzed file declares the same namespace. Over-marking, never
  fabricating an unused verdict. The verdict-set consequence is
  UNCHANGED (the pass's own precondition -- the file must itself be
  imported -- still bounds the verdict set); unlike the import marking,
  the mate mark follows the DECLARED namespace, not the directory
  (the declared namespace is never cross-checked against the directory,
  unchanged).

### 6. Surface, entry points, coverage, manifests

- **Exports**: top-level `class_declaration` / `interface_declaration`
  / `struct_declaration` / `enum_declaration` / `record_declaration`
  names; the modifier set carrying `public` decides exportedness
  (internal/package-private types are NOT module surface). Top-level
  means not nested in another TYPE: namespace nesting (block or
  file-scoped) and preprocessor wrappers are climbed honestly (the
  verified node walk), type nesting is not. C# has no one-public-type-
  per-file rule, so every public top-level type of a file is an export
  candidate. Members are not module surface v1 (reachable through
  their type). Import bindings: the static form binds the type simple
  name; namespace and alias claims bind nothing (fail-open).
- **Entry points**: pack `entry_points` = `["Program.cs"]` (the
  convention -- a documented file-name approximation, never-guess)
  PLUS two parse facts, the same class of cheap facts as the Go
  `package_clause`, the Java main fact and the shebang:
  (a) a `method_declaration` named `Main` whose modifier set carries
  `static` claims the file -- accessibility is NOT required (C#
  accepts non-public entry points; the fact fires on the declaration
  as parsed, no intent guessed);
  (b) a `compilation_unit` with a `global_statement` direct child is
  claimed -- C# 9 top-level statements ARE the program entry point by
  specification.
- **Coverage (shipped SPRINT-13)**: the Coverlet **Cobertura XML**
  adapter (the `dotnet test`/coverlet.msbuild `cobertura` output).
  Anchors are the `<line number hits>` records under
  `<class filename="A/B/C.cs">`: a line is an anchor when the report
  carries a record for it, covered when `hits > 0` (Cobertura's own
  hit-count semantics; the `branch` attribute is consumed exactly as
  far as the line anchor needs it -- the line record is the finest
  honest granularity Cobertura XML ships). Filenames join to the
  analyzed path through the standard exact-then-unique-suffix rule.
  The XML dispatch is by ROOT ELEMENT (verified): `<report>` -> the
  JaCoCo reader (SPRINT-12), `<coverage>` -> the Cobertura reader,
  `<CoverageSession>` -> the **OpenCover reader** (SPRINT-14: the
  "different schema" descope is closed -- OpenCover XML carries an open
  schema with line-anchored statement records, the same honest shape as
  the Go coverprofile block). Anchors are the `<SequencePoint>` records:
  the inclusive `sl`..`el` line range (a sequence point IS the statement
  unit OpenCover measures -- the Go block precedent), covered when
  `vc > 0`; each record joins to `<File uid="..." fullPath="...">`
  through its `fileid` attribute and the fullPath maps through the
  standard exact-then-unique-suffix rule; a record whose `fileid` names
  no known file records nothing (honest). MSBuild binary `.coverage`
  REMAINS a recorded descope (a proprietary binary stream with no open
  spec -- a fabricated parse is worse than the warn).
- **Manifests (shipped SPRINT-13)**: `.csproj` (SDK-style MSBuild).
  OC009 dep-vuln against the vendored OSV snapshot with ecosystem
  **NuGet**: `<PackageReference Include="X" Version="1.2.3">` entries
  (attribute or child element) record an `X` pin when the version is
  an exact literal (alphanumeric/dot/dash/plus only); `$(...)`
  property references, floating `*` versions and ranges are NOT pins
  (unpinned = warn, never guessed). `ProjectReference` entries are
  in-repo project links, not external dependencies -- not OC009
  material (recorded). **`packages.config` and `packages.lock.json` ARE
  read** (closed SPRINT-14): `packages.config` is the legacy NuGet
  manifest -- an `<package id="X" version="1.2.3" ... />` element records
  an `X` pin when the version is an exact literal (the
  `csproj_exact_pin` rule); `packages.lock.json` is the lockfile -- a
  `dependencies.<name>` entry whose `resolved` field is an exact literal
  records the RESOLVED pin (the `requested` range is NEVER substituted,
  never guessed), and both `type: "Direct"` and `type: "Transitive"`
  entries are read (transitive pins are real build inputs -- the go.mod
  `// indirect` precedent). Both are OC009 ecosystem **NuGet**, parsed
  as their own manifests wherever discovered (a packages.config lives in
  its project directory; the manifest walk is per-file, unchanged).
  OC010 licence, honestly layered by what
  the format can declare: `<PackageLicenseExpression>` (the SPDX
  expression the project declares) first; else `<PackageLicenseFile>`
  naming a licence file that exists among the discovered files
  directly inside the project root; else the Go-convention project-
  root licence-FILE detection (LICENSE, LICENCE, COPYING, NOTICE;
  case-insensitive basename) among the discovered files; none -> the
  same warn as every other manifest. Licence text is never parsed.
- **Enabled passes (pack data, not code)**: **OC001-OC011, all
  eleven** -- the whole contract landed in one sprint: the structural
  set from day one, OC003/OC005 with resolver v1-csharp and the
  binding semantics (C# has real cycles -- OC003 is the point), OC007
  with the Cobertura adapter, OC009/OC010 with the .csproj tier. OC007
  fires only with `--coverage`, OC009 only with `--osv-snapshot`;
  without their inputs they produce nothing and the report meta says
  so.

### 7. Honesty invariants carried over

- Parse diagnostics: ERROR/MISSING nodes surface in the report (never
  silent) -- generated code or snippets that confuse the parser show
  up as parse errors, not as clean-but-empty.
- Conditional compilation: every discovered `.cs` file is analyzed AS
  WRITTEN -- no preprocessor evaluation, so BOTH branches of an `#if`
  are parsed and analyzed. This over-reports rather than hides. This is
  a DELIBERATE KEEP at SPRINT-14 (re-recorded with a sharpened reason
  after the Go build-tag descope closed there): evaluating `#if` without
  the real build configuration (csproj DefineConstants across all
  configurations, implicit SDK defines, compiler-generated symbols) would
  GUESS the active branch -- and a wrong guess HIDES code, the direction
  the honesty invariants forbid. Over-reporting both branches is the
  only non-guessing reading; the descope stays, as a decision, not a
  gap.
- Unresolved specifiers stay recorded-and-unresolved; nothing silently
  degrades; honesty counters unchanged.

---------------------------------------------------------------------------

## Rust language contract (written FIRST at SPRINT-14 close; adopted SPRINT-15, full scope)

The contract was written FIRST (recipe step 1, before any adoption code),
mirroring Go/Java/C#. Every pre-adoption shape guess that needed
verification against the pinned grammar was probed BEFORE the adoption
code (the recipe's verification step; probe results recorded in place as
VERIFIED blocks below).

### 1. Grammar

- Upstream: `tree-sitter/tree-sitter-rust` (the MAIN tree-sitter org --
  probed: the grammars org has no rust repo), pinned tag **v0.24.0** (the
  latest tagged grammar at adoption time; the npm binding package
  `tree-sitter-rust@0.24.0` tracks the same tag).
- Vendored at `third_party/tree-sitter-rust/` (sources: `src/parser.c` +
  `src/scanner.c` + `src/tree_sitter/` headers; MIT licence). Provenance
  recorded in `THIRD_PARTY_NOTICES.md`: this environment's git protocol
  is unavailable for new repositories and the grammars-org codeload
  endpoint throttles, so the vendored content is the npm package tarball
  of the pinned version, sha256 recorded in `scripts/vendor.sh` and
  THIRD_PARTY_NOTICES.md, and cross-checked byte-identical (parser.c and
  scanner.c sha256) against the codeload tarball of the same tag --
  same upstream sources, honestly labelled. `scripts/vendor.sh` carries
  the rust fetch with the npm fallback. Never edited after vendoring.
- **EXTERNAL scanner** (the pinned grammar ships `src/scanner.c`; the
  `oc_add_grammar` wiring compiles it when present).
- Shape facts VERIFIED against the pinned grammar at adoption (each one
  probed with an actual parse, not assumed):
  - the root node is `source_file`;
  - `use_declaration` exposes the use tree through ONE field
    (`argument`); the head keywords `crate` / `self` / `super` are NAMED
    nodes (their text is the keyword itself); `pub use` carries a named
    `visibility_modifier` child on the use_declaration;
  - group forms: `scoped_use_list` (prefix + `use_list`), `use_list`
    (brace groups, arbitrarily nested), `use_as_clause` (path + alias),
    `use_wildcard` (path + `*`);
  - a bodyless `mod_item` (children end with the anonymous `;`) is the
    file-module declaration; an inline `mod_item` carries a
    `declaration_list`;
  - `#[...]` attributes are standalone named `attribute_item` SIBLING
    nodes preceding the item they annotate;
  - trait method signatures and extern-block function declarations are
    BOTH `function_signature_item` (the uniform bodyless-symbol node;
    extern blocks are `foreign_mod_item` with a `declaration_list`);
  - the decision nodes parse exactly as named below in section 4
    (verified against the symbol table at adoption; `if let` is an
    `if_expression` whose condition is a `let_condition`, `while let` a
    `while_expression` with the same condition shape -- there are NO
    separate if-let/while-let node kinds in the pinned grammar);
  - doc comments are `line_comment` nodes whose children carry the
    `outer_doc_comment_marker` / `doc_comment` parts INSIDE the comment
    span -- consumed verbatim as plain comments;
  - macro bodies (`macro_definition`) are token trees -- no decision
    node parses inside them (they are opaque token soup to the grammar,
    so complexity is exactly 1 by construction, not by guesswork).

### 2. Functions & spans

- Symbol kinds this sprint: `function_item` (free functions, impl
  methods, trait default methods -- the name field decides), and the
  **named function-value bindings**: a `const_item` or `static_item`
  whose `value` IS a `closure_expression` (the item-level named closure:
  `const HOP: fn(i64) -> i64 = |x| ...`, `static LEAP: ... = |x| ...`).
  All are OC_NODE_FUNCTION symbols; the canonical span is the whole
  defining construct (definition extent, OBS-2 unchanged). A const/static
  whose value is NOT a closure is not a symbol (a named constant, not a
  named function).
- **Bodyless signatures ARE symbols** (the Java SPRINT-14 discipline,
  applied through the same q_bodyless slot): trait method signatures and
  extern-block declarations (`function_signature_item`) record a symbol
  whose canonical span is the whole signature construct and whose
  complexity is exactly 1 (no body, no decisions; LCOV never carries
  counters for them, so OC007 honestly marks them uncovered at worst).
- **Local closures are NEVER symbols** (a deliberate divergence from the
  Go §Go.2 promotion of locals, recorded here as a decision, not a gap):
  a `let`-bound closure inside a function lives inside the enclosing
  function's span already -- its decisions count toward the ENCLOSING
  function (inclusive-by-design, additive-safe, nothing hidden), and
  inventing a second symbol for the binding would double-count the same
  body. Only item-level named definitions (`const`/`static` closures)
  promote. The Go promotion closed a descope where named function VALUES
  were invisible as package members; Rust's analogue at module scope is
  the const/static promotion above.
- Anonymous closures (call arguments, `go`-style immediates) stay
  non-symbols; their bodies count inward (inclusive).
- Methods are symbols with the same span contract (the `function_item`
  inside an `impl_item` / trait `declaration_list`), but NOT module
  surface (the Go method rule, section 6).
- `macro_rules!` definitions (`macro_definition`) ARE symbols: a named
  defining construct with a definition extent; complexity exactly 1
  (macro bodies are token trees -- no decision node parses inside, see
  section 1). Macros are NOT module surface (Rust's own export rule for
  macros is `#[macro_export]`; modeling attribute-driven exportedness is
  a recorded boundary -- without it a macro is honestly never surface,
  the under-report direction).

### 3. Comments

- Node kind `line_comment` (line, `///` and `//!` doc comments are all
  line_comment -- the doc markers are children inside the comment span
  and the text is consumed verbatim, no doc-tree model). OC002's marker
  scan applies verbatim, same as every language.

### 4. Complexity (cyclomatic, register-not-train)

Decision kinds shipped with the descriptor (all VERIFIED against the
pinned grammar's node-types):

- `if_expression` (Rust's `else if` is a nested `if_expression`, so each
  branch increments by itself; `if let` is the same node kind with a
  `let_condition` -- one decision either way),
- `while_expression` (`while let` included, same shape),
- `for_expression` (Rust's only loop-over form),
- `loop_expression` (the unconditional `loop`),
- `match_arm` -- ONE decision per ARM of a `match_block` (an arm whose
  pattern carries alternatives `a | b` is still one arm: the
  alternatives are one exhaustive split of the same value space,
  gocyclo's case-clause discipline carried over; pinned by fixture),
- `try_expression` (the `?` propagation -- a genuine branch on
  success/error),
- inside `binary_expression`: the operator tokens `&&` and `||`.

No ternary exists in Rust. The count is `1 + decisions`, computed over
the function body exactly as for every other language (nested closures
count inward). OC004 threshold semantics and the no-coverage CRAP proxy
(`comp^2+comp`) are unchanged and language-blind.

### 5. Imports & resolver semantics (v1-rust, shipped this sprint)

- The parser records TWO raw channels, both declaration-level:
  1. **the mod-tree channel**: a bodyless `mod_item` (`mod name;`) in
     file F records one raw specifier `mod name`. The record is the
     reference (a mod declaration is a real reference -- Rust compiles
     the file); it pushes NO import binding (declaring a module makes
     its items addressable, it does not USE any of them -- Rust's own
     dead_code lint fires for never-referenced items of a declared
     module, and the analysis must stay able to report them).
     A mod declaration preceded by an attribute_item whose text carries
     `path = ` records NOTHING (the attribute moves the file; probing
     the conventional name would fabricate a WRONG edge). Any other
     attribute (`#[cfg(...)]`, ...) does not move the file -- the
     conventional probe stands (the C# `#if` keep-discipline: both
     branches stay analyzed, over-report never hides).
  2. **the use-channel**: one raw specifier per LEAF of the use tree,
     expanded deterministically: `scoped_identifier` and bare
     `crate`/`self`/`super` heads join with `::`; `use_as_clause`
     records the pre-alias path (the alias is a local rename; marking
     uses the target's own export name -- fail-open, the Go alias
     discipline); `use_wildcard` records `<path>::*`; brace groups
     (`use_list`, arbitrarily nested) expand prefix::leaf per child.
     The specifier keeps its own head (`crate::a::A`, `self::x::y`,
     `super::a::A`, `serde::fix::Thing`) -- the head IS the
     classification. Macro-generated `metavariable` arguments record
     nothing (never guessed). `extern crate` declarations are not an
     import channel in v1 (2015-edition style; recorded boundary -- the
     name is external anyway).
- **Resolver v1-rust** (an edge always means "the target is part of
  this analysis"), all deterministic and filesystem-free:
  - **mod-tree channel**: the declaration's file F names the search
    directory D = dirname(F) when basename(F) is `lib.rs`, `main.rs` or
    `mod.rs` (the directory IS the module), else dirname(F)/stem(F)
    (Rust's stem rule: `src/util.rs` declares `mod helper;` ->
    `src/util/helper.rs`). The record resolves to the exact file
    `D/name.rs`, else `D/name/mod.rs`, else stays unresolved (one edge,
    never a fan-out: a mod declaration names ONE module file).
  - **use-channel**: a specifier whose head is NOT `crate`/`self`/
    `super` (external crate names: `serde`, `std`, the crate's own name
    in integration tests) stays UNRESOLVED -- always, without consulting
    any manifest (the go.mod discipline: a Cargo.toml declaring the
    crate name is a decoy the resolver never reads; `use mycorp::...`
    inside the crate stays unresolved, pinned by fixture).
    For intra-crate heads the resolver anchors the path at the file's
    own module directories:
  - the SELF anchor A(F) = dirname(F) when the file IS a module root --
      basename `lib.rs`, `main.rs`, `mod.rs`, or a `.rs` file directly
      inside a PACKAGE-LEVEL `tests/` directory (cargo compiles each as
      an integration-test crate whose crate root is the file itself: its
      `mod support;` probes `tests/support.rs`, not
      `tests/<stem>/support.rs`; package-level = no `src` segment in the
      tests dir's own directory chain -- a `tests` directory inside the
      src tree is an INTERNAL module directory and keeps the stem rule) --
      else dirname(F)/stem(F);
    - the CRATE anchor R(F) = the nearest ancestor directory named
      `src` or `tests` (walking up from dirname(F)); when the hit is
      `src` and dirname(F) is `<src>/bin`, R is that `bin` directory
      (`src/bin/*.rs` are their OWN crates -- the fixture pins that
      `crate::store::...` inside `src/bin/tool.rs` must NOT reach the
      lib crate's `src/store/...`). Files with no `src`/`tests`
      ancestor anchor the crate at dirname(F) (a loose tree is not a
      cargo crate; the probes simply fail, nothing fabricates);
    - `self::p1::...::pN` probes A(F), `crate::...` probes R(F),
      `super::...` probes dirname(A(F)) -- the parent module's anchor --
      EXCEPT when A(F) == R(F) (the file IS the crate root: `lib.rs`,
      `main.rs`, a `mod.rs` at the crate anchor): the crate has no
      parent module, `super::` stays unresolved;
    - the ITEM-READING POP LADDER at the anchor: probe
      `<anchor>/p1/.../pN.rs` then `<anchor>/p1/.../pN/mod.rs` (the
      full-path module probe), then POP the last segment and repeat
      (p(N-1) down to p1) -- the deepest module file the path names is
      the edge target (`use crate::store::types::Widget` probes
      `src/store/types/Widget.rs`, then the parent module file
      `src/store/types.rs` -- found: an edge to the module that owns
      the item). The ladder NEVER probes the anchor file itself
      (K >= 1): a use-tree that pops past every segment stays
      unresolved honestly (a self-referential re-export like
      `pub use self::inline_mod::thing` where the mod is inline in the
      same file names no OTHER file).
  - never-guess guards: empty segments, absolute paths, `.`/`..`
    segments (Rust paths cannot legally contain them; a malformed
    record is dropped), and everything outside the analyzed set stays
    unresolved; the filesystem is never consulted.
  - MACRO-GENERATED declarations are NOT parse facts (a recorded
    boundary, measured on the pinned corpus): a `mod` declaration inside
    a macro invocation (`cfg_io_util! { mod buf_reader; }`) is an
    anonymous token inside the invocation's token tree -- the grammar is
    deliberately token-soup-opaque there, and expanding macros to
    recover it would guess arbitrary syntax. A file whose only reference
    is macro-generated reports zero in-degree and OC006 verdicts: the
    over-report direction (a verdict triage can clear, versus hidden
    code no tool can recover), the same class of recorded boundary as
    the Go filename-suffix constraints.
- **The imports relation is a SET for Rust**: the mod-tree + use-tree
  channels can name the same target through several records (mod + use,
  brace groups, re-exports); duplicate (from,to) pairs collapse into one
  edge (the C# discipline, scoped to the Rust branch).
- **OC003 (import-cycle) flips on** with the edges: file-level Tarjan
  SCC, one finding per SCC (Rust modules genuinely cycle via
  `crate::`/`super::` paths -- the a/b fixture pair pins one).
- **OC005 (unused-export) flips on** with the bindings: a use-record's
  binding name is the leaf segment (pre-alias) of the path; the resolved
  target file's export matching that name is marked used; a glob leaf
  (`::*`) and any record whose leaf cannot be read mark the whole target
  file used (fail-open, `export * from` discipline). A mod-tree record
  marks nothing (section 5, channel 1 -- declaring is not using). There
  is NO same-directory mate marking for Rust, deliberately (the Java/C#
  mate closes a hole Rust does not have: sibling files are NOT the same
  module -- every cross-file reference goes through a path, and the
  declaration-level channels record exactly those paths; auto-marking
  same-directory mates would fabricate usage and silence honest unused
  verdicts, e.g. the `mod lonely;`-declared module whose items nobody
  names).

### 6. Surface, entry points, coverage, manifests

- **Exports**: top-level `source_file` items with PLAIN `pub`
  visibility -- `function_item`, `struct_item`, `enum_item`,
  `type_item` (aliases), `const_item`, `static_item`, `trait_item`,
  and inline `mod_item`. The visibility check is the modifier node's
  exact text `pub`: `pub(crate)`, `pub(super)`, `pub(in path)` carry
  extra tokens and are NOT module surface (Rust's own exportedness
  rule: restricted visibility does not cross the crate boundary).
  Methods are not surface (the Go method rule); `macro_rules!`
  definitions are not surface (section 2); a `pub use` re-export is
  import-channel data, not an export candidate (v1: re-exports are
  pointers, not definitions -- recorded boundary).
- **Import bindings**: one binding per use-tree leaf: the leaf segment
  (pre-alias), NULL for globs (fail-open). The alias is surface data
  only; marking uses the target's own export name (section 5).
- **Entry points**: pack `entry_points` = `["lib.rs", "main.rs"]` (the
  cargo crate-root convention, basename-matched per `surface.c`) PLUS
  the `fn main` parse fact (the Go package_clause class of cheap parse
  facts): a `function_item` named `main` that is a DIRECT child of
  `source_file` claims the file -- this is what claims `src/bin/*.rs`
  binaries (the pack glob cannot: they are not named main.rs), and the
  fixture pins that the parse fact fires only on the top-level
  declaration.
- **Test conventions**: a `.rs` file directly inside a `tests/`
  directory is runner data -- a zero in-degree is expected and is never
  OC006 material. Both real shapes are covered (measured on the pinned
  corpus): a PACKAGE-LEVEL `tests/x.rs` (cargo compiles each as its own
  integration-test crate; the file is also its own crate root for the
  anchor rule, section 5) and a `tests` directory INSIDE the src tree
  (`src/<...>/tests/x.rs` -- unit-test module files compiled under
  cfg(test) with the lib, whose mod declarations are typically
  macro-generated, see the boundary in section 5). Deliberately NARROW:
  only `<...>/tests/x.rs` (the Sprint 2 over-broad-directory lesson;
  `tests/sub/mod.rs` support modules are NOT auto-claimed). `#[test]`
  attributes do not make a file runner data (the file's in-degree
  decides, as for every language).
- **Shebang vs inner attributes** (measured on the pinned corpus, a
  pre-existing hint bug Rust surfaced): the `#!` first-line prefix is a
  shebang ONLY when the `!` is not followed by `[` -- Rust INNER
  ATTRIBUTES (`#![allow(...)]`, `#![cfg_attr(...)]`) open most real
  crate files, are item-ish attributes at file top, and never claimed
  an executable line; treating them as shebangs silently claimed every
  such file as an entry point and silenced honest OC006 verdicts (99
  findings on the corpus before the fix, 92 after -- the seven files
  the bug hid were zero in-degree, unclaimed, and genuinely
  unreferenced). A kernel shebang interpreter path can never start with
  `[`.
- **Coverage (shipped this sprint)**: the **LCOV** adapter (the
  `lcov`/`geninfo`/`grcov`/`cargo-llvm-cov --lcov` text format -- the
  open format the Rust ecosystem actually emits; `cargo-tarpaulin`
  and `cargo-llvm-cov` both ship it). Dispatch: a text file whose first
  non-blank line starts with `TN:` or `SF:` (the Go coverprofile sniff
  stays first: `mode:`; the two sniffs are disjoint). Structure:
  per-record blocks `TN:` / `SF:<path>` / `FN:`,`FNDA:` / `DA:<line>,
  <count>` / `BRDA:` / `LF:`/`LH:` / `end_of_record`. Anchors are the
  `DA` records: a line is an anchor when the report carries a DA for
  it, covered when the hit count > 0 -- DA is the only record that
  carries both a line anchor and a hit count (FN/FNDA are function-
  keyed, BRDA is branch-keyed with no honest line-anchored hit model in
  v1 -- consumed and skipped, never fabricated). Paths join through the
  standard exact-then-unique-suffix rule. Malformed input fails LOUD
  (the Go coverprofile discipline): a DA/BRDA/LF/LH/end_of_record
  before any `SF:`, a non-numeric DA line or count, or EOF inside an
  open record are USAGE errors, never silently partial.
- **Manifests (shipped this sprint)**: `Cargo.toml` and `Cargo.lock`.
  OC009 dep-vuln against the vendored OSV snapshot with ecosystem
  **crates.io** (OSV's own ecosystem spelling):
  - `Cargo.toml`: dependency tables `[dependencies]`,
    `[dev-dependencies]`, `[build-dependencies]`,
    `[target.'cfg(...)'.dependencies]` and `[workspace.dependencies]`
    are all read (dev/build/transitive pins are real build inputs --
    the `// indirect` and Transitive precedents). A requirement string
    is a PIN iff it is the exact form `=x.y.z...` (the leading `=`
    stripped for the recorded version); caret/tilde/star/comparison
    forms (`"1.2"`, `"~1.2.4"`, `"*"`, `">=1.2, <2.0"`) are NOT pins --
    unpinned = warn, never guessed (Cargo's own semantics). Inline
    table entries read their `version` field. `{ path = ... }` entries
    are in-repo links, not external dependencies -- not OC009 material
    (the `ProjectReference` precedent). `{ workspace = true }` entries
    inherit the same manifest's `[workspace.dependencies]` entry by
    name (version and pin-ness verbatim); an entry the workspace table
    does not name records NOTHING (the version is unknowable from this
    file alone -- never guessed). `{ git = ... }` without a version is
    an unpinned dependency (warn). Comments (`#`) are stripped before
    scanning. OC010 licence, honestly layered by what the format can
    declare: `[package] license = "..."` first; else `license-file =
    "..."` naming a licence file that exists among the discovered files
    directly inside the project root; else the Go-convention project-
    root licence-FILE detection (LICENSE, LICENCE, COPYING, NOTICE;
    case-insensitive basename); none -> the same warn as every other
    manifest. Licence text is never parsed.
  - `Cargo.lock`: every `[[package]]` entry that carries a `source`
    field records the RESOLVED pin (registry and git sources are
    external build inputs -- the packages.lock.json resolved-pin
    discipline); entries WITHOUT a `source` are the workspace members
    themselves and record nothing. The `requested`/requirement ranges
    of Cargo.toml are never substituted (never guessed). OC010 skips
    this manifest (no licence field).

### 7. Enabled passes (pack data, not code)

`packs/rust/pack.json` enables exactly the passes whose semantics are
fully computable under the shipped contract items: **OC001-OC011, all
eleven** -- the whole contract landed in one sprint (the Java/C#
pattern): the structural set (OC001/OC002/OC004/OC006/OC008/OC011) from
day one, OC003/OC005 with resolver v1-rust and the binding semantics
(Rust has real module cycles -- OC003 is the point), OC007 with the
LCOV adapter, OC009/OC010 with the Cargo manifest tier. OC007 fires only
when a coverage profile is passed and OC009 only when a vendored OSV
snapshot is passed -- without their inputs they produce nothing and the
report meta says so (same behavior as every language).

### 8. Honesty invariants carried over

- Parse diagnostics: ERROR/MISSING nodes surface in the report (never
  silent) -- macro-generated or generated-code snippets that confuse the
  parser show up as parse errors, not as clean-but-empty.
- Conditional compilation: every discovered `.rs` file is analyzed AS
  WRITTEN -- no `cfg` attribute evaluation (the C# `#if` DELIBERATE
  KEEP, re-recorded here as the same decision, not a gap): evaluating
  `#[cfg]` without the real feature set (Cargo features, target triples,
  implicit cfgs) would GUESS the active branch -- and a wrong guess HIDES
  code, the direction the honesty invariants forbid. Both branches stay
  analyzed (over-report); a `#[cfg]`-gated mod declaration still probes
  the conventional file (section 5, channel 1).
- Bare path expressions (calling `store::foo()` where `store` is a mod
  declared in the same file, without a `use` declaration) are NOT import
  records -- declaration-level granularity, the same class of documented
  boundary as every other language's v1 (under-reporting edges, never
  fabricating them).
- Unresolved specifiers stay recorded-and-unresolved; nothing silently
  degrades; honesty counters unchanged.

## Ruby language contract (written FIRST at SPRINT-15 close; adopted SPRINT-16, full scope)

The contract was written FIRST (recipe step 1, before any adoption code),
mirroring Go/Java/C#/Rust. Every pre-adoption shape guess that needed
verification against the pinned grammar was probed BEFORE the adoption code
(the recipe's verification step; probe results recorded in place as VERIFIED
blocks below, probe script `scripts/probe_ruby.c`).

### 1. Grammar

- Upstream: `tree-sitter/tree-sitter-ruby` (the MAIN tree-sitter org; the
  grammar has always lived there), pinned tag **v0.23.1** (the latest tagged
  grammar at adoption time; the npm binding package `tree-sitter-ruby@0.23.1`
  tracks the same tag).
- Vendored at `third_party/tree-sitter-ruby/` (sources: `src/parser.c` +
  `src/scanner.c` + `src/tree_sitter/` headers; MIT licence). Provenance
  recorded in `THIRD_PARTY_NOTICES.md`: the vendored content is the npm
  package tarball of the pinned version, sha256 recorded in
  `scripts/vendor.sh` and THIRD_PARTY_NOTICES.md, and cross-checked
  byte-identical (parser.c and scanner.c sha256) against the codeload tarball
  of the same tag -- same upstream sources, honestly labelled.
  `scripts/vendor.sh` carries the ruby fetch with the npm fallback. Never
  edited after vendoring.
- **EXTERNAL scanner** (the pinned grammar ships `src/scanner.c` -- heredoc
  bodies, delimited string/regex/array literal start & end tokens; the
  `oc_add_grammar` wiring compiles it when present). LANGUAGE_VERSION 14
  (verified: the vendored runtime v0.27.2 accepts it).
- Shape facts VERIFIED against the pinned grammar at adoption (each one
  probed with an actual parse, not assumed):
  - the root node is `program`; `__END__` DATA sections parse CLEAN -- the
    data section is one named `uninterpreted` node, and no decision parses
    inside it (the data is honestly not code);
  - `require` / `require_relative` parse as `call` nodes whose `method:`
    field is an `identifier` (the receiver forms -- `Kernel.require 'x'` --
    keep the same method field; the C side filters the two names), whose
    `arguments:` field is an `argument_list`, and a paren-less call still
    produces the argument_list; a plain string argument is a `string` node
    with a `string_content` child;
  - methods: `method` (plain `def`) and `singleton_method` (`def self.x` /
    `def Const.x` -- the `object:` field carries the receiver). ENDLESS
    method definitions (`def f = expr`, Ruby 3.0+) are the SAME `method`
    node with the `body:` field holding the expression -- no separate node
    kind. EMPTY methods (`def foo; end`) have NO `body:` field at all (the
    negated-field pattern catches them; the Java SPRINT-14 discipline);
  - the `name:` field of a method is one of THREE verified node kinds:
    `identifier` (plain), `setter` (`def name=`), `operator` (`def []`,
    `def +`) -- the query captures the name field whatever node it is, and
    the symbol name is the name node's text as written;
  - `class` / `module` carry `name:` fields of `constant` or
    `scope_resolution` (`class Foo::Bar` -- the name node's text is the full
    path as written); `class << self` is a `singleton_class` (no name field
    -- never surface); `alias` is a re-binding node, not a defining
    construct;
  - the decision nodes parse exactly as named in section 4 (verified against
    the probe and the grammar's node-types: the block forms
    `if`/`unless`/`while`/`until`/`for`, `elsif` clauses, `when` clauses of
    `case`, `in_clause` clauses of `case_match`, `rescue` clauses, the
    postfix modifiers `if_modifier`/`unless_modifier`/`while_modifier`/
    `until_modifier`/`rescue_modifier`, and the ternary `conditional`);
  - **the anonymous keyword tokens SHARE the named nodes' type strings**
    (`if`, `when`, `case`, `rescue`, `in`, ... -- the parser labels both the
    named construct and its leading keyword token with the same type), so
    the shared complexity walker now matches decision kinds on NAMED nodes
    only (a Ruby-driven hardening of the walker; verified by inspection that
    no other language's kind list matches an anonymous node type, and the
    existing suites stayed green);
  - the binary operator container node is `binary` (not `binary_expression`
    -- every other language keeps the shared default; the descriptor names
    its ops container explicitly): `&&`/`||`/`and`/`or` are ANONYMOUS
    operator tokens inside it;
  - comments are `comment` nodes -- one kind covers `#` line comments AND
    `=begin`/`=end` blocks (consumed verbatim, no doc model; the Ruby
    "magic comment" directives like `frozen_string_literal` are plain
    comments to the parser and stay OC002 material like every comment).

### 2. Functions & spans

- Symbol kinds this sprint: `method` and `singleton_method` (any receiver).
  All are OC_NODE_FUNCTION symbols; the canonical span is the whole defining
  construct (definition extent, OBS-2 unchanged). Endless definitions are
  the same node (section 1) and need no special case.
- **Bodyless (empty) methods ARE symbols** (the Java SPRINT-14 discipline,
  applied through the same q_bodyless slot): a `method`/`singleton_method`
  with no `body` field records a symbol whose canonical span is the whole
  definition and whose complexity is exactly 1 (no body, no decisions; a
  coverage report never carries counters for them, so OC007 honestly marks
  them uncovered at worst). The negated-field query cannot match a bodyful
  method twice (the functions query keeps its body requirement).
- Operator names and setters keep the name AS WRITTEN (`[]`, `+`, `name=`):
  the name node's text is the honest name, the same discipline as the C#
  accessor synthesis (no invented decorations).
- **Blocks are NEVER symbols** (a deliberate decision, recorded here as the
  Rust local-closure divergence's sibling): `do ... end` blocks, `{ ... }`
  blocks and lambdas are anonymous or locally-bound closures that live
  INSIDE the enclosing method's span -- their decisions count toward the
  ENCLOSING method (inclusive-by-design, additive-safe, nothing hidden), and
  inventing a second symbol for a block would double-count the same body.
- **Metaprogramming is not parse facts** (a recorded boundary, the Rust
  macro boundary's analogue): `define_method`, `attr_accessor`, `Module.new`
  and `eval` generate methods at RUNTIME -- the grammar is deliberately
  opaque to what they generate (the block arguments are token trees of real
  Ruby, but the METHOD ITSELF never appears as a `method` node). Expanding
  metaprogramming would guess arbitrary semantics; the over-report direction
  (files whose only references are dynamic report honest zero in-degree and
  OC006 verdicts a triage can clear) is the accepted one, the same class of
  recorded boundary as the Rust macro-generated declarations.
- `alias` nodes are re-bindings, not defining constructs -- not symbols
  (recorded boundary).

### 3. Comments

- Node kind `comment` (line `#` comments AND `=begin`/`=end` blocks are all
  `comment` -- one kind, consumed verbatim, no doc-tree model). OC002's
  marker scan applies verbatim, same as every language.

### 4. Complexity (cyclomatic, gocyclo-aligned, register-not-train)

Decision kinds shipped with the descriptor (all VERIFIED against the pinned
grammar's node-types and the probe):

- `if` and `unless` (the block forms; `else` is never a decision; `elsif`
  is its own named node, so each branch increments by itself -- the Go
  discipline),
- `while`, `until`, `for` (the block loop forms),
- `when` -- ONE decision per clause of a `case` (a clause with
  comma-separated multiple patterns is still one clause: one split of the
  same value space, gocyclo's case-clause discipline carried over; pinned by
  fixture),
- `in_clause` -- ONE decision per clause of a `case_match` (pattern
  matching). An `if_guard` inside a clause does NOT count separately: a
  guard that fails falls through to the NEXT clause, so the guard is part of
  the same one-split-of-the-value-space discipline (recorded decision),
- `rescue` clauses (a genuine branch on exception; multiple `rescue`
  clauses each count). `ensure` is NOT a decision (it always runs),
- the postfix modifier forms -- `if_modifier`, `unless_modifier`,
  `while_modifier`, `until_modifier`, `rescue_modifier` -- each is its own
  named node and each is one decision,
- `conditional` (the ternary `a ? b : c`),
- inside `binary`: the operator tokens `&&`, `||`, `and`, `or` (all four --
  the low-precedence keyword forms branch exactly like their symbol forms;
  the ops container node name is part of the descriptor, `binary` for Ruby,
  the shared `binary_expression` default for every other language).

No separate node kind exists for `if`/`unless` guarding `then`-less bodies
-- the block forms cover them. The count is `1 + decisions`, computed over
the method body exactly as for every other language (nested blocks count
inward). OC004 threshold semantics and the no-coverage CRAP proxy
(`comp^2+comp`) are unchanged and language-blind. The shared walker's
NAMED-only kind match (section 1) is what keeps the keyword tokens from
double-counting their constructs -- without it every Ruby count would be
roughly double, which the comp hand-count fixtures pin against.

### 5. Imports & resolver semantics (v1-ruby, shipped this sprint)

- The parser records ONE raw channel, call-shaped: every `call` node's
  method field and argument list are captured by the query, and the C side
  filters the two names `require` and `require_relative` (receiver forms
  like `Kernel.require 'x'` included -- the method field is the same shape).
  The recorded specifier carries its own channel tag (the Rust `mod `
  precedent): `require_relative <payload>` or `require <payload>`.
  - The ARGUMENT GUARD: the argument_list must carry EXACTLY ONE named
    child of type `string` whose children contain no `interpolation`
    (Kernel#require takes exactly one static argument; a multi-argument
    call, a non-string argument, or an interpolated string would fabricate
    a path that is not what the code loads -- the guard records NOTHING in
    those shapes, never guessed).
- **Resolver v1-ruby** (an edge always means "the target is part of this
  analysis"), all deterministic and manifest-free:
  - **`require_relative 'p'`**: the search directory is dirname(F); the
    probe is `dirname(F)/p.rb` (Ruby's own rule: relative to the requiring
    file, `.rb` appended). `..` traversal is legal Ruby and is NOT an
    escape hazard: the probe can only ever hit files inside the analyzed
    set (the graph's file index) -- everything outside stays unresolved by
    construction. The probe hits exactly one file; one edge, never a
    fan-out.
  - **`require 'p'` (bare)**: Ruby's `$LOAD_PATH` is unknowable from a
    static scan. v1 probes the two DOCUMENTED LOAD-ROOT CONVENTIONS under
    the analysis root, in order -- the v1-go dotted-path discipline
    (deterministic probes that hit the analyzed set are honest; misses stay
    unresolved; nothing is ever fabricated, and NO manifest is ever read to
    learn the load path -- the Gemfile decoy discipline):
    1. `<root>/p.rb` (a load root at the scan root -- apps run with the
       repo root on the load path),
    2. `<root>/lib/p.rb` (the gemspec/bundler lib load root -- the standard
       gem layout).
  - **The imports relation is a SET for Ruby**: one file may name the same
    target through several records (`require_relative` twice, or a
    require_relative and a bare require hitting the same file); duplicate
    (from,to) pairs collapse into one edge (the C#/Rust discipline, scoped
    to the Ruby branch).
  - never-guess guards: empty payloads, absolute paths, backslashes, and
    payloads exceeding the path buffers stay unresolved; the filesystem is
    never consulted (the probes run against the graph's file index only).
- **OC003 (import-cycle) flips on** with the edges: file-level Tarjan SCC,
  one finding per SCC. require cycles are LEGAL Ruby at runtime
  (`$LOADED_FEATURES` dedupes the second load); statically the cycle is a
  real import shape and the finding is honest (the Rust OC003-is-not-zero
  posture).
- **OC005 (unused-export) flips on** with the bindings: a require record
  binds NULL -- a require is a WHOLE-FILE claim (requiring a file loads all
  its top-level constants; the JS side-effect-import / Go package-import
  fail-open discipline), so the resolved target file's exports are all
  marked used. **The OC005 verdict set for Ruby is EMPTY by construction**
  (the recorded consequence of the whole-file claim, the Go precedent
  asserted by the acceptance gate: at file-granularity marking, every
  export of a file that is itself required is marked used, so no
  export-level verdict can survive -- the honest outcome versus guessing
  name-level usage that Ruby's runtime constant resolution cannot support
  statically). There is NO same-directory mate marking for Ruby,
  deliberately (the Java/C# mate rule closes a hole Ruby does not have the
  same way): Ruby's global-constant visibility is decided at RUNTIME by
  load order, and a static v1 that never guesses marks only what the
  requires claim -- auto-marking directory mates would fabricate usage and
  silence honest unused verdicts. The over-report direction (an unused
  verdict a triage can clear) is the accepted one, the same recorded
  decision as the Rust no-mate rule.

### 6. Surface, entry points, coverage, manifests

- **Exports**: top-level (direct `program` children) `class` and `module`
  declarations -- the name node's text AS WRITTEN (`constant` or
  `scope_resolution`; `class Foo::Bar` records `Foo::Bar`) -- plus top-level
  constant assignments (`VERSION = '1.0'`; the left side must be a plain
  `constant`; a scope-resolution left side `A::B = 1` is a recorded
  descope). The query anchors every capture to direct `program` children,
  so nested classes/modules and member constants never become candidates.
  Ruby has no visibility modifier at file scope (everything a loaded file
  defines is globally visible), so there is NO exportedness filter -- the
  marking consequences ride the file-granularity require claim (section 5).
  Methods are not module surface (the Go method rule); `class << self` has
  no name and is never surface; singleton-class methods are ordinary
  method symbols and behave like every method (section 2).
- **Import bindings**: one binding per require record, name NULL (the
  whole-file claim; section 5). The item-binding machinery other languages
  carry does not apply: Ruby requires name files, not items.
- **Entry points**: pack `entry_points` = `["bin/*", "exe/*"]` (the two
  bundler executable conventions; the patterns carry a slash, so they match
  the path shape -- one segment under bin/ or exe/) PLUS the pre-existing
  shebang hint (`#!/usr/bin/env ruby`): `#!` is a comment to the Ruby
  parser, so a `#!` first line is ALWAYS a kernel shebang for Ruby -- no
  false-positive class exists (the Rust inner-attribute trap has no Ruby
  analogue; the `#!`-not-followed-by-`[` fix is harmless here). There is NO
  main-method parse fact for Ruby v1: the `if __FILE__ == $PROGRAM_NAME`
  guard is a runtime idiom, not a declaration (recorded boundary -- the
  Go package_clause / C# Main parse-fact class stays where it is).
- **Test conventions**: the runner-collected NAME conventions -- `test_*.rb`
  (minitest), `*_test.rb` (minitest), `*_spec.rb` (RSpec) -- are runner
  data: a zero in-degree is expected and is never OC006 material. Same
  language-blind runner-naming discipline as `_test.go` and `Test*.java`.
  Deliberately NARROW: name-based only, NO directory claiming (the Sprint 2
  over-broad-directory lesson -- `test/support/*.rb` helper files are
  legitimately referenced by the tests and must keep their honest
  in-degree).
- **Coverage (shipped this sprint)**: the **SimpleCov JSON adapter** (the
  `.resultset.json` format SimpleCov -- Ruby's de-facto coverage tool --
  emits; `simplecov-json` and SimpleCov's own JSON formatter share the
  shape). Dispatch: a JSON object whose suites (top-level values) are
  objects carrying a `coverage` object (checked before the istanbul
  fallback; the istanbul shape has no `coverage`-suite wrapper).
  Structure: `{"<suite name>": {"coverage": {"<path>": {"lines": [...],
  "branches": {...}}}, "timestamp": ...}}`. The `lines` array is
  positional: index i is line i+1; a NUMERIC entry is an anchor, covered
  when the hit count > 0; a NULL entry is a not-relevant line and is NOT an
  anchor (nothing fabricated for it -- the honest analogue of "no DA
  record"); any other element type is a USAGE error (loud, naming the
  resultset, suite and file). `branches` maps are consumed and skipped (no
  honest line-anchored hit model in v1 -- the BRDA precedent). Suites union
  per file (the istanbul multi-file discipline). Paths join through the
  standard exact-then-unique-suffix rule.
- **Manifests (shipped this sprint)**: `Gemfile.lock` and `Gemfile`; OC009
  dep-vuln against the vendored OSV snapshot with ecosystem **rubygems**
  (OSV's own ecosystem spelling):
  - `Gemfile.lock`: the `GEM` section's `specs:` entries -- `name (version)`
    at exactly 4-space indent -- are the RESOLVED pins (the Cargo.lock
    resolved-pin discipline); the 6-space sub-entries under a gem are that
    gem's own REQUESTED ranges and record NOTHING (every transitively
    loaded gem's resolved pin appears at the 4-space level anyway); `GIT`
    and `PATH` sections are remote-source/in-repo links, skipped entirely
    (the Cargo `{ path = ... }` precedent); `DEPENDENCIES`, `PLATFORMS` and
    `BUNDLED WITH` are not pin material. OC010 licence: the lockfile
    carries no licence field -- the project-root licence-FILE probe (the
    go.mod discipline; LICENSE, LICENCE, COPYING, NOTICE; case-insensitive
    basename among the discovered files directly inside the project root).
  - `Gemfile`: a bounded single-line reader for `gem "name"[, "spec"]*`
    calls (comments stripped; the dep name is the first string argument; a
    `path:`/`:path =>` option marks an in-repo link and records nothing --
    the Cargo path-dep precedent). A spec string is a PIN iff it is the
    bare version form (`"1.2.3"` -- bundler's own exact-requirement
    semantics) or the explicit `"= x.y.z"`; `~>`, `>=`, `>`, `<`, `<=`,
    `!=` and ranges are NOT pins -- unpinned = warn, never guessed (bundler's
    own semantics). No spec at all is unpinned. OC010 licence: the
    project-root licence-FILE probe, same as the lockfile.
  - `.gemspec` files are NOT read in v1 (a recorded boundary: the DSL's
    evaluation semantics exceed a bounded reader -- `File.read`-built
    strings, dynamic assignments; the Gemfile/Gemfile.lock tier carries the
    supply story). The DSL files without a `.rb` extension (Gemfile itself,
    Rakefile, `*.gemspec`) are likewise outside the ANALYZED source set --
    discovery is extension-driven, and adding basenames to discovery would
    change the file universe for every pack (a cross-cutting decision,
    deliberately not taken this sprint).
  - OC009/OC010 for rubygems manifests route through the ruby pack (the
    scan-time ecosystem->pack routing gains the rubygems case; npm->ts-js
    and Maven->java are unchanged).

### 7. Enabled passes (pack data, not code)

`packs/ruby/pack.json` enables exactly the passes whose semantics are fully
computable under the shipped contract items: **OC001-OC011, all eleven**
(the Java/C#/Rust pattern; the fourth full-scope pack): the structural set
(OC001/OC002/OC004/OC006/OC008/OC011) from day one, OC003/OC005 with
resolver v1-ruby and the whole-file binding semantics, OC007 with the
SimpleCov adapter, OC009/OC010 with the Gemfile tier. OC007 fires only when
a coverage profile is passed and OC009 only when a vendored OSV snapshot is
passed -- without their inputs they produce nothing and the report meta
says so (same behavior as every language).

### 8. Honesty invariants carried over

- Parse diagnostics: ERROR/MISSING nodes surface in the report (never
  silent) -- heredoc/regex edge cases that confuse the external scanner
  show up as parse errors, not as clean-but-empty.
- As-written analysis: every discovered `.rb` file is analyzed AS WRITTEN.
  Ruby has no static conditional compilation to evaluate (require-time
  branching is runtime) -- nothing to keep deliberately, nothing guessed.
- Metaprogramming generates methods at runtime -- NOT parse facts (section
  2): the over-report direction (honest OC006 verdicts a triage can clear)
  is the accepted one.
- `__END__` DATA sections are `uninterpreted` nodes -- data, not code
  (verified: no decision parses inside them; nothing recorded from them).
- Unresolved specifiers stay recorded-and-unresolved; nothing silently
  degrades; honesty counters unchanged.

## PHP language contract (Track E, wave-2 completion; written FIRST at SPRINT-19 open, probed against the pinned grammar before any adoption code)

Track E re-opens the language program on the plan's own wave pool
(`OmniCheck_Project_Plan.md` section 8, line "Waves: 1) Java, Go -> 2)
C#, PHP, Ruby, C/C++ -> 3) Rust, Kotlin, Swift, Scala"): five of the
ten pool members were adopted in Track D; PHP is the first of the five
remaining (PHP -> C/C++ -> Kotlin -> Swift -> Scala, wave order). The
grammar is tree-sitter/tree-sitter-php **v0.24.2** (the MAIN
tree-sitter org), pinned in `scripts/vendor.sh` with the npm binding
tarball sha256 `fdd22530...` as the final fallback; the npm package and
the codeload tag are byte-identical for `php/src/parser.c`,
`php/src/scanner.c`, `php/src/node-types.json`, `php_only/src/parser.c`
and `common/scanner.h` (SPRINT-19 cross-check). LANGUAGE_VERSION 15,
accepted by the runtime v0.27.2. The v0.22+ repo layout keeps `php/`,
`php_only/` and `common/` subdirectories; OmniCheck compiles ONLY
`php/src/parser.c` + `php/src/scanner.c` -- the MIXED php+html grammar
-- and leaves `php_only/` as uncompiled vendored bytes. Every shape
fact below carries a VERIFIED block from `scripts/probe_php.c` (four
probe files, zero parse errors, compiled against the vendored runtime
and parser with `-D_DEFAULT_SOURCE -I third_party/tree-sitter-php/common`,
the 0.27 runtime API: `ts_parser_parse_string(parser, NULL, src, len)`).

### 1. Grammar

VERIFIED: the root node is `program` for BOTH pure-PHP files (leading
anonymous `php_tag`) and HTML-mixed template files. In a mixed file the
HTML outside `<?php` blocks is `text` nodes and
`php_end_tag`/`php_tag` transitions are wrapped in `text_interpolation`
nodes; statements -- including decision nodes (`if_statement`) -- sit
directly under `program` between interpolations. One descriptor handles
both file kinds: `text` nodes carry no functions, no decisions and no
imports, so template prose is inert to every analysis. The mixed
grammar parses pure files with zero errors (probe file 1) and mixed
files with zero errors (probe file 4).

### 2. Functions & spans

VERIFIED shapes:
- `function_definition` (top-level and nested): fields `name` (a `name`
  node), `parameters` (`formal_parameters`), `return_type`, `body`
  (`compound_statement`).
- `method_declaration`: the same fields; `visibility_modifier`,
  `static_modifier`, `abstract_modifier`, `final_modifier` children are
  named nodes that carry no name. EMPTY methods -- interface signatures
  and `abstract` methods -- carry NO `body` field (the Java
  bodyless-slot precedent; complexity exactly 1 through `q_bodyless`).
- `anonymous_function` (closures, with `anonymous_function_use_clause`
  listing captured variables), `arrow_function` (`fn`), and
  `anonymous_class` are NOT symbols (the Ruby blocks precedent): their
  decisions count inward, inclusive; they never create symbols.
- Parameters (inside `formal_parameters`; commas are anonymous):
  `simple_parameter` with fields `type` (optional), `reference_modifier`
  (optional `&`) and `name` (a `variable_name` whose text includes the
  `$` sigil); the default is the `default_value`-FIELD child behind an
  anonymous `=`; `variadic_parameter` (optional `type`, `name`, `...`
  anonymous); `property_promotion_parameter` (fields `visibility`,
  `type`, `name` -- PHP 8 constructor promotion; default as above).
  Type nodes: `primitive_type`, `named_type`, `optional_type`
  (nullable `?`), `union_type`, `intersection_type`. The compat (B3)
  signature records the parameter name WITHOUT the `$` sigil (the
  identifier child), raw type text, and `has_default` from the
  `default_value` field; a variadic parameter collapses to the `...`
  marker, an unrecognized named child collapses to ONE marker so arity
  stays caller-visible.
- No main-method parse fact exists (PHP has none; the `index.php` web
  entry is a NAME convention, section 6).

### 3. Comments

VERIFIED: line, block and doc comments are all ONE `comment` node kind
(no `@`-attribute nodes are comments; PHP 8 attributes are
`attribute` nodes attached to declarations and are not comments).

### 4. Complexity (cyclomatic, register-not-train)

The decision table, all VERIFIED named-node kinds:
- `if_statement` = 1; each `else_if_clause` = 1; `else_clause` = 0.
- `while_statement` = 1; `do_statement` = 1 (do-while).
- `for_statement` = 1; `foreach_statement` = 1.
- `switch_statement` itself = 0; each `case_statement` = 1 (comma-less
  fallthrough groups are one clause each); VERIFIED: `default:` is a
  DISTINCT `default_statement` node and counts 0 (a default arm adds no
  branch -- the gocyclo discipline).
- `match_expression` itself = 0; each `match_conditional_expression`
  arm = 1 (a comma condition list is ONE arm -- the Ruby when-clause
  discipline); `match_default_expression` = 1 (it is a real arm).
- `try_statement` = 0; each `catch_clause` = 1 (fields `type`
  (`type_list`) and optional `name`); `finally_clause` = 0.
- `conditional_expression` = 1 -- VERIFIED: the full `? :` ternary AND
  the short `?:` form are the SAME node (the short form has no body
  field); `??` is a `binary_expression` and is NOT a decision (a value
  operator, not a branch); `&&`, `||`, `and`, `or` are anonymous
  operator tokens inside `binary_expression` and count 1 each (the
  CX_OPS_PHP list; the shared `binary_expression` container default --
  no `cx_ops_node` override). `throw_expression`, `exit_statement`,
  `goto_statement`, `yield_expression` are not decisions (the standing
  throw-not-a-branch rule).
- Anonymous-keyword collision: none -- every decision kind string is a
  named node; the SPRINT-16 named-only kind match stays.

### 5. Imports & resolver semantics (v1-php, contract pre-committed)

Two static channels, both VERIFIED:
1. **use declarations** (the PHP import surface): `namespace_use_declaration`
   holds EITHER one or more `namespace_use_clause` children (each with
   an optional `function`/`const` keyword token, a `name`/
   `qualified_name` child -- `qualified_name` carries a `prefix` field
   to its `namespace_name` -- and an optional `alias` field behind
   `as`) OR a `namespace_name` + `body` field holding a
   `namespace_use_group` whose clauses are `namespace_use_clause`s
   (same fields, the group prefix prepends). Resolution: a use of
   `Ns\Class` joins the FILE that defines it through the
   composer.json PSR-4/PSR-0 mapping read from the ANALYSIS ROOT
   (prefix -> directory, backslash -> slash, anchored at the root; the
   longest-prefix rule; `use function`/`use const` clauses resolve to
   the same classmap discipline only when a prefix matches). A use
   whose leading prefix matches NO composer prefix is recorded and
   UNRESOLVED (external, the Go module-boundary precedent) -- nothing
   guessed from include_path or runtime autoloading magic.
2. **require/include expressions** (the Ruby require precedent):
   `require_expression`/`require_once_expression`/
   `include_expression`/`include_once_expression` with the ARGUMENT
   GUARD -- exactly ONE static string child, no interpolation, no
   concatenation (the probe's `__DIR__ . '/x.php'` concatenation
   records NOTHING); a `path/to/file` argument joins relative to the
   requiring file's directory, `__DIR__` never read from macros. The
   imports relation is a SET. No mate marking (runtime load order is
   not a parse fact -- the Rust/Ruby no-mate decision).
composer.json/composer.lock are the manifests (section 6); PSR-4
autoload is resolver CONFIG, not an imports edge.

### 6. Surface, entry points, coverage, manifests

- **Surface:** top-level `class_declaration`, `interface_declaration`,
  `trait_declaration`, `enum_declaration` (with its `enum_case`
  members as written), and top-level `function_definition` names, AS
  WRITTEN; NO exportedness filter (PHP namespace members are all
  public surface -- the Ruby precedent). Namespaced declarations keep
  the simple declaration name; the file's `namespace_definition` (`name`
  field -> `namespace_name`) is a file-level fact, not per-symbol
  prefixing. `const_declaration` at top level adds `const_element`
  names.
- **Entry points:** NAME conventions only -- `index.php` (the web
  entry) and `bin/*` runner scripts; the shebang hint does not exist
  for PHP.
- **Coverage:** the Clover XML adapter (php-code-coverage's
  `clover.xml`): `<coverage>` -> `<file name="...">` entries with
  `<line num= count=>` children; count > 0 is covered, 0 uncovered,
  statements are anchors; dispatched BEFORE the istanbul fallback; a
  file entry whose path resolves to no analyzed file is counted in the
  ambiguity meta, never guessed.
- **Manifests:** `composer.json` (a bounded reader: `name`,
  `require` / `require-dev` versioned dependencies -> OC010 rows;
  `autoload` PSR-4/PSR-0 prefixes -> resolver config; `path` repos are
  in-repo links) and `composer.lock` (the `packages` + `packages-dev`
  arrays' `name` + `version` exact pins are the RESOLVED pins).
  OSV ecosystem: **packagist**.
- **Pack:** `packs/php/pack.json` targets full scope OC001-OC011 (the
  fifth full-scope pack) -- every capability the pack needs ships in
  this sprint (clover adapter, composer manifests, packagist OSV).

### 7. Enabled passes (pack data, not code)

Full scope: OC001-OC011, gated by `packs/php/pack.json` exactly like
the Ruby pack (the fourth full-scope precedent).

### 8. Honesty invariants carried over

- Parse errors are loud: a file that fails to parse keeps its zero
  complexity and reports the error (nothing silently degrades). The
  mixed grammar's `text`/`text_interpolation` nodes are inert to every
  analysis -- template prose never becomes a symbol, a decision or an
  import.
- As-written analysis: no static conditional compilation in PHP to
  evaluate; `declare(strict_types=1)` is a directive node, recorded as
  nothing.
- Dynamic classloading (PSR-4 autoloading at runtime, variable class
  names, `class_exists` gates) is NOT a parse fact -- the Rust macro /
  Ruby metaprogramming boundary, accepted in the over-report direction:
  OC006 over-reports zero-in-degree symbols when autoload fans in at
  runtime.
- Unresolved use-declarations stay recorded-and-unresolved; nothing
  silently degrades; honesty counters unchanged.

### 9. SPRINT-19 adoption addendum (decisions locked at adoption time, all probed or gated)

- **OC005 binding of an aliased use**: the binding is the LAST backslash
  segment of the qualified name (the simple name the resolved file's
  export carries); the `as` alias is the importing file's LOCAL name and
  NEVER narrows the marking (the C# alias discipline) -- binding the
  alias would false-positive OC005 on the declared name.
- **composer prefix normalization**: the map key's trailing separators
  are composer convention and are stripped before the match (composer's
  own raw prefix rule, longest prefix wins, PSR-4 preferred at equal
  length); a fully-qualified use (one leading `\`) names the same class
  as its relative form and is dropped before the match; PSR-4 candidate
  = `<root>/<dir>/<remainder>.php`, PSR-0 candidate =
  `<root>/<dir>/<full-name-path>.php`. Array-valued mappings and empty
  strings record nothing (never guessed). Absent, unreadable or
  unparseable composer.json = NO config (the tsconfig fail-open
  discipline): uses stay resolved-never, unresolved honestly.
- **composer manifests pin discipline**: constraint ranges (`^ ~ > < >=
  <= * x || -`) record NOTHING in composer.json (the npm discipline -- a
  constraint is not a pin and composer resolves the truth in the
  lockfile, so no unpinned row is manufactured); the lock's
  `packages`/`packages-dev` `name`+`version` are the RESOLVED pins.
  Platform requirements (`php`, `ext-*`, `lib-*`, `php-*`, `composer`,
  `hhvm`) name the runtime, not a packagist package, and record nothing
  in BOTH readers.
- **Pack entry-point globs**: the bin/* runner-script convention is
  carried in the pack glob dialect as `**/bin/*` (the
  `**/Application.java` precedent -- a bare `bin/*` anchors at the
  absolute path start and never matches); `index.php` is a pack bare
  name that matches the basename at any depth. The shebang hint does
  not exist for PHP: the parser's generic `#!` line-1 hint is
  family-guarded off for PHP files (a `#!` outside PHP blocks is
  template text).
- **Longest-prefix verification**: the corpus gate's resolver leg and
  the unit suite pin the deeper-prefix-wins behavior (an `A\B\` prefix
  beats `A\` for `A\B\C`) and the fail-open behavior on absent config.

---------------------------------------------------------------------------

## C language contract (wave-2, adopted SPRINT-20 in one session; probed against the pinned grammar before any adoption code)

The C/C++ architecture decision (user-approved at SPRINT-19 close):
**separate `c` and `cpp` packs**, never one combined pack (a single
grammar pin cannot serve two dialects, `.h` routing goes blunt, queries
dilute, the per-language gate granularity is lost), with explicit
decision-node handling and the `.h` content sniffing + project-root
default written into the report when the C++ pack registers. C goes
FIRST: the `.h` ambiguity does not exist while C++ is unregistered, the
grammar is the smallest of the pool, and it ships NO external scanner.
The grammar is tree-sitter/tree-sitter-c **v0.23.4** (the MAIN
tree-sitter org), pinned in `scripts/vendor.sh` with the npm binding
tarball sha256 `31f07c7c...` as the final fallback; the npm package and
the git tag are byte-identical for `src/parser.c`, `src/node-types.json`
and `grammar.js` (SPRINT-20 cross-check). LANGUAGE_VERSION 15, accepted
by the runtime v0.27.2. `src/` holds exactly `parser.c`, `grammar.json`,
`node-types.json` and `tree_sitter/` -- NO external scanner, so
`oc_add_grammar`'s scanner pickup finds nothing. Every shape fact below
carries a VERIFIED block from `scripts/probe_c.c` (seven probe files,
compiled against the vendored runtime and parser with
`-D_DEFAULT_SOURCE -I third_party/tree-sitter-c/src`; the one deliberate
error case -- `#include PREFIX "extra.h"` concatenation -- is a genuine
grammar-level ERROR node, the boundary documented in section 5).

### 1. Grammar

VERIFIED: the root node is `translation_unit`. Preprocessor directives
are first-class AST nodes (`preproc_include`, `preproc_def`,
`preproc_function_def`, `preproc_if`, `preproc_ifdef`, `preproc_else`,
`preproc_elif`) -- the analysis runs on RAW SOURCE, no preprocessing is
performed, and conditional-compilation variants are reported as the
single parsed configuration (as-written analysis, section 8).

### 2. Functions & spans

VERIFIED shapes:
- `function_definition`: fields `type`, `declarator` (a `_declarator`
  chain), `body` (`compound_statement`). C has NO name field -- the
  symbol name is the identifier at the BOTTOM of the declarator chain,
  resolved by descending `declarator` fields (with a positional fallback
  for the `parenthesized_declarator` wrapper, which packs its inner
  declarator as a bare named child -- `seq('(', _declarator, ')')`).
  VERIFIED chains: plain (`void f(void)` -- fd -> identifier),
  pointer-return (`char *dup(const char *)` -- pointer -> fd ->
  identifier), parenthesized, and function-pointer-return
  (`int (*get_step(int n))(int)` -- fd -> parenthesized -> pointer ->
  fd -> identifier). The whole definition is the span (OBS-2).
- K&R old-style definitions are aliased to the regular
  `function_definition`/`function_declarator` nodes and are symbols
  through the same query; their `parameter_list` carries bare
  `identifier` NAMES (types are declared after the closing paren) --
  each pushes its name with the type honestly unknown.
- **Prototypes are NOT symbols.** A declaration whose declarator is a
  `function_declarator` without a body (a prototype, in a `.h` or atop a
  `.c`) is module SURFACE (section 6), never an OC001 symbol. Unlike the
  Java abstract-method case (where the interface IS the contract and no
  definition exists anywhere in the analyzed tree), a C prototype's
  definition normally exists in the same analysis root; counting both
  would double every function and give OC007 two anchors per name (one
  uncovered from the header). `q_bodyless` stays NULL for C.
- Parameters (inside `parameter_list`; `parameter_declaration` with
  fields `type` and `declarator`; commas anonymous): the compat (B3)
  signature records the declarator-chain bottom identifier and the
  AS-WRITTEN type slice (source from the parameter_declaration's start
  to the declarator's start, whitespace collapsed -- `const char` for
  `const char *fmt`; the pointer is declarator syntax and stays out, the
  documented boundary). UNNAMED (abstract) parameters record the `...`
  marker with the same slice (`int` for `int`, `char` for `char *`); a
  `variadic_parameter` collapses to the `...` marker; an unrecognized
  named child collapses to ONE marker so arity stays caller-visible.
- **The function-pointer-return shape carries TWO `function_declarator`s
  and TWO parameter lists**, and both are honest AST facts that are NOT
  the same list: the OUTER declarator is the RETURNED function pointer's
  type (its abstract `(int)` list), the INNER one -- adjacent to the
  identifier -- is the function's own caller-visible list (`(int n)`).
  `c_own_param_list` descends the full chain and takes the LAST
  function_declarator (VERIFIED by fixture; the generic preorder finder
  would take the wrong one).
- Main-method parse fact: a function DEFINITION (body required -- a lone
  prototype claims nothing) whose plain-identifier name is `main` (the
  Go `package main` class). C functions cannot nest, so no anchor is
  needed; a main behind an `#ifdef` wrapper still claims (the honest
  over-claim direction; OC006 severity is INFO and triage decides).

### 3. Comments

VERIFIED: line, block and doc comments are all ONE `comment` node kind
(the shared Q_COMMENTS matches it; q_comments stays NULL; consumed
verbatim, no doc model).

### 4. Complexity (cyclomatic, register-not-train)

The decision table, all VERIFIED named-node kinds:
- `if_statement` = 1. VERIFIED: `else if` is a NESTED `if_statement`
  inside an `else_clause` -- each branch increments by itself (the Go
  discipline); a bare `else_clause` (no nested if) = 0.
- `while_statement` = 1; `do_statement` = 1 (the do-while's `while` is
  part of the do_statement node, never a second decision);
  `for_statement` = 1.
- `switch_statement` itself = 0; each `case_statement` WITH a `value`
  field = 1 (VERIFIED: fall-through `case 1: case 2:` is two nodes, two
  decisions); **a `default:` arm is the SAME node type WITHOUT the
  `value` field and counts 0** (the family guard in the walker -- the
  gocyclo discipline).
- `conditional_expression` = 1 (the ternary). `&&` and `||` are
  anonymous operator tokens inside the shared `binary_expression`
  container (the CX_OPS_C list; no `cx_ops_node` override -- standard C
  has no keyword operator spellings, iso646.h is a header the analysis
  never preprocesses). `goto` is not a decision (the standing
  throw-not-a-branch rule).
- **Preprocessor conditionals (`preproc_if`, `preproc_ifdef`,
  `preproc_elif`) are NOT decisions**: they select code at COMPILE time,
  the analysis runs on raw source, and no preprocessed configuration
  exists to tie a count to. The RUNTIME statements they guard still
  count (the walker sees the whole body subtree through the preproc
  wrappers) -- VERIFIED by fixture.
- Anonymous-keyword collision: none -- every decision kind string is a
  named node; the SPRINT-16 named-only kind match stays.

### 5. Imports & resolver semantics (v1-c, contract pre-committed)

One directive kind, two static channels, both VERIFIED:
1. **the quoted form** (`#include "p.h"`): the `path` field is a
   `string_literal`; recorded as `include <p>` (the channel tag travels
   in the specifier -- the Rust `mod ` precedent). Resolution: joined
   against the INCLUDING FILE's directory (the C preprocessor's own
   rule: the directory of the file containing the directive, NOT the
   .c file's -- VERIFIED by the nested-include fixture), the join
   anchored at the analysis root so the lexical normalizer pops `..`
   across directories (the PHP include-channel discipline).
2. **the angle form** (`#include <stdio.h>`): the `path` field is a
   `system_lib_string` (a LEAF whose text carries the brackets);
   recorded as `system <p>` and NEVER resolved -- system headers are
   outside the analyzed set by definition, recorded-and-unresolved (the
   Go module-boundary precedent; the dependency signal stays visible in
   the import records without fabricating a manifest tier).
- **A macro path records NOTHING**: `#include HEADER` (an `identifier`
  path) and `#include PREFIX "x.h"` (a concatenation -- a genuine
  grammar-level ERROR node) are not static payloads; the shape guard
  records nothing, never guessed. Payload guards (the PHP/Ruby
  discipline): non-empty, no control characters, no backslash
  separators, no absolute payloads.
- Each include pushes ONE bind with a NULL name (a whole-file claim:
  including a header makes all its declarations visible -- the Ruby
  require discipline). The imports relation is a SET (a header included
  twice collapses to one edge). No mate marking (link order is not a
  parse fact).
- No manifest is ever read (none exists for C, section 6).

### 6. Surface, entry points, coverage, manifests

- **Surface:** top-level declarations AS PARSED, with C's own
  internal-linkage exportedness filter -- a `static` storage-class
  specifier narrows to the file (NOT module surface, the C# public
  modifier precedent inverted: C's default is external, `static`
  narrows; `extern` does not). The top-level check climbs preprocessor
  wrappers (the conventional `#ifndef` include guard wraps EVERYTHING in
  a header) and `extern "C"` blocks -- single-declaration and
  declaration_list forms -- through the parent chain; the immediate
  non-wrapper parent must be `translation_unit` (VERIFIED by the
  guarded-header fixture). Captured: function definitions (the
  declarator-chain identifier), declarations (the tag of a DEFINED
  struct/union/enum in the type field -- a bare `struct point p;`
  merely USES the tag and records nothing -- plus EVERY declarator-field
  child separately: `int a, b = 2, c[4];` is three names, VERIFIED),
  type definitions (the declarator-chain name; `type_identifier` or
  deeper for function-pointer typedefs), direct-child
  struct/union/enum tag definitions (name AND body), and object-like /
  function-like macros (the `name` field).
- **The enumerator boundary:** enum constant names (`enumerator`) are
  file-scope in C but are NOT module surface in v1 -- a documented
  boundary, pinned by fixture.
- **Entry points:** NO name-glob convention -- `entry_points` is EMPTY.
  The entry claim is the PARSE FACT of a function definition named
  `main` (section 2); no basename glob can express "defines main".
- **Coverage:** OC007 accepts Cobertura-shaped XML through the EXISTING
  reader (language-blind): `<class filename=...>` records + `<line
  number= hits=>` children. **gcovr emits Cobertura XML natively**
  (`--xml`), so a gcovr report works through the existing reader with
  zero new code (VERIFIED by the gcovr-shaped fixture in
  `tests/fixtures_sprint20/`); lcov tracefiles (`.info`) are out of
  scope in v1 and are rejected as unrecognized.
- **Manifests: NONE.** C has no canonical dependency manifest -- OC009
  and OC010 are enabled pack data, but there is no C manifest reader,
  so nothing is ever discovered for them (no fabricated pkg-config or
  vendored tier: a versionless include is not a pin and a vendored tree
  is not a manifest). Without their inputs the passes produce nothing
  and the report meta says so (the supply block is ABSENT with zero
  manifests; with a snapshot loaded the block reports
  `manifests_scanned: 0`). No OSV ecosystem exists for C includes and
  none is invented. OC010's licence-file probe (LICENSE/COPYING
  basenames) is a manifest-reader convention and is deferred with the
  same rationale.
- **Pack:** `packs/c/pack.json` targets full scope OC001-OC011 (the
  SIXTH full-scope pack) with extensions `.c` AND `.h`.

### 7. Enabled passes (pack data, not code)

Full scope: OC001-OC011, gated by `packs/c/pack.json` exactly like the
PHP pack (the fifth full-scope precedent). OC009/OC010 produce nothing
without their inputs (honest empty tier, section 6).

### 8. Honesty invariants carried over

- Parse errors are loud: a file that fails to parse keeps its zero
  complexity and reports the error (nothing silently degrades). Real
  C interleaves preprocessor conditionals with code at every level --
  including INSIDE expressions and between `else` and its statement --
  and closes `extern "C" {` inside `#ifdef __cplusplus`; a raw-source
  grammar cannot fully parse those shapes. The corpus gate pins the
  measured error rate (203 of curl 7.88.1's 799 C files) as the
  kill-switch: a grammar or tool regression MOVES it. No canonical C
  corpus parses 100% clean as-written (zlib 46/53, musl 382/2248,
  sqlite tree 230/436 -- measured at pin time).
- As-written analysis: no preprocessing is performed;
  conditional-compilation variants are reported as the single parsed
  configuration; X-macros, token pasting and macro-generated
  declarations are not parse facts (the Rust macro boundary, accepted
  in the over-report direction for OC006).
- Unresolved includes stay recorded-and-unresolved; nothing silently
  degrades; honesty counters unchanged.

### 9. SPRINT-20 adoption addendum (decisions locked at adoption time, all probed or gated)

- **The `.h` KNOWN FUTURE BOUNDARY**: `.h` files belong to the `c` pack
  while C++ is unregistered -- the routing is unambiguous and no content
  sniffing exists yet. When the `cpp` pack registers, `.h` routing gets
  the content-sniff + project-root-default treatment written into the
  report (the approved C/C++ architecture decision); until then every
  `.h` is C. Mixed C/C++ projects are the design goal of that decision;
  mixed fixtures are a C++ sprint gate criterion.
- **Prototypes are not symbols** (section 2): the honest
  double-count rationale, pinned by fixture (a prototype-only file
  produces zero OC001 symbols and its name is module surface).
- **The `parenthesized_declarator` wrapper has NO `declarator` field**
  (grammar: `seq('(', optional(ms_call_modifier), _declarator, ')')` --
  a positional named child). The name resolution falls back to the
  wrapper's first named child; every function-pointer shape in the
  fixtures pins it.
- **The enum-constant boundary** (section 6): `enumerator` names are
  file-scope in C but not module surface v1 -- recorded here so the
  decision is a contract, not an oversight.
- **Test conventions: none registered in v1** (no runner-executed C
  convention is name-pinned; a `*_test.c` name claims nothing -- the
  fixture pins the honest OC006 over-report).
- **The corpus gate's honest delta from the PHP recipe** (section 8):
  the parse-error criterion is the PIN, not zero -- real C cannot be
  fully parsed without preprocessing, and the as-written discipline is
  the documented trade.

---------------------------------------------------------------------------


## C++ language contract (wave-2 close-out, adopted SPRINT-21 in one session; probed against the pinned grammar before any adoption code)

The SPRINT-19 architecture decision is now fully executed: **separate
`c` and `cpp` packs**, with the `.h` routing treatment (content sniff +
project-root default, written into the report) implemented this sprint
(section 9). The grammar is tree-sitter/tree-sitter-cpp **v0.23.4** (the
MAIN tree-sitter org, the same version number as the C pin), pinned in
`scripts/vendor.sh` with the npm binding tarball sha256 `615d152f...` as
the final fallback; the npm package and the git tag are byte-identical
for `src/parser.c`, `src/scanner.c`, `src/node-types.json`, `grammar.js`
and `LICENSE` (SPRINT-21 cross-check). LANGUAGE_VERSION 14, accepted by
the runtime v0.27.2. `src/` holds `parser.c`, `scanner.c` (the EXTERNAL
scanner -- raw strings, angle-bracket disambiguation; `oc_add_grammar`
picks it up automatically, the C#/Rust/Ruby/PHP precedent),
`grammar.json`, `node-types.json` and `tree_sitter/`. The FULL C grammar
is inlined at generation time (`grammar.js` requires
`tree-sitter-c/grammar`), so the generated parser is self-contained and
every C statement/expression node kind exists verbatim in the cpp tree.
Every shape fact below carries a VERIFIED block from
`scripts/probe_cpp.c` (ten probe scenarios, compiled against the
vendored runtime, parser AND scanner with
`-D_DEFAULT_SOURCE -I third_party/tree-sitter-cpp/src`; the one
deliberate error case -- `#include PREFIX "extra.hpp"` concatenation --
is a genuine grammar-level ERROR node, the same boundary as C section 5;
nine of the ten scenarios parse clean, including concepts, raw strings,
lambdas and digit separators).

### 1. Grammar

VERIFIED: the root node is `translation_unit`. The preprocessor nodes of
C section 1 carry over verbatim (`preproc_include`, `preproc_def`,
`preproc_function_def`, `preproc_if`, `preproc_ifdef`, `preproc_else`,
`preproc_elif`) -- the analysis runs on RAW SOURCE, no preprocessing is
performed, and conditional-compilation variants are reported as the
single parsed configuration (as-written analysis, section 8). C++
template constructs are first-class AST nodes: `template_declaration`
(a WRAPPER around the following declaration), `template_parameter_list`
with `type_parameter_declaration`, `variadic_parameter_declaration` and
`optional_parameter_declaration` (a template default), and
`concept_definition` with `requires` bodies (VERIFIED clean).

### 2. Functions & spans

VERIFIED shapes:
- `function_definition`: fields `type` (ABSENT for constructors),
  `declarator` (a `_declarator` chain -- the C model), `body`
  (`compound_statement`). The symbol name is the identifier at the
  BOTTOM of the declarator chain, resolved by the SHARED walk
  (`c_declarator_ident`, extended SPRINT-21 -- C behavior unchanged
  byte for byte because none of the four cpp node kinds can occur in a
  tree-sitter-c tree):
  - **`field_identifier`**: the inline method's name (`struct Widget {
    int scale(int v) {...} }` -- the definition sits inside the class's
    `field_declaration_list` and is a SYMBOL like any other function,
    the C# method precedent);
  - **`qualified_identifier`** (`void Widget::scale(int v)`): the name
    is the `name` FIELD child -- the positional fallback would wrongly
    take the scope's `namespace_identifier`, so the field is checked
    explicitly (VERIFIED);
  - **`destructor_name`**: carried AS WRITTEN (`~Widget`) -- returning
    the bare identifier would collide with the constructor of the same
    class (VERIFIED);
  - **`operator_name`**: carried AS WRITTEN (`operator==`) -- the node
    has no identifier child at all (both tokens are anonymous).
- **Templated functions are symbols**: `template_declaration` wraps the
  `function_definition` and the query matches the inner definition; the
  canonical span is the inner definition extent (the wrapper adds no
  extent, VERIFIED by fixture).
- Constructors carry NO `type` field -- the query does not require one
  (VERIFIED); a constructor definition is a symbol named exactly like
  its class.
- **Lambdas are NOT symbols** (anonymous constructs -- the Ruby blocks
  precedent): `lambda_expression` (with `lambda_capture_specifier`,
  optional `trailing_return_type`) is never a symbol; its decisions
  count inward (the walker sees the whole enclosing body's subtree).
- **Prototypes and member declarations are NOT symbols**: a declaration
  with a `function_declarator` and no body (a free prototype, a class
  member declaration) is module SURFACE (section 6), never an OC001
  symbol -- the C discipline carries over (the definition normally
  exists in the same analysis root). `q_bodyless` stays NULL for cpp.
- Parameters (inside `parameter_list`; commas anonymous): the compat
  (B3) signature records the declarator-chain bottom identifier and the
  AS-WRITTEN type slice (the C slice semantics, shared:
  `c_param_type_text`). C++ additions, VERIFIED:
  - **`optional_parameter_declaration`** (`int b = 2`): the
    default-argument shape -- the name and type slice work identically
    and the DEFAULT flag is recorded (the B3 signature's third slot), so
    a signature diff flags default-value changes;
  - **`variadic_parameter_declaration`** (`Args &&...args`): the
    parameter PACK -- collapses to the `...` marker (the PHP
    convention; arity stays caller-visible, markers stay out of name
    diffs), exactly like the bare `variadic_parameter`;
  - **`reference_declarator`** (`int &count`): descends through the
    positional fallback like any fieldless declarator wrapper; the
    reference token stays declarator syntax out of the type slice (the
    pointer boundary, shared).
- The function-pointer-return shape shares C's two-parameter-list
  semantics verbatim (`c_own_param_list` takes the LAST
  `function_declarator`; VERIFIED by fixture).
- Main-method parse fact: a FREE-STANDING function DEFINITION (body
  required) whose plain-identifier name is `main`. The Q_C_MAIN query
  text compiles against the cpp grammar and the identifier-only inner
  declarator keeps C++ member functions (field identifiers),
  out-of-class definitions (qualified identifiers) and destructors out:
  a class with a method `main` claims NOTHING -- VERIFIED by fixture
  (the honest reading of the C++ entry model; OC006 severity is INFO).

### 3. Comments

VERIFIED: line, block and doc comments are ONE `comment` node kind --
the shared `Q_COMMENTS` already matches it, `q_comments` stays NULL
(the C precedent).

### 4. Complexity (cyclomatic, register-not-train)

VERIFIED: C's decision table carries over node-for-node (the C grammar
is inlined): if_statement (`else if` is a NESTED if_statement inside an
else_clause -- each branch increments by itself; a bare else_clause is
not a decision), while/do/for, case_statement WITH the `value` field (a
`default:` arm is the SAME node type WITHOUT it and counts 0 -- the
family guard now fires for BOTH C and cpp families), the ternary
`conditional_expression`, plus `&&`/`||` inside the shared
`binary_expression` container. C++ ADDITIONS (VERIFIED):
- **`for_range_loop`**: the C++11 range-for is its OWN node type (not a
  for_statement) -- one decision each;
- **`catch_clause`**: ONE decision per handler (two handlers = two
  decisions; `try` is not a decision -- the PHP catch discipline).
Preprocessor conditionals are NOT decisions (the C boundary). A
lambda's decisions count inward (anonymous constructs, section 2).

### 5. Imports & resolver semantics (v1-cpp, the v1-c channel shared)

VERIFIED: the include directive is the SAME `preproc_include` node with
the same `path` shapes (`string_literal` / `system_lib_string` /
`identifier`) -- C++ uses the preprocessor's include model unchanged.
The shared `c_include_shape` guard records:
1. **the quoted form** -> `include <p>`, resolved against the INCLUDING
   file's directory (the C preprocessor's own rule; root-anchored
   lexical `..` pops, the PHP include-channel discipline);
2. **the angle form** -> `system <p>`, NEVER resolved
   (recorded-and-unresolved, the Go module-boundary precedent);
3. **a macro path records NOTHING** (not a static payload); payload
   guards shared with C (non-empty, no control characters, no backslash
   separators, no absolute payloads).
Each include pushes ONE NULL-name bind (the whole-file claim, the Ruby
require discipline); the imports relation is a SET. No mate marking.
**The `using` family records NOTHING in v1** (VERIFIED shapes:
`using_declaration` -- both `using NS::x;` and `using namespace std;`
parse as using_declaration nodes in this grammar, `alias_declaration`
for `using X = Y;`, namespace aliases): no honest file mapping from a
namespace to files exists without a manifest, and none is invented --
the recorded boundary of the C++ module model's absence. No manifest is
ever read (none exists for C++ includes, section 6).

### 6. Surface, entry points, coverage, manifests

- **Surface:** top-level declarations AS PARSED, with C++'s own
  internal-linkage exportedness filter: the C `static` rule (shared
  helper) PLUS the anonymous-namespace rule -- a candidate whose parent
  chain reaches an ANONYMOUS `namespace_definition` (no `name` field)
  is internal linkage and never surface (the C `static` rule
  generalized; applied by the climb itself, VERIFIED by fixture). The
  top-level check (`c_is_toplevel`, extended SPRINT-21 -- C behavior
  unchanged because none of the three wrapper kinds can occur in a C
  tree) climbs preprocessor guards, `extern "C"` blocks (single
  declaration AND declaration_list forms), **named namespace bodies**
  (the body is a `declaration_list`; the PHP namespace precedent: the
  members of a named namespace are the file's surface), and
  **`template_declaration`** wrappers. Captured: function definitions
  (the declarator-chain identifier -- inline methods are fdef captures
  whose climb FAILS inside the class body: the class is the surface
  unit, the C# type-nesting precedent), declarations (the tag of a
  DEFINED specifier in the type field + EVERY declarator-field child
  separately), type definitions, **`alias_declaration`** (`using X =
  Y;` -- C++'s typedef spelling; the alias NAME is surface, the RHS
  type is not), struct/union/enum/**class** specifiers with BOTH a name
  and a body, and macros. `using_declaration` / using-directive /
  namespace aliases are NOT captures (never surface).
- **The enumerator boundary carries over:** enum-class constants are
  file-scope in C++ but NOT module surface v1 (the C boundary, pinned).
- **Entry points:** NO name-glob convention -- `entry_points` is EMPTY
  (the m3_gate loader half requires PRESENCE, not non-emptiness, since
  the SPRINT-20 ritual fix). The entry claim is the PARSE FACT of a
  free-standing function definition named `main` (section 2).
- **Coverage:** OC007 accepts Cobertura-shaped XML through the EXISTING
  reader (language-blind) -- gcovr emits Cobertura XML natively for C++
  projects exactly as for C (VERIFIED by the gcovr-shaped cpp fixture in
  `tests/fixtures_sprint21/`); lcov tracefiles are out of scope v1.
- **Manifests: NONE in v1.** C++ has no canonical dependency manifest
  OmniCheck reads -- OC009 and OC010 are enabled pack data, but there is
  no C++ manifest reader, so nothing is ever discovered for them (no
  fabricated pkg-config tier; **vcpkg.json / conanfile.py are NOT
  canonical pins in v1** -- a recorded boundary, not an oversight: a
  vendored dependency tree is not a manifest and a registry name is not
  a pinned artifact). Without their inputs the passes produce nothing
  and the report meta says so (the supply block is ABSENT with zero
  manifests; with a snapshot loaded it reports `manifests_scanned: 0`).
  No OSV ecosystem exists for C++ includes and none is invented. The
  OC010 licence-file probe is a manifest-reader convention and is
  deferred with the same rationale.
- **Pack:** `packs/cpp/pack.json` targets full scope OC001-OC011 (the
  SEVENTH full-scope pack, wave-2 close-out) with extensions `.cpp`,
  `.cc`, `.cxx`, `.c++`, `.hpp`, `.hh`, `.hxx` and `.h`.

### 7. Enabled passes (pack data, not code)

Full scope: OC001-OC011, gated by `packs/cpp/pack.json` exactly like the
C pack. OC009/OC010 produce nothing without their inputs (honest empty
tier, section 6). `oc packs` carries 12 packs; `OC_PARSER_MAX_LANGS` is
12; `OC_LANG_CPP = 9`.

### 8. Honesty invariants carried over

- Parse errors are loud: a file that fails to parse keeps its zero
  complexity and reports the error (nothing silently degrades). Real
  C++ interleaves macros INSIDE declarations at a density C never
  reaches -- canonical idioms like
  `ABSL_NAMESPACE_BEGIN`/`ABSL_ATTRIBUTE_*`/`ABSL_FLAG_*` build
  namespaces and decorate declarations from macros a raw-source grammar
  cannot see through. The corpus gate pins the measured error rate (646
  of abseil 20240722.0's 817 analyzed files, classified: 581 the
  macro-namespace class, 207+ the macro attribute/flag classes, token
  pasting / X-macros / residual) as the kill-switch: a grammar or tool
  regression MOVES it. No canonical C++ corpus parses 100% clean
  as-written (catch2 3.7.1: 135/427; spdlog 1.15.2: 74/132 -- measured
  at pin time).
- As-written analysis: no preprocessing; template instantiation and
  macro expansion produce no parse facts beyond the as-written tree
  (the Rust macro boundary, accepted in the over-report direction for
  OC006).
- Unresolved includes and the whole `using` family stay
  recorded-and-unresolved (or unrecorded for the using family); nothing
  silently degrades; honesty counters unchanged.

### 9. SPRINT-21 adoption addendum (decisions locked at adoption time, all probed or gated)

- **The `.h` routing treatment (the approved architecture, implemented
  this sprint):** BOTH packs claim `.h`; routing for that one extension
  goes through the treatment in `oc_packs_route_source`
  (`src/pack.c`):
  1. **the content sniff** (`oc_parser_sniff_header`, parse facts
     only): a CLEAN cpp parse carrying at least one C++-distinctive node
     kind (`namespace_definition`, `class_specifier`,
     `template_declaration`, `alias_declaration`, `using_declaration`,
     `concept_definition`, `catch_clause`, `for_range_loop`) routes to
     cpp; a content that ERRORS under cpp while parsing clean under C
     routes to c (the probed C-positive class is the K&R old-style
     definition -- keyword identifiers do NOT work: tree-sitter's error
     recovery parses `int class = 3;` clean under cpp);
  2. **the project-root default** (inconclusive sniff): cpp iff the
     DISCOVERED file set carries at least one file with a NON-`.h`
     cpp-pack extension (the project uses C++), else c. The default is
     SCAN-WIDE by design: one C++ source in the tree claims every
     inconclusive header for the C++ grammar (which reads C-compatible
     code correctly -- the C subset), and the routing stays one
     deterministic decision per file.
  The COUNTS land in the report meta's `header_routing` block (JSON)
  and the human `header routing:` line -- never silent. Every other
  path routes as a plain extension match. The routing decision is
  computed ONCE per file and reused by the pass loop (the routed pack,
  not a fresh extension match); `oc graph`, the compat signature scan
  and the mutation collector apply the SAME treatment (one routing
  decision per file everywhere). A build without the tree-sitter
  backend sniffs inconclusive and the root default decides (the
  documented fallback). Measured on the reference corpora: abseil's 359
  headers split 1 sniffed / 358 default-cpp; curl's 232 headers all
  default-cpp (its one `docs/examples/htmltitle.cpp` activates the
  root default) -- the C corpus gate's re-pin documents the delta.
- **The C-pack freeze honored:** the SPRINT-20 corpora scan UNCHANGED
  with the cpp pack registered (the lang gate's criterion 9): fixtures/c
  keeps every contracted count with both `.h` files routed by the root
  default to C, and the v1-c resolver semantics are untouched (the cpp
  family SHARES the implementation -- no C code path changed).
- **`q_main` is Q_CPP_MAIN, textually Q_C_MAIN**: one query constant
  per language (the table convention) with a cpp-specific comment; the
  family branch in the main-hint loop covers both C and cpp.
- **Test conventions: none registered in v1** (no runner-executed C++
  convention is name-pinned; a `*_test.cpp` name claims nothing -- the
  fixture pins the honest OC006 over-report).
- **The corpus gate's honest delta** (section 8): the parse-error
  criterion is the PIN, not zero -- real C++ cannot be fully parsed
  without preprocessing, and the as-written discipline is the
  documented trade; the corpus is Debian abseil 20240722.0 (811
  .cc/.h), the metaprogramming-heaviest of the measured pool, with the
  error classes classified in the gate docstring.

## Kotlin language contract (wave-3 opens, adopted SPRINT-22 in one session; probed against the pinned grammar before any adoption code)

The Wave-2 freeze is honored: C and C++ keep their SPRINT-20/21 corpora,
counts and routing (the freeze legs in the SPRINT-22 language gate), and
Kotlin lands as a SEPARATE pack -- the SPRINT-19 one-language-one-pack
architecture. The grammar is fwcd/tree-sitter-kotlin **0.3.8** (the
canonical community grammar -- the MAIN tree-sitter org ships no Kotlin
grammar), pinned in `scripts/vendor.sh` with the npm binding tarball
sha256 `e2da896c...` as the final fallback; the npm package and the git
tag are byte-identical for `src/parser.c`, `src/scanner.c`,
`src/node-types.json`, `src/grammar.json` and `LICENSE` (SPRINT-22
cross-check). LANGUAGE_VERSION 14, accepted by the runtime v0.27.2 (the
same version the cpp pin carries). `src/` holds `parser.c`, `scanner.c`
(the EXTERNAL scanner -- automatic semicolons, string-interpolation
start/end/content, nested multiline comments, the safe-navigation token;
`oc_add_grammar` picks it up automatically), `grammar.json`,
`node-types.json` and `tree_sitter/`. Every shape fact below carries a
VERIFIED block from `scripts/probe_kotlin.c` (ten probe scenarios,
compiled against the vendored runtime, parser AND scanner with
`-D_DEFAULT_SOURCE -I third_party/tree-sitter-kotlin/src`; nine of the
ten scenarios parse clean, the one deliberate error case being the
broken-parameter error-recovery scenario). Two pinned grammar boundaries
measured at adoption: an in-range `when_condition` cannot FOLLOW an
expression when_condition entry (fwcd 0.3.8; the corpus gate classifies
the survivors), and one-line class bodies produce zero-width MISSING
automatic-semicolon nodes (the scanner requires the newline; real
Kotlin source always uses newlines -- ktlint forbids one-line bodies).

### 1. Grammar

VERIFIED: the root node is `source_file`. Top-level children: the
`package_header` (one `identifier` carrying the dotted package chain as
nested `simple_identifier`s joined by anonymous `.` tokens), the
`import_list` (a wrapper of `import_header`s -- each header is `import`
+ the dotted `identifier` + an optional `wildcard_import` (`.*`) or
`import_alias` (`as` + `type_identifier`)), and the declarations
(function_declaration, class_declaration, object_declaration,
property_declaration, type_alias). Classes, interfaces AND enum classes
are ALL `class_declaration` nodes in the pinned grammar (`interface` and
`enum` are anonymous tokens before the class token -- VERIFIED);
modifiers live in a `modifiers` wrapper of named modifier nodes
(`visibility_modifier` with the ANONYMOUS `private` token inside, the
`kt_is_private` shape; `member_modifier` for open/abstract/override
etc.). Lambdas are `lambda_literal` (often under `annotated_lambda`),
anonymous functions `anonymous_function` -- anonymous constructs, the
Ruby blocks precedent. String templates carry
`interpolated_identifier`/`interpolated_expression` children inside
`string_literal` (clean, probed); raw strings (`"""..."""`) parse via
the scanner; nested block comments (`/* /* */ */`) are the scanner's
`multiline_comment`.

### 2. Functions & spans

VERIFIED shapes:
- `function_declaration`: children `modifiers`?, `fun`, the
  `simple_identifier` NAME, optional receiver (`user_type` + `.` for
  extension functions -- the receiver lives INSIDE the user_type, so
  the declaration's ONLY direct `simple_identifier` is the name),
  `function_value_parameters`, optional return type, optional
  `function_body`. The body is EITHER a block (`{ statements }`) or an
  `=`-expression -- the SAME `function_body` node.
- The SYMBOL set: function_declaration WITH a function_body -- free
  functions, methods, extension functions, generic/suspend/inline
  functions (modifiers and type_parameters are separate children; the
  wrapper adds no extent, the C++ template precedent). A body-less
  declaration (abstract/interface members) is NOT a symbol -- Kotlin
  has no prototypes, so every real definition carries a body; the C
  prototype discipline applies unchanged.
- Lambdas and anonymous functions are NOT symbols; their decisions
  count INWARD (inclusive, the Ruby blocks precedent -- verified by the
  lambdaUser unit fixture: comp 1 with two inner constructs).
- Property ACCESSORS with a body ARE symbols (the C# SPRINT-14
  accounting-hole precedent: accessor decisions previously counted
  toward NOTHING). The pinned grammar parses the accessor as a SIBLING
  of its `property_declaration` under the same body node (class_body /
  source_file / enum_class_body) -- NOT as its child (probed:
  ts_node_parent(getter) is class_body); the owner is the accessor's
  nearest PRECEDING property_declaration sibling, the honest qualified
  name is `Prop.get` / `Prop.set` (the keyword, not the node-type
  spelling). An initializer-only property (`val x = 3`) is no accessor;
  a body-less accessor never matches.

### 3. Parameters & signatures

VERIFIED: `function_value_parameters` carries `parameter` nodes; the
pinned `parameter` node has NO fields -- the name is the direct
`simple_identifier` child, the type the first type-ish child
(`user_type`, `nullable_type`, `not_nullable_type`,
`parenthesized_type`, `function_type` -- carried as written, so
`List<String>` and `(Int) -> Unit` surface verbatim). The default value
trails the parameter at the LIST level: `b: Int = 2` is a parameter
followed by an anonymous `=` + the expression as SIBLINGS of the
parameter (probed) -- the extractor marks the preceding parameter
defaulted on the `=` (the B3 signature's third slot). The `vararg`
modifier wraps its parameter in a SIBLING `parameter_modifiers` node
(probed: the modifier is a separate child BEFORE the parameter); a
vararg parameter collapses to the `...` marker (the C++ pack
convention: arity stays caller-visible, markers stay out of name
diffs). An unrecognized shape collapses to ONE marker.

### 4. Complexity (decision table)

VERIFIED against the pinned grammar AND live parses:
`if_expression` (else-if is a NESTED if_expression -- the Java
discipline), `when_entry` (EVERY entry counts including the else arm --
the Rust match_arm precedent: each arm is an explicit branch; the
pinned grammar has no default-arm distinction), `for_statement`,
`while_statement`, `do_while_statement`, `catch_block` (each handler is
a branch -- the Java catch discipline; finally is not a decision),
`elvis_expression` (Kotlin's conditional-expression spelling, the C#
`??` op precedent -- but the pinned grammar gives it its OWN node type,
so it is a KIND here), and the two boolean-operator containers
`conjunction_expression` / `disjunction_expression` (Kotlin has NO
shared binary_expression: `&&` and `||` are distinct node types, so the
ops mechanism -- which counts anonymous tokens inside ONE container
type -- cannot express them; as kinds each named node counts exactly
once per occurrence, the identical semantics). The `in`-range when
CONDITION is not a decision of its own (only the ENTRY counts);
preprocessor-free language: no more kinds. Hand-counted pins:
comp(rollup fixture) = 14, comp(sum26 fixture) = 35 (34 decisions),
comp(pick2 two-arm when) = 3, comp(tracked.set with one if) = 2.

### 5. Imports & resolver v1-kotlin

VERIFIED: the import channel is `import_header`; the dotted
`identifier` child is the path AS WRITTEN (the pinned grammar keeps the
whole chain inside ONE identifier node). Shapes (the C-side
`kotlin_import_shape`):
- `import a.b.C` (two segments) -> spec `a.b.C`, binds `C` -- the
  unambiguous single-type record (the Java discipline).
- `import a.b.C` (three-plus segments) -> spec as written, binds NULL:
  the member reading is AMBIGUOUS at parse time (C may be an object
  carrying member f, or a package carrying top-level f) -- the
  whole-file claim direction, the C++ include discipline; the resolver
  matches BOTH declared readings.
- `import a.b.*` -> spec `a.b.*`, binds NULL -- the Java
  asterisk-marker package claim (fail-open).
- `import a.b.C as D` -> spec `a.b.C`, binds `C` -- the alias makes the
  claim UNAMBIGUOUS (Kotlin cannot alias a member, only a declaration);
  the alias is local surface detail and never a narrowing predicate
  (the Go alias note).

Resolver v1-kotlin is the DECLARED-FACT resolver. Kotlin's package
model is DECOUPLED from the file system (no package==directory rule
exists -- javac enforces one for Java, the Kotlin compiler does not;
the IDE warns on mismatch), so no directory probe is honest. The
resolver keys on the DECLARED parse facts recorded as hints: the
per-file `package_header` clause (the Go package_clause hint channel)
and the per-file top-level declaration names (the C# namespace-hint
channel, fed by the exports loop). Resolution:
- `a.b.*` -> every analyzed file whose declared package is `a.b` (the
  Java wildcard fan-out discipline).
- `a.b.C` -> every analyzed file whose declared package is `a.b` AND
  that declares a top-level `C`; several files may legitimately carry
  the same top-level name in one package, so the union fans out (set
  semantics, lexicographic order -- the Go fan-out discipline).
- `a.b.C.f` -> BOTH readings against the declared facts: package `a.b`
  declaring `C` (the object-member reading) and package `a.b.C`
  declaring `f` (the top-level reading) -- every recorded package that
  is a dotted prefix of the specifier claims its next segment.
- The one-segment `import C` claims the DEFAULT package (the empty
  package clause is the declared fact).
- Zero matches and NULL hints are both honest non-resolutions: without
  the hint vector (the plain resolve_ex path) every Kotlin specifier
  stays unresolved -- nothing is ever probed against directories, build
  files or manifests. The imports relation is a SET (duplicates
  collapse); self-edges are skipped defensively. NO mate marking (the
  C/C++ precedent; link order is not a parse fact).
- OC005 marking: wildcard claims and the ambiguous member shape mark
  whole files (fail-open, the Ruby require / C++ include discipline);
  named claims mark the matching export of each target file.
- The declared-fact consequence is real cycles: OC003 fired 18 times on
  the pinned corpus (SPRINT-22 corpus gate) -- the second pack after
  Java with live cycles at corpus scale.

### 6. Module surface

VERIFIED: the named top-level declarations. Kotlin has NO
internal-linkage filter beyond `private`: a private top-level
declaration is file-visible only and never surface (the C `static`
rule's Kotlin spelling); public (the default) and internal
(module-visible) surface. The top-level check is the DIRECT
source_file parent (Kotlin has no wrapper nodes -- no climb needed;
class members, nested declarations and companion objects fail it -- the
type-nesting precedent). Captured: function definitions (a top-level
function always has a body), class/interface/enum declarations (ALL are
class_declaration nodes -- probed), object declarations (named objects;
the anonymous companion object carries no name and never matches),
property declarations (the val/var member shape; accessors are the
property's parts, never surface), type aliases. Enum entries and class
members are not module surface. Every exported name is ALSO recorded as
a top-level-symbol parse hint (the resolver's declared facts). The
pack's `entry_points` is EMPTY BY DESIGN: the entry claim is the PARSE
FACT of a top-level function DEFINITION named `main` (the Q_KT_MAIN
fact, the Rust source_file-anchor shape; both `fun main()` and
`fun main(args: Array<String>)` match; a member `fun main` claims
nothing -- probed; the corpus cross-check measured the claim set live).

### 7. Coverage (OC007)

The pack accepts a Cobertura-shaped XML via `--coverage` through the
EXISTING reader (the C/C++ gcovr shape: class filename= records + line
number=/hits=; covered when hits > 0). Kover emits JaCoCo-compatible
XML; the JaCoCo XML reader is a recorded descope in v1 -- nothing is
invented for a format the reader does not take (the gcovr/SARIF-tier
honesty). The SPRINT-22 composition fixture
(`tests/fixtures_sprint22/cobertura_kotlin.xml`) pins the
composition: 18 functions, 2 measured, ONE OC007 on sum26 with
CRAP == comp == 35 at 100 percent coverage.

### 8. Supply tier (the honest delineation)

Kotlin has NO canonical dependency manifest reader in v1. Gradle build
files are NOT pins: a plugins block is not a version-pinned artifact and
`libs.versions.toml` is a version CATALOG, not a lockfile -- neither is
read. OC009 and OC010 are enabled pack data but the kotlin pack
discovers NOTHING for them: the kotlin pack contributes zero manifest
rows on any scan, and on a Kotlin-only scan the supply meta block is
ABSENT with zero manifests (on a mixed scan the block may carry the
OTHER packs' tiers -- the Java gradle reader scans build.gradle.kts on
the pinned corpus -- while the Kotlin contribution stays zero). No OSV
ecosystem exists for Kotlin imports and none is invented. OC010's
licence probe is a manifest-reader convention and is deferred with the
same rationale.

### 9. Honesty invariants

- As-written analysis: no build-system evaluation; gradle/Ktor variants
  are reported as the single parsed configuration.
- Metaprogramming (reflection, delegation, compiler plugins, DSLs,
  string-template code generation) is a parse fact only as far as the
  grammar sees it -- the Rust macro boundary, accepted in the
  over-report direction for OC006.
- Test conventions: none registered in v1 (no runner-executed Kotlin
  convention is name-pinned; a `*Test.kt` name claims nothing).
- The pinned grammar boundaries are MEASURED AND CLASSIFIED at the
  corpus gate (the 119-file boundary of the ktor corpus, seven classes
  -- the abseil discipline: the criterion is the PIN, not zero; a
  grammar or tool regression moves the pinned numbers and the per-file
  set, which is exactly what a kill-switch must do).
- Determinism: byte-identical reports across repeated runs (the lang
  gate x10, the corpus gate x5, single-core wall ~10.6s against the 60s
  budget on the pinned corpus).
- The probe-first discipline: every contract shape above was probed
  against the pinned grammar BEFORE the adoption code existed
  (`scripts/probe_kotlin.c`, ten scenarios; the negative facts pinned
  the design: keyword identifiers parse clean under error recovery,
  the one-line class-body MISSING boundary, the accessor sibling
  ownership).

## Swift language contract (wave-3 continues, adopted SPRINT-23 in one session; probed against the pinned grammar before any adoption code)

The Wave-3 freezes are honored: C, C++ and Kotlin keep their
SPRINT-20/21/22 corpora, counts and routing (the freeze legs in the
SPRINT-23 language gate), and Swift lands as a SEPARATE pack -- the
SPRINT-19 one-language-one-pack architecture. The grammar is
alex-pinkus/tree-sitter-swift **0.7.1-with-generated-files** (the
CANONICAL Swift grammar: the tree-sitter org's tree-sitter-swift is an
abandoned unrelated project whose own README redirects to alex-pinkus,
and every npm `tree-sitter-swift` version publishes from alex-pinkus;
the tag carries NO v prefix), pinned in `scripts/vendor.sh` with the
npm binding tarball sha256 `abd89284...` as the final fallback (the
fallback needs the PLAIN version `0.7.1` -- the npm package publishes
FROM the `-with-generated-files` tag); the npm package and the git tag
are byte-identical for `src/parser.c`, `src/scanner.c`,
`src/node-types.json`, `src/grammar.json` and `LICENSE` (SPRINT-23
cross-check, both hashes recorded in the vendor note). LANGUAGE_VERSION
14, accepted by the vendored runtime (ABI window 13..15). `src/` holds
`parser.c`, `parser_abi13.c`, `parser_abi14.c` (alternative-ABI
emissions, uncompiled), `scanner.c` (the EXTERNAL scanner -- block
comments, raw string parts, implicit/explicit semicolons, the
`&&`/`||`/`??` operator tokens, the `#if` directive tokens;
`oc_add_grammar` picks it up automatically), `grammar.json`,
`node-types.json` and `tree_sitter/`. Every shape fact below carries a
VERIFIED block from `scripts/probe_swift.c` (ten probe scenarios,
compiled against the vendored runtime, parser AND scanner with
`-D_DEFAULT_SOURCE -I third_party/tree-sitter-swift/src`; nine of the
ten scenarios parse clean, the one deliberate error case being the
broken-parameter error-recovery scenario). Three pinned grammar
boundaries measured at adoption: an import ALIAS does not parse
(`import struct Kit.Tag as T` -- the alias refine is a recorded
boundary, never guessed); the generated parser labels the return type
and a parameter's TYPE with the SAME `name` field as the declaration's
own name (the first-match rule disarms it); and a protocol
requirement's default body parses as a SIBLING wrapper node (a second
`protocol_function_declaration` with the `body` field, a direct
protocol_body child -- the adoption code pairs it with its adjacent
requirement through the query `.` anchor).

### 1. Grammar

VERIFIED: the root node is `source_file`. Top-level children: comments
(`comment` and `multiline_comment` -- block comments NEST in Swift, the
external scanner handles them), `import_declaration`s, directives
(`#if`/`#endif` -- the grammar FLATTENS them: the condition call is a
DIRECT source_file child sandwiched between the directive tokens), and
the declarations (function_declaration, class_declaration,
protocol_declaration, property_declaration, typealias_declaration).
Classes, structs, enums, extensions AND actors are ALL
`class_declaration` nodes in the pinned grammar (the anonymous
`declaration_kind` FIELD carries the keyword token -- VERIFIED;
`class Top` / `struct Point` / `enum Color` / `extension Int` /
`actor A` all parse as class_declaration with the name field carrying
the type name, the extension's as a `user_type`). Modifiers live in a
`modifiers` wrapper of named modifier nodes (`visibility_modifier`
with the ANONYMOUS `private`/`fileprivate`/`public`/`internal` token
inside -- the Kotlin shape verbatim; `member_modifier` for
static/class; `property_behavior_modifier` for lazy/weak, and
`property_modifier` for the `private(set)` spelling which stacks a
second visibility_modifier inside the SAME modifiers wrapper). A
top-level `let x = 1` / `var y = 2` parses as a `property_declaration`
(the stored global shape); `var z: Int { ... }` carries the
`computed_value` FIELD (a `computed_property` wrapper holding the
explicit `computed_getter`/`computed_setter` OR the implicit getter's
statements directly).

### 2. Functions & spans

VERIFIED: the canonical span is the whole `function_declaration`
(OBS-2, the one semantic for every language). A symbol is a
function_declaration WITH its `function_body` FIELD -- free functions,
methods, extension methods (the receiver is the extension declaration,
not part of the function: the declaration's name FIELD is the name),
static/class methods (the modifiers carry the keyword), nested
functions, generic functions (type_parameters are separate children;
the wrapper adds no extent, the C++ template precedent),
throws/async/rethrows functions (the `throws` is a NAMED node, `async`
an anonymous token -- neither changes the shape). Swift has no
prototypes, so every real definition carries a body. `init_declaration`
and `deinit_declaration` with a body ARE symbols (the C#
constructor_declaration precedent, SPRINT-13); the grammar gives `init`
no readable name node (the name FIELD is the anonymous `init` token),
so the C side synthesizes the honest `init` / `deinit` spellings. A
protocol REQUIREMENT (body-less) is NOT a symbol (the C prototype
discipline); a requirement WITH a default body IS a symbol (the
sibling-wrapper shape, paired by the query adjacency anchor -- the C#
interface-default precedent: a body carries decisions that would
otherwise count toward nothing; the span is the requirement's own
extent). Lambdas (`lambda_literal`) are anonymous constructs (the Ruby
blocks precedent): NOT symbols, decisions count inward. Property
accessor bodies ARE symbols (the C# SPRINT-14 accounting-hole
precedent): `computed_getter` / `computed_setter` with statements, the
IMPLICIT getter (statements directly in the computed_property -- the
no-`get`-keyword shape) surfaces as `Prop.get` too, and the observers
`willset_clause` / `didset_clause` surface as `Prop.willSet` /
`Prop.didSet`. The owner is reached by a PARENT CLIMB (the accessors
nest INSIDE the property_declaration -- unlike Kotlin's sibling walk);
the name is the owner property's pattern identifier (TWO pattern
shapes, both probed: `pattern > simple_identifier` directly and
`pattern > bound_identifier > simple_identifier` -- the walk takes the
first simple_identifier at bounded depth). An accessor with NO
statements (an empty `didSet { }`) never matches -- the body-less
accessor rule.

### 3. Parameters & signatures

VERIFIED: the `parameter` node is a DIRECT child of the declaration
(there is NO parameter-list wrapper -- `find_param_list` matches the
declaration itself). The name is the `name` FIELD (a direct
simple_identifier); an external argument label rides the separate
`external_name` FIELD and stays OUT of the name (the signature the
caller writes after the call-site label is the local name). The TYPE
is the named child AFTER the anonymous `:` token, skipping the
`parameter_modifiers` wrapper (the generated parser labels the TYPE
with the `name` field too -- the positional rule disarms the quirk).
The default value trails the parameter AT THE LIST LEVEL (the
declaration node IS the list: an anonymous `=` SIBLING followed by the
declaration's `default_value` field -- the Kotlin shape verbatim). A
trailing anonymous `...` child INSIDE the parameter marks the variadic
shape: the parameter collapses to the `...` marker (the C++ pack
convention). `inout` rides `parameter_modifiers` and does not change
the extraction.

### 4. Complexity (decision table)

The kind table (no `cx_ops` -- Swift has NO shared binary_expression):
`if_statement` (else-if is a NESTED if_statement -- probed, the Java
discipline), `guard_statement` (the early-exit branch is an explicit
branch), `ternary_expression` (the C# conditional_expression
precedent), `switch_entry` (EVERY arm counts including the default arm
-- the Rust match_arm precedent), `for_statement`,
`while_statement`, `repeat_while_statement` (Swift's do-while),
`catch_block` (each handler is a branch -- the Java catch discipline),
`nil_coalescing_expression` (the C# `??` op precedent -- but the
pinned grammar gives it its OWN node type, so it is a KIND here, the
Kotlin elvis_expression precedent), and the two boolean-operator
containers `conjunction_expression` / `disjunction_expression` (as
kinds each named node counts exactly once per occurrence -- the
identical semantics, verified by the hand-counted rollup fixture:
comp(rollup) = 14, comp(sum26) = 35, both pinned by test_sprint23 and
the language gate). `#if` compilation directives are NOT a decision
kind -- the C/C++/C# packs count no preprocessor conditional (the
recorded boundary). The corpus gate pins the measured counts (OC001
18, OC004 7 on swift-argument-parser 1.5.0).

### 5. Imports & resolver v1-swift (the declared-fact MODULE resolver)

VERIFIED: the `import_declaration` carries an anonymous `import` token,
an optional ANONYMOUS kind token (`class`/`func`/`struct`/`enum`/
`protocol`/`typealias`/`var`/`let`), the dotted `identifier` (whose
children are the `simple_identifier` SEGMENTS joined by anonymous `.`
tokens -- the C side joins the segment texts, the path AS WRITTEN),
and optional `modifiers > attribute` for `@testable` / `@_exported`.
The pinned grammar has NO import-alias shape (`import struct Kit.Tag
as T` does not parse -- probed boundary; nothing to classify, never
guessed).

Swift's import specifier names a compilation TARGET -- a fact NO
source file carries (there is no package_header analog and none is
invented). The declared facts are the MANIFEST-declared targets: every
analyzed `Package.swift` (SPM's own fixed filename -- not a heuristic)
contributes (target name, declared `path:`) pairs through the
target-hint channel (`oc_surface_hint_push_swift_target`, the SPRINT-22
symbols-channel extension); the extraction reads the
`.target` / `.executableTarget` / `.testTarget` calls' `name:` and
`path:` STRING literals only (the depth-bounded preorder walk; an
interpolated, raw or non-literal value records NOTHING). Each analyzed
Swift file joins its module by the DECLARED path -- an explicit
`path:` argument as written, else the SPM DOCUMENTED default
`Sources/<name>` (the same class of build-system rule the javac
package==directory mapping is; nothing is probed beyond what the
manifest declares). Files outside every declared target stay
module-less and are never import targets; the manifest file itself
carries no module. Import shapes (the C side classifies):
`import M` (whole-module) and `import M.Sub` (the submodule spelling
-- the FIRST segment is the module, the submodule refine is a recorded
v1 boundary) record spec `M` with a NULL bind: the WHOLESALE claim
(importing a module loads all of it -- the Kotlin wildcard fail-open
discipline). `import kind M.T` (kind-scoped with a member) records
spec `M` binding the LAST segment as the member name (the over-mark
direction, bounded by the declared facts -- the Kotlin single-type
record's Swift spelling). The resolution: exact declared-module match,
wholesale fan-out over the module's files (set semantics,
lexicographic order); self-edges are skipped; without hints every
specifier stays unresolved (the honest degradation). OC005 marking:
NULL-bind claims mark whole files; kind-scoped member claims mark the
matching export. The imports relation is a SET.

### 6. Module surface & the entry fact

Surface: the named TOP-LEVEL declarations only -- function definitions
(a top-level function always has a body), type declarations (class /
struct / enum / extension / actor via class_declaration, protocol,
typealias), stored globals (the top-level property_declaration shape),
and the top-level code bindings. `private` AND `fileprivate` never
surface (the C `static` rule's Swift spelling; public/open and
internal -- the DEFAULT -- surface). Type members, enum entries and
accessors never surface (the type-nesting precedent). Every exported
name is ALSO recorded as a top-level-symbol parse hint (the C#
namespace-hint channel): the resolver's named claims key on it.

Entry points: EMPTY manifest list BY DESIGN -- the entry claim is the
PARSE FACT of top-level code (the Q_SW_TOP fact: Swift's spec lets
exactly one file per module carry top-level statements -- the C# 9
global-statement precedent). The pinned grammar has no global_statement
wrapper, so the query matches the statement shapes as DIRECT
source_file children (call_expression, assignment, for/while/
repeat-while/if/guard/do/switch, control_transfer); a top-level `let`
is a property_declaration -- a DECLARATION, never a statement. THE
DIRECTIVE-CONDITION EXCLUSION: `#if compiler(>=5.9)` puts the
condition call on the source_file as a SIBLING of the directive node
(probed on real SPM manifests) -- the C-side directive-builtin callee
set (canImport/compiler/swift/os/arch/targetEnvironment/sourceLocation/
module/_compiler_version/_hasFeature) keeps the version check out: a
condition is not code. A conditionally-compiled STATEMENT still claims
(it IS executable top-level code). The corpus gate measures this
honestly: swift-argument-parser's Package.swift and
Package@swift-5.8.swift carry real `package.targets.append(...)`
statements and ARE claimed as entries (3 claimed files, each with a
top-level statement shape, cross-checked live).

### 7. Coverage (OC007)

gcovr/Cobertura XML through the EXISTING reader (the Kotlin SPRINT-22
precedent verbatim; Kover's JaCoCo-style XML stays a recorded descope).
The gate's composition criterion pins functions_total 34 /
functions_measured 3 / exactly ONE OC007 with CRAP(sum26) == comp ==
35 at 100 percent coverage -- "fully covered is not invisible".

### 8. Supply tier (the honest delineation)

The Swift pack has NO canonical dependency manifest reader in v1 --
OC009 and OC010 are enabled pack data but nothing is ever discovered
for them. `Package.resolved` IS a version-pinned artifact, but no OSV
Swift ecosystem exists and none is invented, so a v1 pin reader would
have no consumer: the SWIFT pack contributes ZERO manifest rows on any
scan; on a Swift-only scan the supply block is ABSENT (on a mixed scan
the block may carry the OTHER packs' tiers while the Swift contribution
stays zero). `Package.swift` is not a supply manifest (a target table
is not a version pin). The corpus gate pins the absence (supply block
ABSENT, manifests key absent, no OSV snapshot loaded).

### 9. Honesty invariants

- Nothing resolves outside the manifest-declared target facts; the
  directory tree is probed only through the DECLARED paths.
- No OSV Swift ecosystem exists and none is invented.
- The import-alias and submodule-refine boundaries are recorded, never
  guessed.
- The directive-condition exclusion is a fixed builtin set; any other
  top-level call IS code.
- As-written analysis: no build-system evaluation beyond the manifest's
  declared target table; swift-tools-version gating is not evaluated.
- Metaprogramming (macros, property wrappers, result builders,
  `#if` evaluation) is a parse fact only as far as the grammar sees it
  -- the Rust macro boundary, accepted in the over-report direction
  for OC006.
- The pinned grammar's measured corpus boundary: 28 of 146 analyzed
  files carry parse errors on swift-argument-parser 1.5.0 (27 the
  attribute-empty-parens shape `@Flag()` / `@Argument()` / `@Option()`
  / `@OptionGroup()` -- the pinned grammar's property-wrapper attribute
  with an empty argument list, its largest gap on this corpus; 1
  directive-adjacent). No canonical Swift corpus parses 100 percent
  clean as-written with this pin; the criterion is the PIN.

## Scala language contract (wave-3 continues, adopted SPRINT-24 in one session; probed against the pinned grammar before any adoption code)

The 15th language, the TENTH full-scope pack. The probe
(`scripts/probe_scala.c`, 10 live-parse cases) caught one contract
mistake before any adoption code existed: `typealias` is NOT Scala
syntax (`type X = ...` is the alias spelling) -- the probe's ERROR node
on the wrong-keyword source is the evidence the probe-first discipline
worked.

### 1. Grammar

Pinned `tree-sitter/tree-sitter-scala` tag `v0.23.4` (the MAIN
tree-sitter org's grammar), vendored by `scripts/vendor.sh` (git tag,
codeload tarball of the SAME tag, then the sha256-verified npm binding
package `tree-sitter-scala@0.23.4` as the final fallback). The npm
twin is byte-identical to the git tag for
parser.c/scanner.c/node-types.json/grammar.json/LICENSE (verified at
adoption). THE 0.24.0 PIN WAS REJECTED: the npm 0.24.0 package was
regenerated from a different commit than the v0.24.0 git tag (20285 vs
19568 parser states, different symbol sets) -- the dual-source
discipline requires byte identity, so the pin stays at v0.23.4 where
the two sources agree. LANGUAGE_VERSION 14, accepted by the vendored
runtime v0.27.2 (ABI window 13..15). The external scanner
(src/scanner.c: the significant-indentation OUTDENT tokens for the
fewer-braces shapes, string-interpolation delimiters) is picked up by
oc_add_grammar automatically. SCANNER AUDITED CLEAN at adoption
against the Swift lessons: `ts_calloc(1, sizeof(Scanner))` (not
calloc(0)), NO bit shifts anywhere, serialize bounded by the
`(indents.size + 3) * sizeof(int16_t)` guard, deserialize bounded by
`size < length` -- no sanitizer carve-out needed.

### 2. Functions & spans

`function_definition` is by construction a `def` WITH a body (the
`body` field: a `block`, an expression, or the Scala 3
`indented_block` -- the query captures the body field with a
field-qualified wildcard). Free functions, methods, extension and
given-body methods, nested and generic defs are all symbols. The
bodyless `function_declaration` (trait/abstract members) is NOT a
symbol -- the C prototype discipline; Kotlin took the same boundary
(no bodyless-symbol query). Lambdas (lambda_expression) are anonymous
constructs: NOT symbols, decisions count inward. The span is the whole
declaration extent. Enum cases (simple_enum_case) and
given/extension CONTAINERS are never symbols; an extension's member
defs are the symbols (the Swift extension precedent).

### 3. Parameters & signatures

The `parameter` node carries name/type/default_value FIELDS (unlike
Kotlin's field-less shape -- the probe pinned the difference);
class_parameter shares the fields, so one extractor serves both. The
variadic shape (`items: String*`) parses the type as a
repeated_parameter_type wrapper and collapses to the `...` marker (the
Kotlin vararg discipline); implicit/using modifiers are anonymous
tokens before the parameter and are invisible to the extractor (the
parameter is still named).

### 4. Complexity (decision table)

if_expression (else-if is a NESTED if_expression, the Java discipline;
Scala's `if` is statement AND ternary in ONE kind), case_clause (EVERY
match/catch arm counts including the wildcard arm -- the Rust
match_arm precedent; the catch's case_block arms ARE the catch
decision, so there is NO separate catch kind -- counting both would
double-count every caught arm), for_expression, while_expression,
do_while_expression, and infix_expression -- which counts ONLY for the
boolean operators `&&` / `||` (the operator is a NAMED
operator_identifier child, so the shared ops mechanism -- which counts
ANONYMOUS tokens inside ONE container type -- cannot express it; the
Java switch-group family-special-case pattern applies: the C side
gates the kind on the operator field's text). `match` itself is not a
kind (its arms are); Scala 3 indented match arms still parse as
case_clause nodes (the same kind); enum cases are not decisions.
Verified by the hand-counted fixtures: rollup comp 14, sum26 comp 38.

### 5. Imports & resolver v1-scala (the declared-fact resolver, SHARED with v1-kotlin)

Scala's package model is DECOUPLED from the file system exactly like
Kotlin's (no package==directory rule exists, none is invented), so the
resolver is the v1-kotlin implementation SHARED: every specifier
resolves against the recorded package clauses (the Go package_clause
hint channel; the FIRST package_clause, the chained-package boundary)
and top-level declaration names (the C# namespace-hint channel, fed by
the exports loop): `a.b.*` fans out over every file whose declared
package is `a.b` (the Java asterisk-marker discipline), `a.b.C`
requires the declared symbol (the union fans out, set semantics,
lexicographic order), `a.b.C.f` matches BOTH declared readings. The
import channel (the whole import_declaration) expands in C, one record
per leaf (the Rust use-tree / PHP use-declaration precedent): BOTH the
Scala 2 `_` and the Scala 3 `*` wildcards parse to the SAME
namespace_wildcard node and normalize to the `.*` specifier; a
selector group `{A, B}` yields one record per selector binding the
selector name; a rename (`as` AND the `=>` arrow form) binds the
ORIGINAL name (the alias is local surface detail, never a narrowing
predicate); a plain path binds the LAST segment for one/two-segment
paths and keeps NULL for three or more -- the AMBIGUOUS MEMBER shape
(`a.b.C.f` binds `f` would UNDER-mark the object `C` the member
reading actually uses, so the claim stays a whole-file claim, the C++
include discipline; the resolver still matches both readings). The
anonymous `.` separators carry the `path` field in the pinned grammar
-- a probe-caught quirk: the join reads only the NAMED path children.
Without hints every specifier stays unresolved (the honest
degradation). The imports relation is a SET.

### 6. Module surface & the entry fact

The named top-level declarations surface: function_definition, class /
object / trait / enum definitions (case/sealed/abstract are anonymous
modifier tokens on the SAME class_definition node), type_definition
(`type X = ...`), and top-level val/var definitions (the pattern
field's simple `identifier` shape; a tuple pattern records NOTHING --
the honest simple-name shape, never guessed). The filter is `private`
(package-scoped, never surface v1 -- the C static rule's Kotlin
spelling; the package-scope nuance is the recorded boundary);
`protected` stays surface (package/superclass-visible, the Kotlin
treatment of `internal`). given/extension definitions are NOT surface
(unnamed by construction or container nodes). Enum cases and class
members are not module surface (the type-nesting precedent). Every
export is ALSO a top-level-symbol parse hint (the C# namespace-hint
channel). THE ENTRY FACT is the DUAL main parse fact: a
function_definition named `main` carrying a parameters list inside an
object_definition (the JVM shape; the object need not be top-level --
the honest over-claim direction, the C #ifdef note, OC006 severity
INFO) or an @main-annotated function_definition (the Scala 3
spelling).

### 7. Coverage (OC007)

No Scala-native coverage reader in v1: sbt-jacoco emits
JaCoCo-compatible XML and the Cobertura/JaCoCo reader is the Java
pack's -- `--coverage` composes it through the existing reader (the
Kotlin pack's honesty verbatim: nothing is invented for a format the
reader does not take). The hand-written corpus pins the composition:
3 of 17 functions measured, sum26 CRAP=1482 at 0 percent coverage
(exactly one OC007), the fully covered rollup silent.

### 8. Supply tier (the honest delineation)

The Scala pack has NO canonical dependency manifest reader in v1:
build.sbt is EXECUTABLE DSL code (a `libraryDependencies++` call is
not a version-pinned artifact and is not read), no sbt lockfile is
universally present, and no OSV Scala ecosystem exists and none is
invented. OC009/OC010 are enabled pack data that never discover
anything Scala-specific: the SCALA pack contributes zero manifest rows
on any scan; on a Scala-only scan the supply block is ABSENT (on a
mixed scan the block may carry the OTHER packs' tiers -- the corpus
gate reports the RUBY Gemfile tier found in scala/scala3's docs/_spec/
-- while the Scala contribution stays zero).

### 9. Honesty invariants

- Nothing resolves outside the declared facts (package clauses +
  top-level names); no directory probing, no build-file reading.
- No OSV Scala ecosystem exists and none is invented.
- The chained-package boundary (the OUTER clause recorded), the
  3+-segment NULL-bind boundary, and the tuple-pattern export boundary
  are recorded, never guessed.
- The boolean-only infix gate is a fixed C-side family special case;
  every other infix operator is not a decision.
- As-written analysis: no build-system evaluation; sbt/Maven/Gradle
  variants are reported as the single parsed configuration.
- Metaprogramming (macros, implicits/givens resolution, type classes,
  compiler plugins) is a parse fact only as far as the grammar sees it
  -- the Rust macro boundary, accepted in the over-report direction
  for OC006.
- The pinned grammar's measured corpus boundary: 1489 of 14872
  analyzed files carry parse errors on scala/scala3 3.3.1 (10.0
  percent; 7232 error nodes; classified at pin time: 277 the
  using/given instance shapes, 116 the macro quote/splice syntax, 99
  the fewer-braces indented constructs, 70 the .nn postfix, 64 the
  match/union type operators, 43 the repeated argument type, 32 the
  inline defs, 28 the given imports, 760 residual). No canonical Scala
  corpus parses 100 percent clean as-written with this pin; the
  criterion is the PIN.
