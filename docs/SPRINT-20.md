# SPRINT-20 -- wave-2 continues: C adopted (full scope, one session), the first no-scanner grammar

**Date:** 2026-11-22 · **Base:** SPRINT-19 evidence HEAD (`7c4a72d`,
restored this session from the sha256-verified
`omnicheck-sprint19-php.tar.gz`, `316bbe90…`, with a history restart per
the documented convention) · **User direction:** "did you finish?" —
SPRINT-20 (the C language pack, approved at SPRINT-19 close together
with the C/C++ architecture decision) had NOT started when the previous
session ended; this session executes it end to end: restore → vendor →
baseline 33/33 → adoption → gates → docs → matrix → archive.

## 1. The approved architecture (SPRINT-19 close decision, honored here)

**Separate `c` and `cpp` packs**, never one combined pack: a single
grammar pin cannot serve two dialects, `.h` routing goes blunt, queries
dilute, and the per-language gate granularity is lost. Decision-node
handling is explicit; `.h` content sniffing + project-root default lands
with the C++ pack (the KNOWN FUTURE BOUNDARY, documented in
`docs/LANGUAGES.md` C section 9 -- until then every `.h` is C and the
routing is unambiguous). C goes FIRST: the ambiguity does not exist yet,
the grammar is the smallest of the pool, and it ships NO external
scanner.

## 2. Vendor + probe (before any adoption code)

`tree-sitter/tree-sitter-c` **v0.23.4** (the MAIN tree-sitter org)
vendored at `third_party/tree-sitter-c/`; the npm binding tarball of the
same version (`tree-sitter-c-0.23.4.tgz`, sha256 `31f07c7c…`) recorded
in `scripts/vendor.sh` as the sha256-verified final fallback and
cross-checked BYTE-IDENTICAL against the git tag for `src/parser.c`,
`src/node-types.json`, `grammar.js` and `LICENSE`. `src/` holds exactly
`parser.c`, `grammar.json`, `node-types.json` and `tree_sitter/` -- NO
external scanner (the FIRST OmniCheck grammar without one;
`oc_add_grammar` compiles `src/parser.c` alone). LANGUAGE_VERSION 15,
accepted by the vendored runtime v0.27.2. `scripts/probe_c.c` (seven
probe files) verified every contract shape against the pinned grammar
BEFORE the adoption code existed: the declarator chains (plain,
pointer-return, parenthesized, function-pointer-return), the K&R alias,
the parameter shapes (named, abstract/unnamed, variadic, fnptr), the
decision nodes (the `default:` arm is a `case_statement` WITHOUT the
`value` field -- the family guard's evidence), the include channels
(string_literal / system_lib_string / identifier), the include-guard and
`extern "C"` wrapper shapes, and the multi-declarator line. One
deliberate error case: `#include PREFIX "extra.h"` concatenation is a
genuine grammar-level ERROR node (the raw-source boundary, section 6).

## 3. The adoption (this session)

- **Descriptor** (`OC_LANG_C`, the 11th registered language;
  `OC_PARSER_MAX_LANGS` 10 -> 11; `include/oc/imports.h` gains
  `OC_LANG_C = 8`): symbols are `function_definition`s with a body (the
  whole definition is the span, OBS-2). C has NO name field, so
  `c_declarator_ident` resolves the symbol name at the bottom of the
  declarator chain (positional fallback for the parenthesized wrapper --
  grammar fact: `seq('(', optional(ms_call_modifier), _declarator, ')')`
  carries NO field). **Prototypes are NOT symbols** (the honest
  double-count rationale: unlike Java abstract methods, the definition
  normally exists in the same root, and both would double every function
  and give OC007 two anchors per name); the header API surface is the
  exports query's job. `q_bodyless` stays NULL.
- **Signatures** (`extract_params_c` + `c_own_param_list`): the compat
  (B3) signature records the declarator-chain identifier and the
  AS-WRITTEN type slice (qualifier + base type, whitespace collapsed;
  the pointer is declarator syntax, the documented boundary); abstract
  parameters record the `...` marker with the same slice; variadic
  collapses to `...`; K&R lists carry bare names with the type honestly
  unknown. The function-pointer-return shape carries TWO
  function_declarators and TWO parameter lists -- the OUTER is the
  RETURNED function pointer's type (abstract `(int)`), the INNER one
  (adjacent to the identifier) is the function's own caller-visible
  list; `c_own_param_list` descends the full chain and takes the LAST
  function_declarator (the generic preorder finder would take the wrong
  one -- probed and pinned by fixture).
- **Complexity**: `CX_KINDS_C` = if_statement (else-if is a NESTED
  if_statement inside an else_clause, so each branch increments by
  itself; a bare else_clause = 0), while/do/for (the do-while's `while`
  is part of the do_statement node), case_statement WITH a `value` field
  (a `default:` arm is the same node type WITHOUT it and counts 0 -- the
  family guard in `count_complexity`; fall-through labels are separate
  nodes), conditional_expression (the ternary), plus `&&`/`||` inside
  the shared binary_expression container. Preprocessor conditionals
  (#if/#ifdef/#elif) are NOT decisions (compile-time selection; the
  runtime statements they guard still count through the wrappers).
- **Imports** (`Q_C_IMPORTS` + `c_include_shape` + resolver v1-c in
  `src/imports.c`): the quoted form records `include <p>` and resolves
  against the INCLUDING FILE's directory (the C preprocessor's own rule;
  root-anchored lexical `..` pops, the PHP include-channel discipline);
  the angle form records `system <p>` and NEVER resolves
  (recorded-and-unresolved, the Go module-boundary precedent); a macro
  path records NOTHING (not a static payload). Each include pushes one
  NULL-name bind (the whole-file claim, the Ruby require discipline);
  the imports relation is a SET. `php_probe_path` was renamed
  `probe_path_index` (shared by the PHP and C resolvers).
- **Surface** (`Q_C_EXPORTS` + `c_is_toplevel` + `c_has_static` +
  `c_specifier_defines_named`): top-level declarations with C's own
  internal-linkage exportedness filter (`static` narrows, `extern` does
  not); the toplevel check climbs preprocessor guards (the conventional
  `#ifndef` include guard wraps EVERYTHING in a header) and `extern "C"`
  blocks (single-declaration AND declaration_list forms); captured:
  function definitions, declarations (the tag of a DEFINED specifier in
  the type field + EVERY declarator-field child separately --
  `int a, b = 2, c[4];` is three names), type definitions (the
  declarator-chain name), direct-child tag definitions (name AND body),
  macros (the `name` field). The ENUMERATOR BOUNDARY: enum constants
  are file-scope in C but NOT module surface v1 (documented, pinned).
- **Main fact** (`Q_C_MAIN` + the C branch in the main-hint loop): a
  function DEFINITION named `main` (body required; C functions cannot
  nest; an #ifdef-wrapped main still claims -- the honest over-claim
  direction, OC006 severity is INFO). `entry_points` is EMPTY in the
  pack: no basename glob can express "defines main".
- **Pack** (`packs/c/pack.json`, the SIXTH full-scope pack):
  extensions `.c` AND `.h`; OC001-OC011 enabled.
- **Supply tier -- the honest delineation**: C has NO canonical
  dependency manifest. No C manifest reader exists; OC009/OC010 are
  enabled pack data but nothing is ever discovered for them (no
  fabricated pkg-config/vendored tier: a versionless include is not a
  pin and a vendored tree is not a manifest). Without their inputs the
  passes produce nothing and the report meta says so (the supply block
  is ABSENT with zero manifests; with a snapshot loaded the block
  reports `manifests_scanned: 0`). No OSV ecosystem exists for C
  includes and none is invented. The OC010 licence-file probe is a
  manifest-reader convention and is deferred with the same rationale.
  OC007 accepts Cobertura-shaped XML through the EXISTING reader --
  gcovr emits Cobertura XML natively, so gcovr reports work with zero
  new code (VERIFIED by `tests/fixtures_sprint20/gcovr_cobertura.xml`);
  lcov tracefiles are out of scope v1.

## 4. Tests, fixtures, gates

- `tests/test_sprint20.c` -- **218 checks, 0 failures**, wired as the
  34th suite: symbols/spans (plain, pointer-return, fnptr-return, K&R;
  prototypes never symbols), the decision table (8 hand-counted cases
  incl. the default arm, fall-through labels and the preprocessor
  boundary), parameters (named/abstract/variadic/fnptr/K&R; the OUTER vs
  INNER parameter-list distinction), the import channels (the macro-path
  and empty-payload guards), the surface (tags, typedefs, macros,
  multi-declarators, the static filter, the enumerator boundary), the
  toplevel climb (include guards + extern "C" blocks + function locals),
  the main fact (definition claims, prototype does not), resolver v1-c
  (same-dir, `..` pop, nested-include anchoring, system never resolves,
  set-semantics edges, the a.h/b.h cycle), the gcovr Cobertura adapter,
  the honest empty supply tier (`manifests_scanned` absent), corpus A
  (13 files, contracted rule counts, OC006 == 11 exact files,
  main_app.c claimed by the parse fact), and the pack shape.
- Fixtures: `tests/fixtures/c/` (13 hand-counted structural files --
  the comp-11 rollup, the comp-35 decision inventory, the 63-line
  long function, the prototype/multi-declarator boundary file, the tag/
  typedef/macro surface file, the dup pair, the TODO, the include
  channels, the guard-wrapped header, the main-definition file, the
  runner-shaped `*_test.c`, the credential header) +
  `tests/fixtures/c_resolver/` (10 files: same-dir, `..`-pop,
  nested-include anchoring, the include cycle, the driver) +
  `tests/fixtures_sprint20/gcovr_cobertura.xml`.
- Gates (both PASS, verdicts in `docs/evidence/`):
  - **Language gate** `scripts/acceptance/lang_c_gate.py` -- 7/7:
    corpus A parse clean + pack loaded; byte-identical x10; contracted
    rule counts with the EXACT OC006 file set; exactly TWO import edges
    both into the analyzed set; corpus B resolver v1-c exact (9 edges,
    decoys unresolved, OC003=1, OC005 empty by construction, OC006=5,
    OC010=0); the gcovr Cobertura composition (measured 13/13 functions,
    ONE OC007 finding on sum26 = CRAP==comp at 100 percent); supply-tier
    honesty (supply block absent; with a snapshot loaded
    manifests_scanned == 0 and OC009 == 0 -- no C ecosystem, none
    invented).
  - **Corpus gate** `scripts/acceptance/c_corpus_gate.py` -- 6/6 on
    **curl 7.88.1** (Debian orig tarball, sha256 `cdb38b72…` pinned, 799
    .c/.h files, corpus digest `d288deee…`): corpus integrity; parse
    honesty (analyzed 803 = 799 C + 4 python pack files; **203 parse
    errors -- the PINNED raw-source boundary**, see below); determinism
    x5 byte-identical; resolver honesty (2218 edges, ZERO targets
    outside the analyzed set, OC006 == zero-in-degree minus 128 measured
    entry claims, all 125 claimed C files carry a main definition); rule
    honesty (OC001 614, OC002 41, OC003 4 -- REAL include cycles, OC004
    372, OC006 448, OC008 1811, OC011 27; supply block absent); wall
    budget 2.57 s < 60 s single-core.
- **The honest corpus-gate delta (documented in both the gate docstring
  and `docs/LANGUAGES.md` C section 8)**: PHP files are self-contained
  parse units and phpseclib3 parsed with ZERO errors; C cannot -- real
  C interleaves preprocessor conditionals INSIDE expressions and between
  `else` and its statement, closes `extern "C" {` inside
  `#ifdef __cplusplus`, and uses GNU named-variadic macros and comment
  token pasting; a raw-source grammar cannot fully parse those shapes.
  No canonical C corpus parses 100% clean as-written (measured at pin
  time: zlib 1.3.1 46/53, musl 1.2.3 382/2248, sqlite 3.40.1 tree
  230/436). The criterion is therefore the PIN: any grammar or tool
  regression MOVES the 203/2444 numbers, which is exactly what a
  kill-switch must do; the classified error classes (extern-C guards,
  preprocessor-interleaved expressions/control flow, GNU variadic
  macros, token pasting) are the evidence the errors are the documented
  boundary, not tool breakage.

## 5. Docs

`docs/LANGUAGES.md` C section (the full contract: nine sections + the
honesty invariants + the adoption addendum, every shape fact VERIFIED;
the adoption-status table now carries the Ruby, PHP and C rows that were
missing) · `docs/ARCHITECTURE.md` registered-languages paragraph (the
`c` clause) · `THIRD_PARTY_NOTICES.md` C row + provenance note ·
README refresh: 10 -> 11 languages, 10 -> 11 packs, 33 -> 34 tests per
cell (headline + matrix + mermaid + cheat sheet), the first-party LOC
table re-derived (**151 files / 45,344 lines**; `scripts/audit_docs.py`
tests-component now excludes `tests/fixtures*` -- its own label said
fixtures excluded, and since SPRINT-20 the C fixtures are THEMSELVES .c
files), the shipped-packs list. `scripts/audit_docs.py`:
**35/35 items, 100 percent, loc match=True**.

## 6. Environment notes (this sandbox)

- cmake 4.4.5 + ninja via `pip install cmake ninja` (the recycle wiped
  `/home/z/.venv`; re-proven). The clang-19 debs in `toolchain-debs/`
  survived; `clang-format-19` ships as the tracked deb at the repo ROOT
  (the SPRINT-19 convention).
- The pinned corpus tarball `curl_7.88.1.orig.tar.gz` and its
  extraction `curl-7.88.1/` live UNTRACKED in `work/` and re-provision
  per the gate hint (the phpseclib3 .deb convention); the gate never
  downloads anything.
- The vendored `third_party/tree-sitter-c/` is gitignored by design and
  comes back via `scripts/vendor.sh` on the restore path.

## 7. Gate day (2026-11-22): the M5 ritual chain ran GREEN on the C tree

The mechanized ritual (`scripts/acceptance/gate_day_runner.py --gate M5`)
completed every step on the sprint-20 tree, bundle
`docs/evidence/gate_day/M5_2026-11-22/bundle.json`:

- **tree-clean** on `main` (the restored repo's default branch was
  renamed `master` -> `main`; the v1 tag ritual cuts on `main`).
- **matrix, 4 cells, 34/34 each**: default 101.7s, sanitize 494.1s,
  clang-release 68.8s, clang-sanitize **across 3 chunked ctest windows
  (712.3s)** -- the sanitize cell's ctest ALONE (686s serial for 34
  suites; ASan-instrumented tree-sitter parsing dominates) exceeds a
  10-minute command window at ANY parallelism (-j2 measured 1.16x
  average concurrency, -j4 doubled every suite on 2 cores), so the
  runner gained the documented `--ctest-chunk i/n` constrained-host
  mechanism: ctest split by test index across windows, cell PASS
  recorded only when every chunk ran clean AND the executed-test union
  equals the authoritative `ctest -N` catalog -- partial chunk progress
  never completes the step (the B8 completion-gate pattern).
- **harnesses m2+m3+m4+m5 all PASS** (eslint pin, black pin, the four
  M4 criteria, the four GA criteria). Two same-day ritual fixes, each
  committed BEFORE re-proof:
  - `m4_gate.py` now substitutes the `@@OC_REPO_ROOT@@` coverage
    templates into /tmp copies (the SPRINT-18 `fixture_subst()`
    convention): the harness leg had never run on a fresh restore and
    fed the RAW template, so the V8 url was unreadable and the scan
    exited 2 with an empty report.
  - `m3_gate.py`'s loader half requires `entry_points` PRESENCE, not
    non-emptiness: the C pack detects entries as a parse FACT (main
    definition), so its manifest list is legitimately empty.
  The m5 leg re-ran the full determinism audit itself: **6 runs, 1
  distinct digest** (gcc/clang x Release/Sanitize on the 10k corpus;
  worst Release wall 13.63s, worst sanitize 171.41s).
- **audit** 1 distinct digest (same verdict re-affirmed); **docs**
  35/35, loc match=True (README/SPRINT-20 first-party LOC re-derived to
  45,344 after the ritual fixes); **package** all six legs green
  (package.sh, shim OC_BIN, shim native, npm pack, npm install -g,
  install smoke byte-identical to the repo build).
- The runner's `--cell` WITHOUT `--fresh` (SPRINT-20) and the fixed
  `-j 4` ctest parallelism (build parallelism stays host-adaptive) are
  documented in the runner docstring; the ritual steps 7-9 (version
  bump, package re-run, tag) stay with the human at the formal gate
  date per the runner's contract.

