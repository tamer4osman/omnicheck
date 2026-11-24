# Architecture (Sprint 0-24)

## Pipeline

```
            +------------------------------+
  files --> | discovery (pack-agnostic)    |
            +--------------+---------------+
                           v
            +------------------------------+     packs/*.json (declarative,
            | parser backend (tree-sitter) |<--- one per language: 15 packs /
            +--------------+---------------+     14 vendored grammar trees
                           v
            +------------------------------+
            | unified graph + comment docs |
            +--------------+---------------+
                           v
            +------------------------------+
            | analysis passes (OC001, ...) |
            +--------------+---------------+
                           v
            +------------------------------+     OmniDecide sidecar (opt-in,
            | triage (rank-only, fail-open)|<--- OC_OMNIDECIDE_BIN, JSONL
            +--------------+---------------+
                           v
            deterministic JSON report (stable fingerprints)
```

## Modules

| Module | File(s) | Responsibility |
| --- | --- | --- |
| arena | `arena.c` | bump allocator; all long-lived parse products live here |
| str | `str.c` | string views (FNV-1a) + growable buffers for transient data |
| json | `json.c` | writer (fixed field order) + strict parser (arena DOM) + JSONC-lenient variant (tsconfig) |
| fingerprint | `fingerprint.c` | stable finding identity |
| finding | `finding.c` | finding model, sink, deterministic sort |
| graph | `graph.c` | nodes (file/function/class/variable), edges, per-file docs; function nodes carry parse-time `complexity` |
| imports | `imports.c` | raw specifier records -> `OC_EDGE_IMPORTS`; resolver v2/v3 for the JS family plus the per-language v1 family (v1-go/java/csharp/rust/ruby/php/c/cpp/kotlin/swift/scala -- per-language contracts in docs/LANGUAGES.md) |
| pathutil | `pathutil.c` | lexical path join/normalize/dirname shared by the resolver stack |
| tsconfig | `tsconfig.c` | tsconfig.json/jsconfig.json paths+baseUrl alias resolution (resolver v2) |
| surface | `surface.c` | module surface: exports, import bindings, usage marking, entry-point classification (OC005/OC006) |
| coverage | `coverage.c` | coverage ingestion (Istanbul, V8, coverage.py JSON, `go test -coverprofile` text; JaCoCo XML with the Java pack, Coverlet Cobertura XML with the C# pack, LCOV with Rust, SimpleCov JSON with Ruby, Clover XML with PHP, gcovr Cobertura XML with C/C++, Cobertura XML with Kotlin/Swift/Scala) + per-span composition for OC007 / CRAP-ms |
| dup | `dup.c` | duplication index for OC008: normalized-line seed windows, text-verified lockstep extension, exact-text grouping (WBS 2.4) |
| supply | `supply.c` | supply-chain tier (WBS 4.2): manifest parsing, vendored OSV snapshot matching, deterministic secret patterns (OC009-OC011) |
| mutate | `mutate.c` | mutation engine (WBS 4.5): tree-sitter operator sets, serial runner, content-hash cache, survival report (WBS 4.5) |
| compat | `compat.c` | breaking-change gate (WBS 4.6): public-surface diff of two analysis roots |
| pack | `pack.c` | `pack.json` loader + validation, extension matching; manifest schema `omnicheck.pack/v0` (id, language, extensions, entry_points, enabled_passes) |
| discover | `discover.c` | recursive, ignore-aware, sorted file discovery |
| parser | `parser.c`, `parser_ts.c` | backend seam; tree-sitter implementation |
| pass | `pass.c`, `src/passes/` | registry + pure analysis passes |
| triage | `triage.c` | sidecar client with guardrails |
| cli | `src/cli/main.c` | check / graph / packs / lsp / mcp / compat / mutate commands |

## Rule registry (11 analyses)

The shipped rule set, exactly as registered by `src/passes/*.c`
(`scripts/audit_docs.py` derives this table from the code and fails the
M5 docs gate if any rule, CLI command or schema is undocumented):

| Rule | Name | Default severity | Fires when |
| --- | --- | --- | --- |
| OC001 | long-function | warn | function definition spans more than `OC001.max_lines` lines (default 60) |
| OC002 | todo-comment | info | comment carries an open TODO / FIXME / HACK marker |
| OC003 | import-cycle | warn | strongly connected component of import edges over more than one file |
| OC004 | complex-function | warn | cyclomatic complexity above `OC004.max_complexity` (default 15); doubles as the no-coverage CRAP proxy |
| OC005 | unused-export | warn | export never referenced by any analyzed import binding (imported files only; wildcard/require/dynamic use fails open) |
| OC006 | unreferenced-module | info | analyzed file with no inbound import edges claimed by no entry-point rule |
| OC007 | crap-hotspot | warn | CRAP(m) = comp^2 * (1 - cov/100) + comp above `OC007.max_crap` (default 30), coverage composed |
| OC008 | duplicate-code | warn | normalized line block (`OC008.min_block`, default 10) duplicated verbatim in 2+ locations |
| OC009 | dep-vuln | error | pinned dependency matches the vendored OSV snapshot (unpinned requirements: warn) |
| OC010 | licence | warn | manifest declares no licence |
| OC011 | secret-pattern | error | credential-shaped pattern in source (generic secret-name assignment: warn) |

Every rule is intent-blind and deterministic: structurally true
statements only, severities fixed by the registry, triage (rank-only,
fail-open) decides attention, never truth.

## Determinism contract

1. Discovery sorts and dedupes; findings sort by `(file, line, col, rule)`.
2. Reports contain no wall-clock timestamps or durations; the JSON writer
   emits fixed field order.
3. Fingerprints are FNV-1a 64-bit over length-prefixed
   `rule_id, file, anchor[, normalized_snippet]` -- line/column numbers are
   deliberately excluded so findings survive code motion. Comment-type
   findings fold in normalized marker text to stay distinct.
4. `tests/test_cli` asserts byte-identical reports across runs.

## Memory strategy (risk R2 mitigation)

Analysis structures are arena-allocated and freed in one shot (`oc_arena_destroy`).
Heap-owned growable arrays (`oc_strbuf`, `oc_finding_vec`, `oc_graph.nodes/edges`,
`oc_path_vec`, `oc_pack_set.packs`) have explicit destroy functions. The
sanitize preset runs the whole suite under ASan+UBSan with leak detection --
the harness that caught two real bugs during Sprint 0 (see `docs/SPRINT-0.md`).

## Parser backend seam

`oc_parser` hides grammars behind one function: parse a file into the graph.
Backends are selected at build time; a build without vendored grammars fails
loudly at use with an actionable message instead of silently degrading.
Adding a language: vendor grammar -> add descriptor (language fn + function,
comment and iRegistered languages: `javascript`, `typescript`, `tsx`, `python` (Sprint 1);
`go` (Sprint 9 spike; resolver v1-go, import bindings, package_clause entry
claiming and the OC003/OC005 flip-on in SPRINT-10; the `go test
-coverprofile` adapter and the `go.mod` manifest tier in SPRINT-11 -- all
eleven passes live); `java` (SPRINT-12, full scope in one sprint: grammar
v0.23.5, resolver v1-java -- single-type/static imports resolve to the
class file `a/b/C.java` verbatim, package wildcards `a.b.*` claim the
package directory with whole-package fan-out and the imports relation is a
SET --, entry points as contracted file-name approximations, the JaCoCo XML adapter
and the `pom.xml` Maven tier -- all eleven passes live); `csharp`
(SPRINT-13, full scope in one sprint: grammar v0.24.0 -- the first vendored
grammar WITH an external scanner --, resolver v1-csharp -- plain usings
resolve to the namespace directory `A/B/C` (the documented two-convention
layout ladder: SDK/VS slash mapping first, mono's dotted single-directory
form second), static usings to the type file `A/B/C.cs`, alias usings
probe in the documented order, the imports relation is a SET --, the
static-Main and top-level-statements entry facts, the Coverlet Cobertura
XML adapter and the `.csproj` NuGet tier -- all eleven passes live);
`rust` (SPRINT-15, full scope in one sprint: grammar v0.24.0 -- the
second vendored grammar WITH an external scanner --, resolver v1-rust --
two declaration-level channels: the mod-tree (`mod name;` resolves to
the module file by the stem rule) and the use-tree (`crate::`/`self::`/
`super::` leaves resolve through the self/crate/super anchors with the
item-reading pop ladder; external crate heads stay unresolved and no
manifest is ever read; the imports relation is a SET --, the `fn main`
parse fact, the LCOV adapter and the Cargo.toml/Cargo.lock supply tier
with OSV ecosystem crates.io -- all eleven passes live); `ruby`
(SPRINT-16, full scope in one sprint: grammar v0.23.1 -- the THIRD
vendored grammar WITH an external scanner --, resolver v1-ruby --
require_relative resolves against the requiring file's directory with the
analysis-root-anchored `..` probe; bare requires probe the two documented
load-root conventions under the analysis root (`<root>/p.rb`, then
`<root>/lib/p.rb`, the gemspec/bundler layout); $LOAD_PATH is never read
from any manifest; the imports relation is a SET --, the runner-collected
NAME test conventions (test_*.rb / *_test.rb / *_spec.rb, name-based
only), the SimpleCov .resultset.json adapter (positional lines, null =
not-relevant and NOT an anchor, loud malformed input) and the
Gemfile/Gemfile.lock supply tier with OSV ecosystem rubygems -- all
eleven passes live; the shared complexity walker now matches decision
kinds on NAMED nodes only -- the Ruby keyword tokens share type strings
with their constructs); `php` (SPRINT-19, Track E wave-2 completion, full
scope in one sprint: grammar v0.24.2 -- the FOURTH vendored grammar WITH
an external scanner, compiled as the MIXED php+html tree so template
text/text_interpolation nodes are inert to every analysis and one
descriptor handles both file kinds --, resolver v1-php -- use
declarations resolve through the composer.json PSR-4/PSR-0 mapping read
from the ANALYSIS ROOT (longest-prefix rule, backslash->slash,
root-anchored; PSR-4 strips the prefix, PSR-0 keeps the full name path);
a use whose leading prefix matches no composer prefix stays
recorded-and-unresolved, nothing guessed from include_path or runtime
autoloading; require/include resolve their ONE static string argument
relative to the requiring file's directory, the argument guard records
NOTHING for concatenations or interpolation; the imports relation is a
SET --, the Clover XML coverage adapter (php-code-coverage's clover.xml,
dispatched before the Cobertura reader on the shared <coverage> root) and
the composer.json/composer.lock supply tier with OSV ecosystem packagist
-- all eleven passes live; NO shebang hint for PHP (a #! line outside PHP
blocks is template text; entry points are NAME conventions only:
index.php and bin/* runner scripts); `c` (SPRINT-20, wave-2, full scope
in one session: grammar v0.23.4 -- the FIRST vendored grammar with NO
external scanner, src/ holds parser.c + grammar.json + node-types.json
only --, the descriptor resolves symbol names by descending declarator
chains (C has no name field; the parenthesized wrapper is positional,
not fielded) with the function-pointer-return shape taking the INNER
function_declarator's parameter list, prototypes are module surface but
NEVER symbols (the honest double-count rationale), resolver v1-c --
quoted includes resolve against the INCLUDING file's directory with
root-anchored lexical `..` pops, angle-bracket system includes stay
recorded-and-unresolved (the Go module-boundary precedent), a macro path
records NOTHING, the imports relation is a SET --, gcovr's native
Cobertura XML works through the existing reader with zero new code, the
supply tier is the honest EMPTY tier (no canonical C manifest exists,
nothing is fabricated, the report meta says so), the entry claim is the
main-definition PARSE FACT (no name glob can express "defines main",
entry_points is empty; the `.h` boundary note landed in SPRINT-21 as the
routing treatment) -- all eleven passes live); `cpp`
(SPRINT-21, wave-2 close-out, full scope in one session: grammar
v0.23.4 -- the C grammar inlined at generation time plus an external
scanner --, the C declarator-chain walk extended with the four cpp node
kinds (field_identifier, the qualified_identifier name field,
destructor_name and operator_name carried as written), C's decision
table plus for_range_loop and catch_clause, resolver v1-cpp SHARING the
v1-c include channel verbatim (the `using` family records nothing in
v1 -- no honest file mapping without a manifest), the toplevel climb
extended with named-namespace bodies and template_declaration wrappers
while anonymous namespaces are internal linkage (never surface), the
entry claim the free-standing main-definition PARSE FACT (a member
`main` claims nothing -- probed), gcovr Cobertura through the existing
reader, the honest EMPTY supply tier in v1 (vcpkg.json/conanfile.py are
not canonical pins), and the APPROVED .h ROUTING TREATMENT implemented:
both packs claim .h, a .h routes through the content sniff (clean cpp
parse + a C++-distinctive node -> cpp; cpp errors with a clean C parse
-> c) and otherwise the project-root default (cpp iff the discovered
set carries a non-.h cpp extension), the counts in the report meta's
header_routing block -- never silent -- all eleven passes live.
CoCo XML coverage adapter and pom.xml supply support --
the first pack with the complete OC001-OC011 set); `kotlin`
(SPRINT-22, wave-3 opens, full scope in one session: grammar fwcd
v0.3.8 -- the canonical community grammar, LANGUAGE_VERSION 14 plus an
external scanner for automatic semicolons / string interpolation /
nested comments --, symbols = function_declaration WITH a body (free,
method, extension, generic/suspend/inline; body-less abstract/interface
members are NOT symbols), property accessors with a body promoted to
symbols named Prop.get/Prop.set (the C# accounting-hole precedent; the
pinned grammar parses the accessor as a SIBLING of its
property_declaration), the decision table if_expression (else-if
nested) + when_entry (every arm counts including else -- the Rust
match_arm discipline) + for/while/do-while + catch_block +
elvis_expression + the conjunction/disjunction containers (Kotlin has
no shared binary_expression), resolver v1-kotlin: the DECLARED-FACT
resolver -- Kotlin's package model is decoupled from the file system,
so imports resolve against the RECORDED package_header clauses (the Go
package_clause hint channel) and top-level declaration names (the C#
namespace-hint channel): wildcard `a.b.*` fans out over the package's
files (the Java asterisk-marker precedent), a single `a.b.C` requires
the declared symbol (the union fans out over every file that carries
it -- set semantics, lexicographic order), the ambiguous member shape
keeps a NULL bind (whole-file claim, the C++ include discipline), and
WITHOUT hints everything stays unresolved (the honest degradation), the
entry claim the top-level main-definition PARSE FACT (source_file
anchored: a member `fun main` claims nothing -- probed), the honest
EMPTY supply tier in v1 (gradle files are not pins; the kotlin pack
contributes zero manifest rows on any scan), and no shared extension
with any other pack (no routing treatment needed) -- all eleven passes
live); `swift`
(SPRINT-23, wave-3 continues, full scope in one session: grammar
alex-pinkus v0.7.1-with-generated-files -- the CANONICAL Swift grammar
(the tree-sitter org's tree-sitter-swift is an abandoned unrelated
project whose README redirects here), LANGUAGE_VERSION 14 plus an
external scanner for block comments / raw strings / implicit
semicolons / the operator tokens --, symbols = function_declaration
WITH a body (free, method, extension, static/class, generic) +
init/deinit with a body (the C# constructor precedent, the honest
synthesized spellings) + protocol functions with a DEFAULT body (the
sibling-wrapper shape, paired by the query adjacency anchor);
body-less requirements are NOT symbols), property accessor bodies
promoted to symbols named Prop.get/Prop.set/Prop.willSet/Prop.didSet
(the C# accounting-hole precedent; the accessors nest INSIDE the
property_declaration -- a parent climb, unlike Kotlin's sibling walk;
the implicit getter counts as get; an EMPTY body matches nothing), the
decision table if_statement (else-if nested) + guard_statement +
ternary_expression + switch_entry (every arm counts including default
-- the Rust match_arm discipline) + for/while/repeat-while +
catch_block + nil_coalescing_expression + the conjunction/disjunction
containers (Swift has no shared binary_expression), resolver
v1-swift: the DECLARED-FACT resolver, the MODULE spelling -- Swift's
import specifier names a compilation TARGET no source file carries,
so imports resolve against the MANIFEST-declared facts (every analyzed
Package.swift contributes (target, path) pairs through the
target-hint channel; each file joins its module by the DECLARED path
-- an explicit `path:` as written, else the SPM documented default
Sources/<name>); whole-module and submodule claims resolve on the
FIRST segment and fan out wholesale (NULL bind, the Kotlin wildcard
discipline), kind-scoped member claims bind the LAST segment (the
over-mark direction bounded by the declared facts), the
directive-condition builtin set keeps `#if compiler(...)` version
checks out of the top-level-statement entry fact, and WITHOUT hints
everything stays unresolved (the honest degradation), the entry claim
the top-level-statement PARSE FACT (the C# 9 global-statement
precedent: a top-level `let` is a declaration and never fires it; the
corpus manifests carry real `package.targets.append` statements and
are honestly claimed), the honest EMPTY supply tier in v1
(Package.resolved is a version-pinned artifact but no OSV swift
ecosystem exists and none is invented; the swift pack contributes
zero manifest rows on any scan), and no shared extension with any
other pack (no routing treatment needed) -- all eleven passes
live); `scala`
(SPRINT-24, wave-3 continues, full scope in one session: grammar
tree-sitter/tree-sitter-scala v0.23.4 -- the MAIN org's grammar, the
pin held at 0.23.4 because the 0.24.0 npm twin was regenerated from a
different commit than its git tag (20285 vs 19568 parser states; the
npm 0.23.4 package IS byte-identical to the tag on every contract
file), LANGUAGE_VERSION 14 plus an external scanner for the
significant-indentation outdent tokens and string-interpolation
delimiters, audited clean against the Swift lessons (ts_calloc(1,...),
no bit shifts, bounded serialize/deserialize) --, symbols =
function_definition (by construction a def WITH a body; the bodyless
function_declaration of trait/abstract members is NOT a symbol, the C
prototype discipline Kotlin took), lambdas anonymous, parameters carry
name/type/default_value FIELDS with the variadic repeated_parameter_type
collapsing to the `...` marker, the decision table if_expression
(else-if nested; Scala's if is statement AND ternary in ONE kind) +
case_clause (EVERY match/catch arm -- the catch decision IS its
case_block arms, no separate catch kind) + for/while/do-while +
infix_expression C-gated to the boolean operators ONLY (the operator
is a NAMED operator_identifier -- the shared anonymous-token ops
mechanism cannot express it; the Java switch-group family-special-case
pattern applies), resolver v1-scala: the DECLARED-FACT resolver SHARED
with v1-kotlin (Scala's package model is decoupled from the file
system exactly like Kotlin's: imports resolve against the RECORDED
package_clause facts and top-level declaration names -- wildcard
fan-out, single requires the declared symbol, the ambiguous member
shape matches BOTH readings), the import channel expands selector
groups to one record per selector (the Rust use-tree precedent; BOTH
the Scala 2 `_` and the Scala 3 `*` wildcards normalize to the
asterisk marker; renames bind the ORIGINAL name), the entry claim the
DUAL main parse fact (a def named main with parameters inside an
object_definition -- the JVM shape; an @main-annotated def -- the
Scala 3 shape), the honest EMPTY supply tier in v1 (build.sbt is
executable DSL code, not a version-pinned artifact; no OSV Scala
ecosystem exists and none is invented; the scala pack contributes zero
manifest rows on any scan), and no shared extension with any other
pack (no routing treatment needed) -- all eleven passes live).
Per-language adoption contracts live in `docs/LANGUAGES.md` (recipe step 1:
the contract lands BEFORE any code, mirroring the contract style of this
document). Each parse records per-file diagnostics (BUG-1 fix: ERROR/MISSING
nodes are surfaced in the report, never silent), per-function cyclomatic
complexity (decision kinds + `&&`/`||`/`??` for the JS family;
branches/except/boolean operators/ternaries/comprehension-ifs for Python;
if/for/case clauses + `&&`/`||` for Go; if/for/for-each/while/do/switch
rules and case-labelled groups/catch/ternary + `&&`/`||` for Java, with a
descriptor-level filter excluding default-only switch groups; nested
closures count inward in every language), and raw import specifiers.

## Function span contract (OBS-2 fix, Sprint 2)

Every function node carries ONE canonical span: the DEFINITION EXTENT --
first line of the defining construct through its last line. For 1TBS
JavaScript this is the declaration through the closing brace; for Python
the `def` line through the last body line (docstring included). OC001,
OC004, OC007, coverage composition and the graph dump all consume
`def_start/def_end`; there is no second span anywhere. (Body-block spans
existed until Sprint 1 and caused the cross-language threshold asymmetry
recorded as OBS-2 in `docs/SPRINT-0.md`; fixed in Sprint 2.)

## Import resolution contract (v2, `imports.c` + `tsconfig.c`)

1. The parser records specifiers; resolution runs AFTER every file is
   parsed, so an edge always means "the target is part of this analysis".
2. JS/TS/TSX: relative specifiers (`./x`, `../x`) resolve as before,
   with the same fixed probing order (exact, extensions, `index.*`,
   TS `.js`->`.ts`). Bare specifiers resolve ONLY through the nearest
   tsconfig.json/jsconfig.json (`compilerOptions.paths` exact and
   single-`*` patterns, longest-pattern-first; `baseUrl` with the TS
   legacy default of the config dir when `paths` exists without it).
   The tsconfig upward search is BOUNDED at the analysis root (the CLI
   root arguments): a config outside the analysis is invisible, by
   contract. Bare specifiers without a matching alias still skip,
   never guessed. package.json exports / node_modules resolution stays
   out of scope (WBS 4.x).
3. Python: explicit relative imports (`from .x import y`) resolve as
   before (PEP 328 levels). Absolute imports resolve through resolver
   v3 (Sprint 4): the importing file's package root is its nearest
   ancestor directory WITHOUT an `__init__.py` among the analyzed
   files -- for packaged code that is the repository root, for loose
   scripts the scripts directory itself. Probing starts there
   (`<root>/<spec>.py`, then `<root>/<spec>/__init__.py`), then falls
   back to each analysis root in command-line order (src-layout /
   monorepo bases). Specifiers that match no intra-repo file (stdlib,
   site-packages) stay unresolved -- never guessed -- so the SPRINT-2
   OC006 over-reporting shrinks to names that genuinely have no
   intra-repo target. `from pkg import name` resolves the specifier
   `pkg` to the package `__init__.py` (the import RECORD carries
   specifiers only); `import pkg.sub` resolves the submodule.
4. Duplicate specifiers may produce duplicate edges; passes tolerate
   that (OC003 is SCC-based; in-degree is presence-based). Self-imports
   produce no edge. All resolution goes through one shared context
   (`oc_resolve_ctx`, sorted path index), so edges and surface usage
   marking can never disagree.
5. Go (Sprint 9 spike, resolver v1-go Sprint 10): the parser records one
   raw specifier per `import_spec` (at ANY depth under its declaration --
   Sprint 10 also fixed the latent Sprint-9 bug where grouped
   `import ( ... )` blocks recorded nothing) plus one import binding
   (the alias when aliased, NULL otherwise). A dotted path resolves
   through resolver v1-go: the package directory it names under an
   analysis root (roots probed in command-line order), with the
   DIRECT-CHILDREN rule -- subdirectories are separate packages. The
   whole package is the target: edges fan out to every direct-child
   `.go` file, and surface marking covers the whole package (fail-open
   by contract). Guards: relative/absolute/`..`/empty-segment forms and
   anything outside the analyzed set (stdlib, remote modules) stay
   unresolved; go.mod is NEVER read and module-prefix stripping stays
   out of scope, so a module-prefix specifier stays unresolved even
   when its target directory exists (pinned by the acceptance gate's
   decoy manifest). Single-target view (`oc_resolve_specifier`):
   the package's lexicographically-first discovered file.

## Module surface contract (Sprint 2, `surface.c`)

- The parser records export candidates and import BINDINGS (names)
  while parsing. JS: `export <declaration>`, `export {a, b as c}`,
  `export default` (pseudo-name "default"), re-exports; import names
  via the import clause. Python: top-level defs/classes (underscore-
  prefixed excluded as private); `__all__` IS the public surface when
  present (PEP 8) and demotes everything else. Go (Sprint 9/10):
  package-level declarations with an exported (upper-case first rune,
  ASCII) name; methods are receiver-scoped and NOT package surface;
  import bindings carry the alias when aliased (NULL otherwise) and a
  package import marks the WHOLE package used -- fail-open by contract,
  so at whole-package resolution granularity OC005's verdict set for Go
  is empty by construction (asserted, not assumed;
  `docs/LANGUAGES.md` §Go.5/§Go.6).
- `oc_surface_build` resolves bindings through the SAME resolver as
  edges and marks exports used. FAIL-OPEN policy: wildcard imports,
  side-effect imports, `require()` / dynamic `import()`, `export *
  from`, and default imports mark whole files used rather than guessed.
- OC005 unused-export (warn): export with no referencing binding, only
  in files that are themselves imported. OC006 unreferenced-module
  (info): file with zero inbound import edges claimed by no entry rule
  (pack `entry_points` globs -- `**`/`*` glob syntax --, shebang first
  line, the parsed `package main` clause (Go, Sprint 10), narrow test
  conventions -- `*_test.go` joined in Sprint 10). Both are structurally
  true, intent-blind: triage's job, never auto-verdicts.

## Coverage composition contract (CRAP-ms, Sprint 2, `coverage.c`)

- Inputs: Istanbul `coverage-final.json` and coverage.py `coverage.json`
  (format sniffed per file; several files may be composed together --
  the multi-source increment). Line anchors: Istanbul statementMap
  start lines covered when `s[id] > 0`; coverage.py union of
  executed/missing lines, covered = executed.
- A coverage path maps onto an analyzed path by exact string first,
  then by UNIQUE "/"-boundary suffix; ambiguous suffixes match nothing
  (honest, never guessed).
- Per-function coverage = covered anchors inside the definition span /
  anchors inside it (integer floor). CRAP(m) = comp^2 * (1 - cov/100) +
  comp; OC007 warns above `OC007.max_crap` (default 30). Functions with
  no coverage entry or zero anchors are unmeasured and NEVER produce
  findings; OC004 remains the no-coverage proxy. Line-anchor
  composition is the cross-language-uniform approximation of Istanbul's
  per-function counters (recorded descope).

## Duplication contract (Sprint 3, `dup.c`, OC008)

The index is built ONCE per run from the analyzed file set and attached
to the pass context like surface/coverage; the per-file pass emits one
finding per duplicated location of the current file.

- Normalization: CR stripped, leading/trailing whitespace trimmed,
  internal whitespace runs collapsed to one space. Blank lines stay in
  the line array (spans count physical lines) but normalize to "".
- Seeds: windows of `OC008.min_block` (default 10, min 2) consecutive
  lines, bucketed by a 64-bit rolling hash. The hash is ONLY a bucketing
  device -- every candidate is verified by direct text comparison, so a
  hash collision can never manufacture a duplicate.
- Extension: verified seeds extend in lockstep (the line t beyond every
  member's span must textually match across ALL members, forward and
  backward). Seeds inside one maximal block converge to the same spans;
  identical (file, start, end) locations collapse.
- Grouping: a dup group is a run of >= 2 locations with equal span hash,
  equal span length AND re-verified equal span text. One finding per
  location (severity warn); the message lists up to 8 locations sorted
  by (path, start), "+N more" beyond that.
- Honesty counters: `meta.dup.groups` / `groups_truncated`
  (cap `OC008.max_groups`, default 256) / `files_skipped_oversize`
  (cap `OC008.max_file_bytes`, default 1,000,000). Nothing is silently
  dropped.
- Fingerprints are FNV-1a 64 over (rule, file, anchor, normalized
  snippet); location is deliberately EXCLUDED so a finding survives code
  motion. Passes that can emit several findings per anchor fold a
  distinguishing snippet (`dup:<text-hash>:<start>`, OC001 span size,
  OC011 keyword+literal); genuinely byte-identical siblings (same rule,
  file, anchor and snippet, e.g. the same statement twice in one file)
  therefore share one fingerprint BY DESIGN, and triage annotates every
  member of such a group identically (see `src/triage.c`).
- Findings are intent-blind by design (same discipline as OC003/OC005/
  OC006): structurally true duplication, severity warn, triage decides.

## OmniDecide integration (three guardrails, four failure modes)

Only findings text (prose) leaves the process, one JSON line in, one out.
Triage may only annotate `triage_rank` / `triage_confidence`; report order
and severities are untouched. Provider missing, broken (`/bin/false`), slow
(3 s `SIGALRM`), or malformed output all degrade to "unranked", never to a
failed run. `tests/test_triage` covers all four failure modes plus the
rank-permutation invariant.

**Vendored kernel (decision L8 of `OMNIDECIDE_LEVERAGE.md`, 2026-11-20).**
The kernel tree ships IN-REPO, unmodified, under `third_party/omnidecide/`
(Apache-2.0; pinned by zip sha256 + per-file `VENDOR.sha256` manifest in its
`PROVENANCE.md`; `scripts/vendor.sh` verifies the manifest). The build
compiles it as the upstream CMake subdirectory target `omnidecide` and
links exactly two first-party binaries against it: the protocol-v0
provider `oc-omnidecide-provider` (`tools/omnidecide/oc_odk_provider.c`),
which passes a fixed English-prose kernel config and never touches the
adaptive brain -- the statelessness precondition for byte-determinism
with triage enabled -- and the post-hoc auditor
`oc-omnidecide-explain` (`tools/omnidecide/oc_odk_explain.c`), which
may exit 2 on mismatch (auditors fail closed; pipeline components fail
open) and is never used by a scan. The provider optionally writes an
audit trail (`OC_OMNIDECIDE_ARTIFACTS_DIR`, default OFF: the byte-exact
request pin plus the full uncapped ranked list with explain components)
after its stdout response is already emitted; the artifacts are
documentation, never an input to the pipeline. The process boundary
RETAINED at runtime: the analyzer still only
talks to a spawned provider via `$OC_OMNIDECIDE_BIN` (opt-in `--triage`
plus env), so provider failures stay boring (budget, process-group kill,
fail-open). In every build cell, `test_triage_real` drives the real
provider through both seams (direct protocol + `oc check` e2e: rank-only,
double opt-in, honest `ranked_count`, byte-identical reruns),
`test_triage_audit` proves the audit channel (auditor contract,
artifacts byte-identity, replay re-derivation), and the
kernel's own self-checks run unmodified as `odk_smoke_test` (373) +
`odk_spine_test` (210) from the vendored root -- 583 checks green in-repo,
clean under ASan+UBSan+LSan.

## Shared scan pipeline + report contract (Sprint 4, `scan.c` + `report.c`)

- `oc_scan_run` (WBS seam for `oc check`, `oc lsp`, `oc mcp`) is THE
  analysis pipeline: packs -> discover -> parse -> resolve -> coverage
  -> surface -> duplication -> passes -> (triage) -> sort. Moved
  verbatim from `cmd_check` in Sprint 4; `oc check` output stayed
  byte-identical (asserted by the committed checksums and test_cli).
  The result owns the graph (document symbols), the sorted findings,
  the named parse-error records, and every meta counter; it is always
  safe to destroy, on success and failure alike. Parse failures of
  analyzed files are records, never hard errors -- exit-code policy
  stays with the caller (`oc check` dominates with exit 2).
- `oc_report_write_json` / `oc_report_write_human` are THE report
  renderers: one report contract, three surfaces. The JSON field order
  is the committed `omnicheck.report/v0` schema and never reordered.

## LSP server contract (Sprint 4, `lsp.c`, WBS 3.3)

`oc lsp` speaks LSP 3.17 over stdio (JSON-RPC 2.0, Content-Length
framing). Methods: `initialize` (capabilities: full text sync +
documentSymbolProvider; rootUri/workspaceFolders selects the scan
root), `initialized`, `shutdown`/`exit` (spec exit codes: 0 after
shutdown, 1 without), `textDocument/didOpen|didChange|didSave` (each
triggers a fresh scan; `textDocument/publishDiagnostics` carries the
document's findings in canonical order plus NAMED parse-error
diagnostics at severity 1, code `parse-error` -- a broken file can
never look clean in an editor either), and `textDocument/documentSymbol`
(flat `DocumentSymbol[]` from the graph: Function 12 / Class 5 /
Variable 13, definition-extent ranges). Honest scopes: diagnostics
reflect the ON-DISK state (buffer text is not analyzed -- descope);
columns are byte offsets, not UTF-16 code units (descope); fresh scan
per event (no incremental cache -- descope). URIs are percent-encoded
`file://` paths, decoded exactly; non-file URIs are rejected, not
guessed. Unknown requests get -32601; unknown notifications are
ignored; a failed scan is logged and fail-open (session survives).

## MCP server contract (Sprint 4, `mcp.c`, WBS 3.4)

`oc mcp` speaks the MCP stdio transport (JSON-RPC 2.0,
newline-delimited). `initialize` echoes the client's protocolVersion
(default `2024-11-05`); `ping` answers empty; `tools/list` returns
THREE tools in fixed order with fixed JSON schemas:
`omnicheck_scan` (the full deterministic report, byte-identical to
`oc check` JSON for the same inputs), `omnicheck_findings` (filtered,
capped findings query: rule / file_contains / severity / limit over
the canonically sorted findings), `omnicheck_rules` (the built-in rule
registry). `tools/call` failures follow MCP: unknown tool = protocol
error -32602; scan execution failure = result with `isError:true`
(fail-open for the agent). The session ends when stdin closes (exit
0) -- the client owns the lifecycle. Per-call `config` overlays the
session config; strings only (non-string values are a -32602).

## Supply-chain tier (Sprint 5, `supply.c`, WBS 4.2)

Three rules registered like every other pass (registry 8 -> 11):

- **OC009 dep-vuln** runs on MANIFESTS found in the analyzed tree
  (`package.json`, `requirements.txt`, `pyproject.toml`), gated by the
  ecosystem pack's enabled list (npm -> ts-js, PyPI -> python). Matching
  is against a VENDORED OSV snapshot passed with `--osv-snapshot`
  (`omnicheck.osv_snapshot/v0`; the `snapshot_date` is echoed into the
  report meta so external-data age is visible in output). Only pinned
  specifiers are matched -- package.json exact pins (digits/dots, no
  `^ ~ * x`), requirements.txt `==`, pyproject PEP 508 `==`; ranges are
  never guessed and unpinned requirements are reported as warn. Version
  compare: dotted numeric segments, missing = 0, a prerelease suffix
  sorts before the plain release.
- **OC010 licence** flags manifests declaring no licence. Manifest
  findings anchor at line 1 with the dependency/symbol named in the
  message (the JSON parser does not track line numbers; requirements.txt
  carries real ones). The pyproject reader is a bounded line scanner,
  not a TOML parser.
- **OC011 secret-pattern** runs per analyzed file with a fixed pattern
  list (AWS AKIA + exactly 16 charset chars with a boundary check, PEM
  private key header, GitHub ghp_ + 36, Slack xox tokens -> error;
  generic secret-name assignment with a long quoted literal -> warn).
  First match per pattern per line; no regex, no entropy guesses.

The scan pipeline keeps each analyzed file's raw bytes alive until after
the pass loop (OC011 reads `ctx.source`), then frees them.

## CRAP leaderboard + V8 adapter (Sprint 5, WBS 4.3)

The coverage loader sniffs a third format: V8 (NODE_V8_COVERAGE JSON).
Byte-offset ranges become line anchors by reading the url file once: a
line is an ANCHOR when any function range contains its start byte and
COVERED when the DEEPEST containing range has count > 0 (an uncovered
inner block shadows a covered enclosing function). With coverage
composed, `oc check` emits a `crap_leaderboard` block: top 10 MEASURED
functions by CRAP desc (ties path asc, line asc); the human report
gains a matching table. Conditional on coverage -- runs without
`--coverage` are byte-identical to earlier releases.

## Mutation engine (Sprint 5, `mutate.c`, WBS 4.5)

`oc mutate [paths] --test-cmd CMD` applies the plan's seven operator
sets compiled as tree-sitter queries against the pinned grammars
(relational swap, logical swap, condition negation, constant tweak,
statement deletion -- docstrings excluded as equivalent mutants,
return-value change, argument swap). Determinism: mutants are collected
in tree order and executed serially; the cache is keyed by content hash
+ exact edit; a pre-flight run requires the suite to PASS unmutated
(a failing clean suite is a hard error, never a fake kill ratio);
mutated files are spliced in place and restored unconditionally;
timeouts kill the process group. Report: `omnicheck.mutation_report/v0`
with killed/survived/timeout counters and wall seconds; exit 1 when
survivors exist. The `--cache FILE` verdict store is
`omnicheck.mutation_cache/v0`, keyed by content hash + exact edit -- a
cache hit reproduces the verdict a fresh run would have produced. Coverage-guided selection with --coverage: only
mutants inside functions with composed coverage > 0 run.

## Breaking-change gate (Sprint 5, `compat.c`, WBS 4.6; signature diffs Sprint 17)

`oc compat --baseline DIR --current DIR` diffs the PUBLIC SURFACE of
the two trees (exports + function nodes with parameter signatures,
paths relative to each root). BREAKING (exit 1): removed named export,
removed public module (exports gone with the file), function gone from
a still-existing public module, and signature changes per the binding
policy table (docs/SPRINT-17.md section 3): `changed-param-count`,
`param-reordered`, `changed-param-type` (both sides declared only),
`param-renamed` (breaking for named-arg languages: Python, C#).
Informational (never exit-affecting): additions, private-module
removal, `added-param-default` (widening with defaults), and the
Java/C# arity-set deltas (`removed-overload`, `added-overload` --
an arity fully removed is BREAKING, a partially removed set is not).
Types compare as raw source-order text; a parameter undeclared on
either side is never compared. Report
`omnicheck.compat_report/v1` (v0 retired; change objects may carry a
one-line `detail`). The graph carries the ordered signature on
function nodes (`oc_param`: name, raw type text, default flag) since
Sprint 17; the `oc graph` JSON dump is unchanged.

## CI gate templates (Sprint 4, `templates/`, WBS 3.6)

`templates/github-action/action.yml` is a composite action (build oc
from the vendored tree, then `oc check` with `--fail-on`); exit 1 at
the threshold, exit 2 on any parse failure -- a broken file can never
yield a silent green run in CI either. `templates/ci/omnicheck-gate.yml`
is the workflow template; `templates/ci/README.md` documents the two
wiring options (engine checkout vs prebuilt `oc-bin`) and every input.
