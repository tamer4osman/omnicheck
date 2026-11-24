# SPRINT-21 -- wave-2 closes: C++ adopted (full scope, one session), the approved .h routing treatment implemented

**Date:** 2026-11-23 · **Base:** SPRINT-20 evidence HEAD (`1dbf68b`,
restored this session from the sha256-verified
`omnicheck-sprint20-c.tar.gz`, `1ac3b787…`, with a history restart per
the documented convention; the restored repo's `git init` landed
directly on `main` this time -- the sandbox carries
`init.defaultBranch=main`, so the SPRINT-20 `master -> main` rename
step was a no-op) · **User direction:** "Then let us start C++" --
SPRINT-21 (the C++ language pack; the separate-pack architecture was
user-approved at SPRINT-19 close and restated at SPRINT-20 close) had
NOT started when the previous session ended; this session executes it
end to end: restore → vendor → baseline 34/34 → probe → adoption →
gates → docs → archive.

## 1. The approved architecture, executed

**Separate `c` and `cpp` packs** (the SPRINT-19 close decision,
restated by SPRINT-20 §1): C++ is the SEVENTH full-scope pack, never a
C-pack extension. The KNOWN FUTURE BOUNDARY SPRINT-20 recorded in
`docs/LANGUAGES.md` C section 9 -- `.h` routing gets the content-sniff +
project-root-default treatment written into the report when the cpp
pack registers -- is IMPLEMENTED this sprint (section 4): the boundary
is now behavior, gated by fixture, unit and corpus.

## 2. Vendor + probe (before any adoption code)

`tree-sitter/tree-sitter-cpp` **v0.23.4** (the MAIN tree-sitter org,
the same version number as the C pin) vendored at
`third_party/tree-sitter-cpp/`; the npm binding tarball of the same
version (`tree-sitter-cpp-0.23.4.tgz`, sha256 `615d152f…`) recorded in
`scripts/vendor.sh` as the sha256-verified final fallback and
cross-checked BYTE-IDENTICAL against the git tag for `src/parser.c`,
`src/scanner.c`, `src/node-types.json`, `grammar.js` and `LICENSE`.
`src/` holds `parser.c` + `scanner.c` (the EXTERNAL scanner -- raw
strings, angle-bracket disambiguation; `oc_add_grammar` picks it up
automatically) + `grammar.json` + `node-types.json` + `tree_sitter/`.
The FULL C grammar is inlined at generation time (`grammar.js` requires
`tree-sitter-c/grammar`), so the parser is self-contained.
LANGUAGE_VERSION 14, accepted by the vendored runtime v0.27.2.
`scripts/probe_cpp.c` (ten probe scenarios) verified every contract
shape against the pinned grammar BEFORE the adoption code existed: the
function-definition declarator chains (free, inline method via
`field_identifier`, out-of-class via the `qualified_identifier` `name`
FIELD -- the positional fallback would wrongly take the scope's
`namespace_identifier` --, constructor without a `type` field,
destructor and operator names AS WRITTEN), the template wrappers
(`template_declaration` around definitions and class specifiers), the
parameter shapes (named, reference via `reference_declarator`,
default-value `optional_parameter_declaration`, the parameter PACK
`variadic_parameter_declaration`, abstract), the decision nodes (C's
table verbatim PLUS `for_range_loop` and `catch_clause`; the
`case_statement` value-field family guard carries over), the include
channels (identical to C), the surface shapes (named/anonymous/nested
namespaces, `alias_declaration`, `using_declaration`, `enum class`,
guards, `extern "C"`), the main fact (free-standing only -- a member
`main` does NOT match the identifier-only declarator), the scanner
shapes (raw strings, lambdas, attributes, digit separators,
concepts/requires -- all clean), and the one deliberate error case
(`#include PREFIX "extra.hpp"`). Two NEGATIVE probe facts pinned the
sniff design: `int class = 3;` parses CLEAN under cpp (tree-sitter
error recovery -- keyword identifiers are NOT C-positive evidence);
the K&R old-style definition errors under cpp while parsing clean
under C -- it is the probed C-positive class.

## 3. The adoption (this session)

- **Descriptor** (`OC_LANG_CPP`, the 12th registered language;
  `OC_PARSER_MAX_LANGS` 11 -> 12; `include/oc/imports.h` gains
  `OC_LANG_CPP = 9`): symbols are `function_definition`s with a body
  ANYWHERE -- free functions, inline and out-of-class methods,
  constructors (no `type` field; named exactly like the class),
  destructors (`~X` as written), operator overloads (`operator==` as
  written), templated functions (the wrapper adds no extent).
  Prototypes and member declarations are NOT symbols; lambdas are
  anonymous (decisions count inward). The name is the declarator-chain
  bottom identifier via the SHARED `c_declarator_ident` walk, extended
  with the four cpp node kinds -- C behavior unchanged byte for byte
  (none of the kinds can occur in a tree-sitter-c tree).
- **Signatures** (`extract_params_cpp` + the shared
  `c_own_param_list`/`c_param_type_text`): the as-written type slice
  carries over; `optional_parameter_declaration` records the DEFAULT
  flag (the B3 signature's third slot); the parameter PACK and the
  bare `...` collapse to the `...` marker.
- **Complexity**: `CX_KINDS_CPP` = C's table + `for_range_loop` +
  `catch_clause`; the case_statement value-field guard now fires for
  BOTH families (the same node type inlined from C).
- **Imports** (`Q_CPP_IMPORTS` = the C channel shape + the shared
  `c_include_shape` + resolver v1-cpp): resolver v1-cpp SHARES the
  v1-c implementation verbatim (both `oc_resolve_specifier` and the
  edge loop take `OC_LANG_C || OC_LANG_CPP` -- the C++ preprocessor
  include model is the C model). The `using` family records NOTHING in
  v1 (no honest file mapping without a manifest). Each include pushes
  one NULL-name bind; the imports relation is a SET.
- **Surface** (`Q_CPP_EXPORTS` + the extended climb): top-level
  declarations with C++'s internal-linkage filter -- the C `static`
  rule PLUS anonymous namespaces (a candidate under an anonymous
  `namespace_definition` never surfaces; applied by the climb itself).
  The climb adds NAMED namespace bodies (the PHP namespace precedent)
  and `template_declaration` wrappers. Captured: function definitions,
  declarations (multi-declarator lines name each declarator), type
  definitions, `alias_declaration` (`using X = Y;`), the four
  specifier kinds with name AND body (struct/union/enum/**class**),
  macros. Class members are never module surface (the C# type-nesting
  precedent); enumerator constants stay the documented boundary.
- **Main fact** (`Q_CPP_MAIN`, textually Q_C_MAIN): a free-standing
  function DEFINITION named `main`; the identifier-only inner
  declarator keeps member functions and out-of-class definitions out
  (probed; a class with a method `main` claims nothing).
  `entry_points` is EMPTY in the pack (the m3 loader requires
  PRESENCE, not non-emptiness -- the SPRINT-20 ritual fix).
- **Pack** (`packs/cpp/pack.json`, the SEVENTH full-scope pack):
  extensions `.cpp .cc .cxx .c++ .hpp .hh .hxx .h`; OC001-OC011
  enabled.
- **Supply tier -- the honest delineation**: C++ has NO canonical
  manifest reader in v1. **vcpkg.json / conanfile.py are NOT canonical
  pins** (a recorded boundary: a vendored dependency tree is not a
  manifest, a registry name is not a pinned artifact); OC009/OC010 are
  enabled pack data but nothing is ever discovered for them; the
  supply block is ABSENT with zero manifests; no OSV ecosystem exists
  for C++ includes and none is invented. OC007 accepts Cobertura XML
  through the EXISTING reader (gcovr emits it natively for C++; VERIFIED
  by `tests/fixtures_sprint21/gcovr_cobertura_cpp.xml`); lcov stays out
  of scope v1.

## 4. The .h routing treatment (the approved architecture, implemented)

`oc_packs_route_source` (`src/pack.c`) + `oc_parser_sniff_header`
(`src/parser_ts.c`, stub in `src/parser.c` for no-backend builds):
plain extension matching stops being well-defined the moment both packs
claim `.h`, so routing for that ONE extension goes through:

1. **the content sniff** (parse facts only): a CLEAN cpp parse
   carrying a C++-distinctive node kind (`namespace_definition`,
   `class_specifier`, `template_declaration`, `alias_declaration`,
   `using_declaration`, `concept_definition`, `catch_clause`,
   `for_range_loop`) routes to cpp; a content that ERRORS under cpp
   while parsing clean under C routes to c (the probed C-positive
   class: the K&R definition; keyword identifiers do NOT work --
   error recovery parses `int class = 3;` clean);
2. **the project-root default** (inconclusive sniff): cpp iff the
   discovered file set carries a NON-`.h` cpp-pack extension, else c.
   The default is SCAN-WIDE by design: one C++ source claims every
   inconclusive header for the cpp grammar (which reads C-compatible
   code correctly), and the routing stays one deterministic decision
   per file.

The routing is computed ONCE per file in the scan and the pass loop
sees the ROUTED pack (never a fresh extension match); `oc graph`, the
compat signature scan and the mutation collector apply the SAME
treatment (one routing decision per file everywhere). The counts land
in the report meta's `header_routing` block (JSON) and the human
`header routing:` line -- never silent; blocks absent from
single-C-family scans stay byte-stable.

**The measured consequence on the SPRINT-20 corpus, honestly
documented and re-pinned**: curl 7.88.1 carries ONE `.cpp` file
(`docs/examples/htmltitle.cpp` -- skipped as unknown-type in
SPRINT-20, claimed by the cpp pack since), so all 232 inconclusive
curl headers route to cpp under the root default. The re-pin of
`c_corpus_gate.py`: analyzed 803 -> 804, parse-error FILES unchanged
at 203 (the same files error under both grammars), error NODES
2444 -> 2383 (the cpp grammar recovers the same shapes with fewer
nodes), OC008 1811 -> 1812 (the example joins a duplicate group),
every other rule count unchanged, the routing block pinned
(default_cpp 232). The SPRINT-20 verdict JSON stays as the historical
pre-routing evidence; the gate docstring carries the delta note.

## 5. Tests, fixtures, gates

- `tests/test_sprint21.c` -- **265 checks, 0 failures**, wired as the
  35th suite: symbols/spans (free, inline via field_identifier,
  out-of-class via the qualified name field, ctor, dtor/operator as
  written, templated; prototypes/member declarations/lambdas never
  symbols), the decision table (10 hand-counted cases incl. the
  range-for, the two-handler catch, the C++17 if-with-initializer and
  the lambda-inward rule), parameters (named/reference/default-flag/
  abstract/pack/fnptr-return), the import channels (the macro-path and
  empty-payload guards), the surface (classes, enum class, aliases,
  multi-declarators, the static filter, the anonymous-namespace rule,
  the enumerator boundary), the climb (guards, extern "C", named
  namespace bodies, template wrappers), the main fact (definition
  claims; a member or qualified main does not), resolver v1-cpp (the
  shared implementation, set semantics, the cycle), the SNIFF
  (cpp-positive / C-positive via the K&R shape / inconclusive), the
  ROUTING treatment (all five reasons against the real packs dir), the
  gcovr Cobertura adapter, the honest empty supply tier, corpus A
  (14 files, contracted rule counts, OC006 == 12 exact files, the
  header_routing block exact) and the pack shape.
- Fixtures: `tests/fixtures/cpp/` (14 hand-counted structural files --
  the comp-35 inventory incl. range-for and catch, the 63-line long
  function, the surface boundary file, the SNIFFED C++ header, the
  root-default C-compatible header, the dup pair, the TODO, the include
  channels, the guard-wrapped C++ namespace header, the main-definition
  file, the runner-shaped `*_test.cpp`, the credential header, the
  symbol-shape engine) + `tests/fixtures/cpp_resolver/` (10 files:
  same-dir, `..`-pop, nested-include anchoring, the include cycle, the
  driver) + `tests/fixtures_sprint21/gcovr_cobertura_cpp.xml`;
  `scripts/gen_fixtures_cpp.py` generates them with in-file
  hand-count assertions (the SPRINT-20 discipline).
- Gates (all four PASS, verdicts in `docs/evidence/`):
  - **Language gate C++** `scripts/acceptance/lang_cpp_gate.py` --
    9/9: corpus A parse clean + pack loaded; byte-identical x10;
    contracted rule counts with the EXACT OC006 file set; TWO import
    edges into the analyzed set; the header_routing block exact
    (sniffed_cpp 1 = types.h, default_cpp 1 = local_util.h); corpus B
    resolver v1-cpp exact (9 edges, decoys unresolved, OC003=1, OC005
    empty by construction, OC006=5, OC010=0); the gcovr Cobertura
    composition (16/16 measured, ONE OC007 on sum26 = CRAP==comp at
    100 percent); supply-tier honesty; **the C-pack freeze leg** --
    with the cpp pack registered, fixtures/c keeps EVERY SPRINT-20
    contracted count (both .h by the root default to C) and the
    c_resolver edge set is unchanged (9 edges, OC003=1, OC006=5).
  - **Corpus gate C++** `scripts/acceptance/cpp_corpus_gate.py` --
    7/7 on **abseil 20240722.0** (Debian orig tarball, sha256
    `df6d1f5e…` pinned, 811 .cc/.h files, corpus digest `a855f072…`,
    wall ~5.3s): corpus integrity; parse honesty (analyzed 817 = 811
    C-family + 5 python + 1 C compat file; **646 parse errors -- the
    PINNED raw-source boundary**, classified: 581 the macro-namespace
    class -- `ABSL_NAMESPACE_BEGIN` expands to `namespace absl {` --
    207+ the macro attribute/flag classes, token pasting / X-macros /
    residual; no canonical C++ corpus parses 100% clean as-written --
    catch2 3.7.1 135/427, spdlog 1.15.2 74/132, measured at pin
    time); determinism x5 byte-identical; resolver honesty (21 edges,
    ZERO targets outside the analyzed set, OC006 == zero(809) -
    claimed(18), all 18 with a main shape or a python entry rule);
    rule honesty (OC001 568, OC002 199, OC004 152, OC005 1 -- a
    PYTHON-pack finding, measured; OC006 791, OC008 1724; OC003/007/
    009/010/011 silent; supply block absent); header-routing honesty
    (sniffed_cpp 1, default_cpp 358 -- every .h routed through the
    treatment); wall budget ~5.3s < 60s single-core.
  - **Language gate C re-proven** (the freeze): 7/7 unchanged.
  - **Corpus gate C re-pinned** (the routing delta above): 6/6 on the
    documented new pins.

## 6. Docs

`docs/LANGUAGES.md` C++ section (the full contract: nine sections + the
honesty invariants + the adoption addendum, every shape fact VERIFIED;
the adoption-status table carries the cpp row; the C row's future-
boundary note now says "landed") · `docs/ARCHITECTURE.md`
registered-languages paragraph (the `cpp` clause) ·
`THIRD_PARTY_NOTICES.md` cpp table row + provenance note · README
refresh: 11 -> 12 languages, 11 -> 12 packs, 34 -> 35 tests per cell
(headline + matrix + mermaid + cheat sheet), the first-party LOC table
re-derived (**156 files / 48,625 lines**), the shipped-packs list, the
headline numbers. `scripts/audit_docs.py`: **36/36 items, 100 percent,
loc match=True**.

## 7. Environment notes (this sandbox)

- cmake 4.4.5 + ninja via `pip install cmake ninja`; gcc 14.2. The
  clang-19 matrix cells were NOT re-run this sprint (the M5 chain's
  formal dates are unchanged and its GREEN bundle at SPRINT-20 close is
  committed evidence; a 12-pack M5 re-proof is a decision queued with
  the v1.0 tag -- see the handoff).
- The pinned corpus tarballs (`curl_7.88.1.orig.tar.gz`,
  `abseil_20240722.0.orig.tar.gz`) and their extractions live UNTRACKED
  in the repo root (gitignored; the gate hint prints the provisioning
  recipe; the gates never download).
- The vendored `third_party/tree-sitter-cpp/` is gitignored by design
  and comes back via `scripts/vendor.sh` on the restore path.
