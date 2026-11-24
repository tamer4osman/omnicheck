# SPRINT-17 -- B3 build: compat signature diffs (typed-where-declared)

Status: build sprint, 2026-11-22. Scope is BOUND by the decision record
in `docs/B3_COMPAT_SIGNATURE_DIFFS.md` section 7 (Option B, typed-where-
declared) and its section 4 policy table. Contract first (this file),
then code, fixtures, tests, gate, matrix, docs -- the standing sprint
shape. Nothing here touches analyzer verdict logic of OC001-OC011 or
the `oc check` report; the 10k determinism audit is out of scope by
construction (compat is a separate command, not in the check pipeline).

## 1. Data model (graph)

`oc_node` (function nodes only) gains an ordered parameter signature:

    typedef struct oc_param {
        const char *name;    /* arena-owned; "..." for splat/rest/variadic/
                             * unnamed/destructuring markers */
        const char *type;    /* arena-owned raw source text, NULL when the
                             * language or annotation site declares none */
        int has_default;     /* 1 when the parameter carries a default value */
    } oc_param;

- `params` / `n_params` on `oc_node`, arena-owned, set via
  `oc_graph_set_params()` right after symbol creation.
- The `oc graph` JSON dump is UNCHANGED this sprint (its byte shape is
  test-locked); signatures live on the node struct for the compat gate.
- Extraction is tree-walking over the matched function's parameter list
  node -- no changes to the per-language query strings.

## 2. Per-family extraction rules (8 grammars, 9 packs)

Param-list node types per family: JS family `formal_parameters`
(javascript, typescript, tsx grammars), Python `parameters`, Go
`parameter_list`, Java `formal_parameters`, C# `parameter_list`, Rust
`parameters`, Ruby `method_parameters` (+ `parenthesized_parameters`).
First pre-order match inside the function's defining construct wins.
Anonymous children (commas, parens) are skipped; named children are
dispatched per family:

- JS family: `identifier` -> (name, NULL, no default); TS
  `required_parameter` -> (pattern name, `type` field text or NULL,
  default only when an `assignment_pattern` child exists);
  `optional_parameter` -> has_default 1; `assignment_pattern` -> name
  from `left`, has_default 1; `rest_pattern` -> marker. Covers the
  javascript and typescript grammar shapes with one rule set.
- Python: `identifier`; `default_parameter`/`typed_default_parameter`
  -> has_default 1; `typed_parameter` -> type = the non-identifier
  child; `list_splat_pattern`/`dictionary_splat_pattern` -> marker;
  the `*` / `**` separator nodes are skipped.
- Go: `parameter_declaration` -> name field (or marker when unnamed),
  type field text; `variadic_parameter_declaration` -> name or marker,
  element type text.
- Java: `formal_parameter` -> type field + `variable_declarator`
  identifier; `spread_parameter` -> marker; `receiver_parameter`
  skipped (implicit this, not caller-visible).
- C#: `parameter` -> type + name fields, has_default from the
  `default` field; `parameter_array` -> marker.
- Rust: `parameter` -> pattern text when the pattern node is an
  `identifier` (else marker), `type` field text; `self_parameter` ->
  (self, NULL); other named children -> marker.
- Ruby: `identifier`; `default_parameter` -> has_default 1;
  `keyword_parameter` -> name = identifier child, has_default when a
  value child follows; splat/double-splat/block/forwarding -> marker.

Any unrecognized named child collapses to ONE marker param "..." so
arity stays caller-visible; markers never participate in type or
rename comparisons. Extraction failure (no param-list node found,
arena OOM) yields n_params = 0 with params NULL -- the same shape as
a zero-argument function, and the compat gate treats it as
"signature unknown, nothing comparable" (see 3).

## 3. Compat diff (report omnicheck.compat_report/v1)

Public-scope gate (unchanged): signature comparison runs only for
functions present on BOTH sides, in files present on both sides whose
BASELINE side carries exports. One diff finding per changed symbol;
kind precedence when several classes fire on one symbol:
changed-param-count > param-reordered > changed-param-type >
param-renamed. `detail` (one string) carries the old -> new summary.

- Single-signature families (all but Java/C#):
  - length differs, language has defaults (JS family, Python, Ruby,
    C#): every added trailing param WITHOUT default -> BREAKING
    `changed-param-count`; all added params with defaults ->
    informational `added-param-default`. Any REMOVED param -> BREAKING
    `changed-param-count` (defaults do not save callers of removals).
  - length differs, language has NO defaults (Go, Rust): BREAKING
    `changed-param-count`.
  - length equal: if the (name, type) multiset is unchanged but order
    differs -> BREAKING `param-reordered`; else element-wise: type
    differs with BOTH types non-NULL -> BREAKING `changed-param-type`;
    else name differs -> `param-renamed`, BREAKING for named-arg
    languages (Python, C#), informational otherwise. NULL types on
    either side are never compared (honest fill rates).
- Overload families (Java, C#): arity is a property of the signature
  SET. Arity vanished from current -> BREAKING `changed-param-count`;
  new arity -> informational `added-overload`; same arity with fewer
  signatures -> informational `removed-overload` (the last overload
  for an arity is the vanished case above); same arity, same count ->
  signatures sorted by raw text and compared pairwise with the
  single-signature rules.
- v0 kinds (`removed-module`, `removed-export`, `removed-function`,
  `added-*`) keep their exact semantics and shape; the schema string
  bumps to `omnicheck.compat_report/v1`; every change object may carry
  `detail`. Exit codes unchanged (0 SAFE / 1 BREAKING / 2 tool error).
- Determinism: same pinned grammars both sides; per-symbol finding
  with stable kind precedence; existing sort (kind, path, symbol) --
  one finding per symbol keeps the sort total.

## 4. Verification plan

- `tests/fixtures/compat/sig/<family>/{v1,v2}`: one pair per grammar
  family (js, ts, py, go, java, csharp, rust, ruby) exercising count
  (breaking and default-widened), type change (typed families),
  reorder, rename.
- `tests/test_sprint17.c`: extraction contract per family (params
  arrays pinned) + compat v1 end-to-end verdicts via the built binary;
  registered in tests/CMakeLists.txt with the standing definitions.
- `scripts/acceptance/m4_gate.py` criterion 3 extends: the signature
  fixture pairs produce the pinned BREAKING/SAFE verdicts alongside
  the existing v1->v2 / v1->v2safe assertions (which must stay green
  byte-for-byte).
- Matrix: the standing 4-cell from-scratch runner, per cell
  (default, clang-release, sanitize, clang-sanitize), CTEST_PARALLEL
  per the standing mechanism; suites now 32 per cell (test_sprint17
  joins).
- Docs: README compat section, CHANGELOG, docs completeness recount
  (audit_docs.py), SPRINT-17 record. Archive + sha256 sidecar at the
  sprint HEAD.

## 5. Non-goals (unchanged descope lines)

- No cross-version type RESOLUTION (raw source-order type text only,
  no compiler dependency, no generics normalization).
- No call-site analysis, no import-graph usage (pure surface diff).
- No LSP/MCP surface for compat (B4 territory, decision-pending).
- No change to OC001-OC011, check reports, mutation, coverage.

## 6. Sprint record (2026-11-22, built and gated)

Landed exactly on the contract above, with three grammar-shape
discoveries fixed during the build (driven by the vendored grammars'
node-types, not by guessing):

- tree-sitter-ruby v0.23.x names default-carrying parameters
  `optional_parameter` (fields name/value); no `default_parameter`
  node exists. Keyword parameters carry name/value fields.
- tree-sitter-c-sharp v0.24 has NO `default` field on `parameter`;
  `int b = 2` is parameter(type, name, "=" expression) -- the default
  is detected by the anonymous "=" token child (the expression child
  surfaces under its concrete type, not the `expression` supertype).
- Java's `formal_parameter` may lack the `name` field; variable names
  are `identifier` while type names are `type_identifier`, so the
  first identifier descendant is the parameter name.
- Go grouped declarations `(a, b int)` expand to one param per name
  (the `name` field repeats); variadic parameters keep their element
  type text.

Results:

- Extraction pinned per family in `tests/test_sprint17.c` (148
  checks, 0 failures): Go grouped expansion, Ruby optional/keyword
  defaults, TS colon trim, splat/variadic/unnamed markers, Java/C#
  name+default shapes.
- The binding policy table verified end to end on all 8 per-family
  signature pairs (tests + M4 gate criterion 3b), each pinned to its
  exact BREAKING-kind multiset and informational count; the SPRINT-5
  v0 pairs stay byte-preserved on the v1 schema (removed-export +
  removed-function BREAKING exit 1; additions-only SAFE exit 0).
- Matrix: 4 build cells from scratch per cell (the standing
  SPRINT-9 mechanism, CTEST_PARALLEL_LEVEL=2), 32 suites per cell --
  test_sprint17 joins as the 32nd: default, clang-release, sanitize,
  clang-sanitize all green (bundle
  `docs/evidence/gate_day/M4_2026-11-22/bundle.json`; the clang
  cells via the documented no-sudo /tmp/clangroot recipe).
- M4 gate: PASS on all criteria + KPIs at the sprint HEAD
  (`docs/evidence/sprint17_compat_sig_gate_verdict.json`).
- Docs completeness 33/33 with the derived LOC table refreshed
  (142 files / 39,683 lines) and the compat schema documented in
  README + ARCHITECTURE (audit `docs/evidence/docs_completeness.json`).
- Determinism: untouched by construction -- the compat gate is a
  separate command, not in the `oc check` pipeline; the 10k corpus
  digest applies to check reports and was not re-baselined.

Toolchain-drift hygiene (the session sandbox recycle restored a newer
GCC 14.2 whose warnings are fatal under -DOC_WERROR=ON; fixed so the
tree stays warning-clean): nested `/*` sequences inside block comments
(`src/bin/*.rs` text) in src/imports.c, src/parser_ts.c,
tests/test_sprint15.c; dead static helper `pom_exact_pin` removed from
src/supply.c (its caller left in the SPRINT-14 descopes). No verdict
logic touched.

Environment note (recorded for reproducibility): the session sandbox
recycle wiped the working tree including .git; the tree was restored
from the sha256-verified sprint-16 archive and the compatibility
symlink `omnicheck -> work/` was recreated (the V8/istanbul fixtures
carry absolute paths; see the v8 handoff addendum).

Correction to section 4's plan: the repo has no CHANGELOG file (the
sprint records ARE the changelog); this record serves that role. The
README command table and ARCHITECTURE compat section carry the v1
contract instead.
