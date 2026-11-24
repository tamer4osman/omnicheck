# SPRINT-19 -- Track E opens: wave-2 completion begins with PHP (full scope, one sprint)

**Date:** 2026-11-22 · **Base:** SPRINT-18 evidence HEAD (`c669a87`,
restored this session from the sha256-verified
`omnicheck-sprint19-php-contract.tar.gz`, `199c78f9…`, with a history
restart per the documented convention) · **User direction:** "I believe
there are other languages that haven't been added yet, even though they
were part of the plan!" (verified TRUE against the plan's wave pool) and
"Make the PHP in one session" -- the PHP language adoption lands
contract-first, the same recipe Go, Java, C#, Rust and Ruby followed,
in one session.

## 1. The correction (handoff v10 section 1, honored here)

The v9 handoff's "ALL target languages are complete and gated" claim was
wrong for the plan's own pool: the plan waves are 1) Java, Go -> 2) C#,
PHP, Ruby, C/C++ -> 3) Rust, Kotlin, Swift, Scala. Five of ten pool
members were adopted through SPRINT-16; **five remain** (PHP, C/C++,
Kotlin, Swift, Scala). PHP is the first of the five; the wave program is
the north-star goal, not a v1.0-gate precondition (plan section 3, A2
and non-goal 4: v1.0 ships exactly two language packs).

## 2. Contract first (commit `7e5b9fd`, before any adoption code)

The PHP contract landed as a full `docs/LANGUAGES.md` section (eight
sections + the honesty invariants), every shape fact carrying a VERIFIED
block from `scripts/probe_php.c` (four probe files, zero parse errors,
compiled against the vendored runtime + parser with the 0.27 runtime
API): the `program` root for BOTH pure-PHP and HTML-mixed files (the
mixed grammar's `text`/`text_interpolation` nodes are inert to every
analysis), `function_definition`/`method_declaration` with EMPTY methods
carrying NO `body` field (the Java bodyless-slot precedent),
`anonymous_function`/`arrow_function`/`anonymous_class` never symbols,
the one-kind `comment` node, the full decision table
(`default_statement` the DISTINCT default arm counting 0; match arms one
per clause; the full and short ternary the SAME `conditional_expression`
node; `??` a value operator), the two import channels with the
require/include argument guard, the Clover/composer manifest shapes, and
the `use`-through-composer-PSR-4/PSR-0 resolver pre-commit. The grammar
was pinned in `scripts/vendor.sh`: `tree-sitter/tree-sitter-php`
**v0.24.2** (the MAIN tree-sitter org), npm binding tarball sha256
`fdd22530…` cross-checked byte-identical against the codeload tag for
`php/src/parser.c`, `php/src/scanner.c`, `php/src/node-types.json`,
`php_only/src/parser.c` and `common/scanner.h`. LANGUAGE_VERSION 15,
accepted by the vendored runtime v0.27.2. OmniCheck compiles ONLY the
MIXED php+html grammar (`php/`); `php_only/` stays vendored bytes.

## 3. The adoption (this session, one session end to end)

- **Descriptor** (`OC_LANG_PHP`, the 10th registered language;
  `OC_PARSER_MAX_LANGS` 9 -> 10): symbols are `function_definition` and
  `method_declaration` with a real body (the whole declaration is the
  span, OBS-2); EMPTY methods (interface signatures, `abstract`) are
  symbols with complexity exactly 1 through the q_bodyless slot; the
  anonymous constructs are never symbols and their decisions count
  inward, inclusive; comments are the one `comment` kind (the shared
  `Q_COMMENTS` already matches, so q_comments stays NULL); `q_package`,
  `q_main`, `q_top`, `q_accessors` all NULL (PHP has none).
- **Complexity**: `CX_KINDS_PHP` = if_statement, else_if_clause,
  while_statement, do_statement, for_statement, foreach_statement,
  case_statement, match_conditional_expression,
  match_default_expression, catch_clause, conditional_expression;
  `CX_OPS_PHP` = `&& || and or` inside the shared `binary_expression`
  container (cx_ops_node NULL). All kind strings are named node types --
  no anonymous collision (the SPRINT-16 named-only match stays). The
  comp-35 fixture inventory is hand-counted (34 decisions: 22 kind + 12
  operator) and pinned by the gate.
- **Compat (B3) signatures**: parameter names WITHOUT the `$` sigil
  (the `name` node inside the grammar's `variable_name` wrapper), raw
  type text, `has_default` from the anonymous `=` behind the
  `default_value` field, variadic parameters collapse to the `...`
  marker, bodyless declarations carry caller-visible signatures.
- **Resolver v1-php** (`src/imports.c`): the use channel resolves
  through the composer.json PSR-4/PSR-0 mapping read from the ANALYSIS
  ROOT (longest-prefix rule, backslash->slash, root-anchored; PSR-4
  strips the prefix, PSR-0 keeps the full name path; absent/broken
  config = no config, the tsconfig fail-open discipline); a use whose
  leading prefix matches no composer prefix stays
  recorded-and-unresolved (external, the Go module-boundary precedent);
  the include channel resolves its ONE static string argument relative
  to the requiring file's directory (root-anchored lexical `..` pops,
  the Ruby require_relative discipline); the imports relation is a SET;
  no mate marking (runtime load order is not a parse fact). The
  composer table lives in the shared `oc_resolve_ctx`, lazy per root.
- **Coverage**: the Clover XML adapter (`php-code-coverage`'s
  clover.xml) -- `<coverage>` -> `<file name=...>` -> `<line num=
  count=>`; count > 0 covered; dispatched BEFORE the Cobertura reader
  on the SHARED `<coverage>` root (the Clover signature `<file name=`
  entries carrying `<line num=` records is sniffed exact; comments are
  stripped first); malformed records fail loud; unresolvable paths land
  in the ambiguity meta, never guessed.
- **Manifests**: composer.json (`require`/`require-dev` exact pins are
  digits-and-dots with optional leading `v` -- the npm discipline;
  constraint ranges and platform requirements record nothing; the
  `license` field feeds OC010 like package.json) and composer.lock
  (`packages` + `packages-dev` name+version are the RESOLVED pins;
  the lockfile tier is OC009-only). OSV ecosystem **packagist**.
- **Pack**: `packs/php/pack.json`, full scope OC001-OC011 (the FIFTH
  full-scope pack), extensions `.php`, entry points NAME conventions
  only (`index.php` as the pack bare name at any depth; the bin/*
  runner-script convention in the pack glob dialect as `**/bin/*` --
  the `**/Application.java` precedent). The shebang hint is
  family-guarded OFF for PHP by contract (a `#!` line outside PHP
  blocks is template text).
- **Adoption addendum** (`docs/LANGUAGES.md` PHP section 9): the
  OC005 binding rule (the alias never narrows marking -- the C#
  discipline), the composer prefix normalization, the composer pin
  discipline, the pack glob dialect, the longest-prefix verification.

## 4. Verification

- **Unit suite**: `tests/test_sprint19.c` (230 checks, wired into
  `tests/CMakeLists.txt`; the per-cell count moves 32 -> 33): symbols +
  spans, the decision table hand-counts (default arm 0, match comma
  list one arm, ?? 0, short ternary 1), parameters/compat (no `$`,
  promotion defaults, variadic collapse), the use/include record
  channels with the argument guard, the mixed-template inertness, the
  surface inventory (enum_case + const_element + NO exportedness
  filter + no shebang hint), resolver v1-php in-process (PSR-4/PSR-0,
  longest-prefix via a /tmp fixture, guards, set semantics), the Clover
  adapter (anchors/covered/ghost paths, loud malformed input), the
  composer readers (pins/ranges/platforms, the lock resolved pins, the
  packagist snapshot), corpus A integration counts, the pack data.
  `test_cli`/`test_sprint1` pack-count assertions moved 9 -> 10 with the
  adoption.
- **Language gate** (`scripts/acceptance/lang_php_gate.py`, the Ruby
  pattern, 7 criteria): corpus A (9 files) parse clean + byte-identical
  x10 + contracted counts (OC001 1, OC002 1, OC004 1, OC006 8, OC008 2,
  OC011 1; OC003/OC005/OC007/OC009/OC010 silent) + zero imports edges;
  corpus B (15 files + 3 manifests) resolver exact (11 edges with the
  contracted endpoint pairs, decoys unresolved -- Psr, Doctrine, the
  `__DIR__` concatenation, the interpolated include, the missing class
  under a matching prefix, the nested composer.json's fake/ mapping;
  OC003 1, OC005 empty by construction, OC006 3, OC010 2); Clover
  composition (every line covered measures EVERY function, OC007
  exactly one on the comp-35 rollup -- CRAP==comp at 100 percent);
  manifest tier (3 manifests, OC010 2 on the composer.json pair, OC009
  0 without and 2 WITH the vendored packagist snapshot, edges
  unchanged). Verdict `docs/evidence/lang_php_gate_verdict.json` --
  PASS.
- **Corpus gate** (`scripts/acceptance/php_corpus_gate.py`): Debian
  `php-phpseclib3` 3.0.19-1+deb12u6 (upstream phpseclib 3.0.19), 334
  real-world .php files, pinned .deb sha256 + corpus digest; parse
  clean (0 errors, 0 error nodes), byte-identical x5, ZERO imports
  edges (every class load rides the Composer autoloader at runtime --
  the metaprogramming boundary, over-report accepted), measured counts
  pinned (OC001 114, OC002 2, OC004 71, OC006 334, OC008 878, OC011
  48), wall 0.81 s against the 60 s budget. Verdict
  `docs/evidence/php_corpus_gate_verdict.json` -- PASS. (The corpus
  artifacts re-provision per the gate's hint on a fresh checkout, the
  Ruby corpus convention.)
- **Derived docs**: README rows refreshed (languages 10, packs 10, 33
  tests per cell -- including the SPRINT-17-era drift the handoff
  flagged: line 159 said "31 tests" while the true count was 32, now
  33), the first-party LOC table re-derived from the tree (146 files /
  42,435 lines), the badge and matrix cells, the shipped-packs list,
  `THIRD_PARTY_NOTICES.md` PHP table row + provenance note,
  `docs/ARCHITECTURE.md` packs paragraph. `scripts/audit_docs.py`:
  **34/34 items, 100 percent, loc match=True**.
- **clang-format-19**: `check-format.sh` OK (the SPRINT-18 hygiene
  holds; one process lesson recorded: the formatter is for C sources
  only -- a CMakeLists.txt must never be handed to it).
- **Matrix, 4 build cells from scratch per cell** (the standing
  SPRINT-9 mechanism, `CTEST_PARALLEL_LEVEL=2`, 33 suites per cell),
  bundle `docs/evidence/gate_day/M5_2026-11-22/bundle.json`:
  default 72.5s, sanitize 415.9s, clang-release 63.9s,
  clang-sanitize 587.0s -- all green (the per-CELL resume contract was
  exercised twice: the sanitize cell alone exceeds a single command
  window).
- **Determinism audit across the 4 cells** (same bundle, `audit`
  step): ONE report digest across all cells with PHP registered
  (distinct_digests=1); single-core worst Release wall 14.28 s and
  worst sanitize wall 161.86 s against the 90 s sprint budget -- both
  budgets re-affirmed. The m1-replay leg is the M1 gate's obligation
  (the runner removes it from the M5 chain by design); the fresh-clone
  restore story was exercised live at session start -- this session's
  tree came up from the sha256-verified v10 archive through
  `vendor.sh` + configure + build + 32/32 ctest before any adoption
  code existed.

## 5. What comes after PHP (user decisions queued)

- **C/C++** completes wave 2 -- needs the `docs/LANGUAGES.md` C/C++
  contract written FIRST, a grammar probe for tree-sitter-c /
  tree-sitter-cpp (expect the biggest grammar of the pool), and a
  CONTRACT-STAGE decision to present with options: one combined C/C++
  pack vs separate `c` and `cpp` packs (grammar, extensions, entry
  points and pack data differ).
- **Wave 3** (Kotlin, Swift, Scala) is explicitly reserved as a user
  decision -- present the selection and order before building anything.
- The v1.0 gate work (M1-M5 gates, `docs/V1_TAG_RITUAL.md`) remains
  parallel; Track B items B1/B2 remain the user's own work.
