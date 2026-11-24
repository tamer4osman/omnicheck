# Sprint 16 -- Track D wave 1, fifth language: Ruby (full scope, one sprint)

**Date:** 2026-11-22 · **Base:** SPRINT-15 final HEAD (`893dc7d`, the
sprint-15 handoff archive `omnicheck-sprint15-rust.tar.gz`, sha256
`2f9799b6…` re-verified before re-initializing the tree) · **User
direction:** "Since it's time for Ruby, consider yourself a lead Ruby
engineer and an expert in C-based debugging -- complete all the required
tasks" -- the Ruby language adoption lands contract-first, the same
recipe Go (SPRINT-9/10/11), Java (SPRINT-12), C# (SPRINT-13) and Rust
(SPRINT-15) followed, in one sprint.

## Contract first (commit `73d5ba8`, before any adoption code)

The Ruby contract landed as a full `docs/LANGUAGES.md` section, mirroring
the Go/Java/C#/Rust structure (grammar, functions & spans, comments,
complexity, imports & resolver, surface/entry/coverage/manifests,
honesty invariants). Every pre-adoption shape guess was probed against
the pinned grammar BEFORE code (`scripts/probe_ruby.c`, the C#
verification pattern), and the probe results are recorded as VERIFIED
blocks in place: the `program` root with `__END__` DATA as `uninterpreted`,
the require/require_relative `call` shapes, `method`/`singleton_method`
with endless definitions as the SAME node and EMPTY methods carrying NO
`body` field, the three name-field kinds (identifier / setter /
operator), `scope_resolution` names, the full decision-kind inventory,
the anonymous-keyword type collision, the `binary` ops container, and
the one-kind `comment` node.

A CONTRACT AMENDMENT (`d98d2ef`) landed before the adoption code: the
whole-file require claim marks every export of a required file used, so
the OC005 verdict set for Ruby is EMPTY by construction (the recorded
Go precedent), pinned by the lang gate.

## Grammar vendoring (commit `325c5ab`)

`tree-sitter/tree-sitter-ruby` **v0.23.1** (the MAIN tree-sitter org;
the grammar has always lived there), vendored at
`third_party/tree-sitter-ruby/`. Provenance: the npm binding package
tarball (sha256 `1f0f0c10…`, recorded in `scripts/vendor.sh`), whose
`src/parser.c` (`4ce46835…`) and `src/scanner.c` (`e7a6196d…`) were
cross-checked byte-identical against the codeload tarball of the same
tag. The THIRD vendored grammar with an EXTERNAL scanner
(`oc_add_grammar` picks it up: heredocs, delimited literals);
LANGUAGE_VERSION 14, accepted by the runtime v0.27.2. Never edited after
vendoring.

## The adoption (descriptor, resolver, adapters, pack)

- **Descriptor** (`OC_LANG_RUBY`, the 9th registered language;
  `OC_PARSER_MAX_LANGS` 8 -> 9): symbols are `method` and
  `singleton_method` (endless definitions are the same node); EMPTY
  methods are symbols with complexity exactly 1 through the q_bodyless
  slot (the Java SPRINT-14 discipline); operator and setter names keep
  the name as written (`[]`, `name=`); blocks are NEVER symbols (their
  decisions count inward, inclusive -- the Rust local-closure
  divergence's sibling); metaprogramming is not parse facts (the Rust
  macro boundary's analogue). Comments are `comment` (one kind covers
  `#` lines AND `=begin`/`=end` blocks, consumed verbatim).
- **Complexity**: the shared walker now matches decision kinds on NAMED
  nodes only -- a Ruby-driven hardening (the pinned Ruby grammar labels
  the anonymous KEYWORD tokens with the same type strings as the
  constructs they open; without the hardening every Ruby count would be
  roughly double; verified by inspection that no other language's kind
  list matches an anonymous node type, and all existing suites stayed
  green). Decision table: if/unless/while/until/for + ONE decision per
  `when` clause (comma patterns are ONE clause, gocyclo's
  case-clause discipline) + ONE decision per `in_clause` (an `if_guard`
  does NOT count separately -- a failing guard falls to the NEXT clause,
  part of the same split) + `rescue` clauses (ensure is not a decision)
  + the five postfix modifier forms + the ternary `conditional`; ops
  `&&`/`||`/`and`/`or` inside `binary` -- the ops CONTAINER node name is
  now part of the descriptor (`cx_ops_node`; Ruby `binary`, every other
  language keeps the shared `binary_expression` default). The comp-35
  hand-count is pinned by fixture and gate.
- **Resolver v1-ruby**: one raw channel, call-shaped -- the query
  captures every call's method field + argument list and the C side
  filters `require`/`require_relative` (receiver forms like
  `Kernel.require` included). The ARGUMENT GUARD: exactly ONE named
  string child with no interpolation/heredoc (multi-argument calls,
  non-string arguments and dynamic payloads record NOTHING, never
  guessed). The specifier carries its own channel tag (the Rust `mod `
  precedent). `require_relative 'p'` probes dirname(F)/p.rb with the
  join anchored at the ANALYSIS ROOT (the from_path is scan-root
  relative, so the lexical normalizer pops `..` across the file's own
  directory -- the one-require_relative-from-test/mycorp shape); `..`
  traversal is legal Ruby and not an escape hazard (the probe can only
  hit the analyzed file set). Bare `require 'p'` probes the two
  DOCUMENTED LOAD-ROOT CONVENTIONS under the analysis root, in order:
  `<root>/p.rb` (a load root at the scan root) then `<root>/lib/p.rb`
  (the gemspec/bundler lib layout) -- the v1-go dotted-path discipline;
  `$LOAD_PATH` is never read from any manifest (the Gemfile decoy
  discipline). The imports relation is a SET for Ruby. There is NO
  same-directory mate marking, deliberately (Ruby's global-constant
  visibility is decided at RUNTIME by load order; auto-marking mates
  would fabricate usage -- the Rust no-mate decision).
- **Surface**: top-level `class`/`module` declarations (name as
  written: `constant` or `scope_resolution`) plus top-level constant
  assignments (plain `constant` left side; the `A::B = 1` left side is a
  recorded descope). NO exportedness filter (Ruby has no file-scope
  visibility). Methods are not surface (the Go method rule);
  `class << self` has no name and is never surface. Entry points: the
  pack globs `bin/*` + `exe/*` (the bundler executable conventions) plus
  the pre-existing shebang hint (`#!` is a comment to the Ruby parser,
  so a `#!` first line is ALWAYS a kernel shebang for Ruby -- no
  false-positive class exists; the Rust inner-attribute trap has no Ruby
  analogue). Test conventions: the runner-collected NAME conventions
  `test_*.rb` / `*_test.rb` / `*_spec.rb` -- name-based only, NO
  directory claiming (the Sprint 2 over-broad-directory lesson). No
  main-method parse fact v1 (the `if __FILE__ == $PROGRAM_NAME` guard is
  a runtime idiom, recorded boundary).
- **Coverage**: the **SimpleCov JSON adapter** (`.resultset.json`, the
  format Ruby's de-facto coverage tool emits): dispatch before the
  istanbul fallback; the `lines` array is positional (index i is line
  i+1); a NUMERIC entry is an anchor covered when > 0; a NULL entry is a
  not-relevant line and is NOT an anchor (nothing fabricated); any other
  element type is a loud USAGE error naming the resultset, suite and
  file; `branches` maps are consumed and skipped (the BRDA precedent);
  suites union per file (the istanbul discipline).
- **Manifests**: `Gemfile` (a bounded single-line `gem`-call reader;
  comments stripped; a spec is a PIN iff it is the bare version form
  `"1.2.3"` -- bundler's own exact-requirement semantics -- or the
  explicit `"= x.y.z"`; `~>`/`>=`/ranges are NOT pins, unpinned = warn;
  `path:` options are in-repo links, not OC009 material) and
  `Gemfile.lock` (the GEM section's 4-space `specs:` entries are the
  RESOLVED pins; the 6-space sub-entries are that gem's own REQUESTED
  ranges and record nothing; GIT/PATH sections are remote-source/
  in-repo links, skipped). OC010 licence for both: the project-root
  licence-FILE probe (the go.mod discipline). OSV ecosystem
  **rubygems**. `.gemspec` files are NOT read in v1 (the DSL's
  evaluation semantics exceed a bounded reader; recorded boundary).
  The scan-time ecosystem routing gains rubygems -> the ruby pack.
- `packs/ruby/pack.json`: **OC001-OC011, all eleven** (the fourth
  full-scope pack).

## The corpus gate measured the recorded boundaries

The pinned corpus (Debian `ruby-sequel` 5.63.0-2, upstream sequel
5.63.0, 280 .rb files, sha256 + extraction-relative digest pinned in
`work/`) measured and recorded:

1. **Zero parse errors across the corpus** -- real-world Ruby (heredocs,
   endless definitions, pattern matching, metaprogramming-heavy DSLs)
   parses clean with the pinned grammar (kill-switch criterion).
2. **Dynamic loading is the Ruby analogue of the Rust macro boundary**
   (measured): sequel loads most extension/adapter files through
   `Sequel.extension` / plugin autoloads -- runtime requires, not parse
   facts -- so the honest zero-in-degree count is large (219 of 280) and
   OC006 over-reports exactly there. The over-report direction (a
   verdict a triage can clear) is the accepted one; the corpus gate's
   internal-consistency criterion pins the verdict set as
   zero(219) - tests-conv(0) - entry(0) = 219, measured live.
3. **The require_relative web of a real gem is a DAG** (measured): 94
   imports edges across 61 targets, all inside the corpus, and OC003 is
   zero -- the recorded contrast with the Rust corpus whose module
   cycles are legal and detected (OC003 = 17 there).

## Tests and evidence

- `tests/test_sprint16.c` (the 31st suite; matrix cells now 31 tests,
  231 checks): symbols/spans (method, singleton, endless, empty = comp
  1, setter/operator names as written, blocks never symbols, block
  decisions count inward), the decision-table hand-counts (branch chain,
  one decision per when clause with the comma discipline, in_clause with
  the guard NOT counted separately, rescue not ensure, the five modifier
  forms, the ternary, &&/||/and/or, the comp-10 shape), the require
  channel with the exact-one-static-string guard, resolver v1-ruby
  (sibling/child/../.. channels root-anchored, the two load-root
  conventions, honest misses, never-guess guards, set-semantics edges),
  surface (top-level exports, the shebang hint, the runner NAME
  conventions), the SimpleCov adapter (union, null-not-anchor, loud
  malformed input, istanbul dispatch unchanged), the Gemfile tier
  (bundler pin semantics, resolved lockfile pins, the licence probe),
  OSV rubygems, and both fixture corpora end to end; plus
  `tests/fixtures/ruby`, `tests/fixtures/ruby_resolver` and
  `tests/fixtures_sprint16/` (unit shapes, the simplecov tier, the
  gemfile tier, the rubygems OSV snapshot). Generators tracked in-repo
  (`gen_ruby_fixtures.py`, `gen_sprint16_fixtures.py`, the
  gen_fuzz_corpus convention).
- Language gates at the sprint HEAD: **`lang_ruby_gate` 7/7**
  (structural counts, zero edges on corpus A, the resolver corpus with
  the EIGHT contracted edge pairs and the decoys unresolved, SimpleCov
  composition measured 15/15 with OC007 only on the comp-35 function at
  100 percent coverage, the manifest tier with the rubygems snapshot)
  and **`ruby_corpus_gate` 6/6** (integrity, parse clean, determinism,
  resolver realism, rule honesty, wall 0.88s against the 60s budget).
- The docs gate caught the drift as designed: README/ARCHITECTURE pack
  rows and the derived LOC table re-recorded (141 files / 38,332 lines,
  33/33 items).

## Where the work stands

The Ruby descope ledger carries the recorded boundaries only
(metaprogramming; `.gemspec` unread; DSL files without the `.rb`
extension outside the analyzed set; no main-method parse fact; the
scope-resolution constant-assignment left side; the two-convention
load-root probe). Nothing pre-committed remains. Track B items B1/B2
still await an explicit user decision.
