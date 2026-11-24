# Sprint 15 -- Track D wave 1, fourth language: Rust (full scope, one sprint)

**Date:** 2026-11-21 · **Base:** SPRINT-14 final HEAD (`0eb1c9d`, the
sprint-14 handoff archive; this session re-verified the archive sha256
`306d4500…` before re-initializing the tree) · **User direction:** "finish
the Rust part in one session" -- the Rust language adoption lands contract-
first, the same recipe Go (SPRINT-9/10/11), Java (SPRINT-12) and C#
(SPRINT-13) followed, in one sprint. The session itself was recycled once
mid-flight (sandbox): the persisted scripts (`probe_rust.c`, both fixture
generators, the clang debs) and the handoff archive survived and drove the
recovery; nothing was re-guessed.

## Contract first (commit `b62e7cb`, before any adoption code)

The Rust contract landed as a full `docs/LANGUAGES.md` section, mirroring
the Go/Java/C# structure (grammar, functions & spans, comments, complexity,
imports & resolver, surface/entry/coverage/manifests, honesty invariants).
Every pre-adoption shape guess was probed against the pinned grammar BEFORE
code (the C# verification pattern), and the probe results are recorded as
VERIFIED blocks in place: the `source_file` root, the `use_declaration`
argument field, the NAMED `crate`/`self`/`super` head nodes, the uniform
bodyless node `function_signature_item` (trait signatures AND extern-block
declarations), `foreign_mod_item`, the decision node kinds (no separate
if-let/while-let kinds -- `let_condition` inside `if_expression`/`while_expression`),
doc-comment spans, and macro bodies as token trees.

## Grammar vendoring (commit `6226713`)

`tree-sitter/tree-sitter-rust` **v0.24.0** (the MAIN tree-sitter org --
probed: the grammars org has no rust repo), vendored at
`third_party/tree-sitter-rust/`. Provenance: the npm binding package
tarball (sha256 `4248da0b…`, recorded in `scripts/vendor.sh`), whose
`src/parser.c` and `src/scanner.c` were cross-checked byte-identical
(sha256) against the codeload tarball of the same tag. The SECOND vendored
grammar with an EXTERNAL scanner (`oc_add_grammar` picks it up); the
runtime v0.27.2 accepts its LANGUAGE_VERSION 15. Never edited after
vendoring.

## The adoption (descriptor, resolver, adapters, pack)

- **Descriptor** (`OC_LANG_RUST`, the 8th registered language): symbols
  are `function_item` (free functions, impl methods, trait default
  methods) plus the ITEM-LEVEL named closures (const/static items whose
  value IS a closure_expression -- the Rust analogue of the Go §Go.2
  promotion); local let-bound closures are NEVER symbols (the recorded
  divergence: the binding lives inside the enclosing function's span, its
  decisions count inward, and a second symbol would double-count).
  Bodyless signatures (`function_signature_item`) and `macro_rules!`
  definitions are symbols with complexity exactly 1 (the Java SPRINT-14
  discipline through the q_bodyless slot; macro bodies are token trees --
  no decision parses inside, complexity 1 by construction). Comments are
  `line_comment` (doc markers are children inside the span, consumed
  verbatim). Decision table: if/while/for/loop/match_arm/try + `&&`/`||`
  (one decision per MATCH ARM -- alternative patterns are one split of
  the same value space, gocyclo's case-clause discipline; the comp-35
  hand-count is pinned by fixture and gate).
- **Resolver v1-rust**: two declaration-level channels. The mod-tree
  (`mod name;` records resolve to the ONE module file by the stem rule;
  `#[path = ...]`-attributed mods record NOTHING -- the attribute moves
  the file; mod records push NO binding: declaring is not using) and the
  use-tree (each leaf records its own head: `crate::`/`self::`/`super::`
  anchors with the item-reading pop ladder -- the deepest module file the
  path names is the edge target, the anchor file itself is never probed;
  external crate heads stay unresolved ALWAYS, no manifest is ever read;
  brace groups/nested groups/globs/aliases expand deterministically, the
  binding name is the pre-alias leaf segment). The imports relation is a
  SET for Rust. There is NO same-directory mate marking, deliberately:
  Rust has no same-module mate hole -- every cross-file reference goes
  through a path, and auto-marking same-directory mates would silence
  honest unused verdicts.
- **Surface**: top-level source_file items with PLAIN `pub` (the modifier
  node's exact text; pub(crate)/pub(super)/pub(in path) never cross the
  crate boundary); methods and macros are not surface. Entry points: the
  pack globs (lib.rs, main.rs) plus the `fn main` parse fact (claims
  `src/bin/*.rs` binaries). Test convention: a `.rs` file directly inside
  a `tests/` directory is runner data (both shapes: package-level
  integration-test crates AND src-tree unit-test module files).
- **Coverage**: the **LCOV** adapter (the open format grcov,
  cargo-llvm-cov --lcov and cargo-tarpaulin emit): DA records are the
  anchors, covered when the hit count > 0; FN/FNDA/BRDA/LF/LH are
  consumed with no fabricated line model; malformed input fails LOUD
  (pre-SF keys, non-numeric DA fields, EOF inside an open record).
- **Manifests**: `Cargo.toml` (exact `=x.y.z` requirements are pins;
  caret/tilde/star/ranges are NOT -- unpinned warns, Cargo's own
  semantics; `{ path = ... }` entries are in-repo links, not OC009
  material; `{ workspace = true }` inherits the same manifest's
  `[workspace.dependencies]` entry, an unresolvable inheritance records
  nothing; OC010 layers license / license-file / the project-root licence
  file) and `Cargo.lock` (resolved pins for `[[package]]` entries with a
  `source`; the workspace member itself records nothing). OSV ecosystem
  **crates.io**.
- `packs/rust/pack.json`: **OC001-OC011, all eleven** (the third pack to
  carry the full set).

## The corpus gates caught two real findings

The pinned corpus (Debian `librust-tokio-dev` 1.43.1-1, upstream tokio
v1.43.1, 516 .rs files, sha256 + extraction-relative digest pinned in
`work/`) measured and recorded:

1. **The shebang hint mistook Rust inner attributes for shebangs.** Most
   real crate files open with `#![allow(...)]` / `#![cfg_attr(...)]` --
   the pre-existing `#!`-prefix hint claimed EVERY such file as an entry
   point, silencing honest OC006 verdicts (92 on the corpus before the
   fix, 99 after: seven genuinely unreferenced files -- config.rs,
   process.rs, builder.rs, mock_open_options.rs, metrics/io.rs,
   sync/mpsc/mod.rs, tests/support/mpsc_stream.rs -- had been hidden).
   The hint now excludes `#![` (a kernel shebang interpreter path never
   starts with `[`); pinned by a unit test and recorded in the contract.
2. **Macro-generated declarations are not parse facts** (recorded
   boundary): tokio's `cfg_io_util! { mod buf_reader; ... }` blocks put
   mod declarations inside macro invocations, where the grammar is
   deliberately token-soup-opaque (the probe shows anonymous tokens).
   Expanding macros would guess arbitrary syntax; the over-report
   direction (OC006 verdicts a triage can clear) is the accepted one,
   the same class of boundary as the Go filename-suffix constraints.

The corpus gate also pins the honest OC003=17 (Rust module cycles are
LEGAL -- parent `mod child;` + child `crate::parent::item`; one SCC
verified by hand: src/fs/mod.rs declares `mod canonicalize;` while
canonicalize.rs uses `crate::fs::asyncify`), the OC006 internal-
consistency criterion (zero(269) - tests-conv(169) - entry(1) = 99,
measured live from graph + report), and tokio's own licence clause being
read (OC010 honest-silent).

## Tests and evidence

- `tests/test_sprint15.c` (the 30th suite; matrix cells now 30 tests,
  276 checks): symbols/promotions/bodyless/macros + the local-closure
  divergence, the decision-table hand-counts (comp-10 and comp-35), the
  use-tree expansion (groups/globs/aliases/pre-alias bindings, the
  `#[path]` guard), the resolver (anchors, stem rule, pop ladder, the
  src/bin own-crate exception, external heads, set semantics, escape
  guards), plain-pub exports + restricted visibility, the fn main fact
  scoped to top-level, the LCOV adapter (anchors/covered + loud
  malformed input incl. the TN header and pre-SF keys), the Cargo
  readers (13 deps / 5 pins, path/inherited skips, licence layering,
  resolved pins), OSV crates.io, the tests/ conventions, and the
  inner-attribute/shebang fix; plus `tests/fixtures/rust`,
  `tests/fixtures/rust_resolver` and `tests/fixtures_sprint15/` (unit
  shapes, cargo tier, lcov, the crates.io OSV snapshot).
- The resolver corpus's committed bytes pinned the contract's edge set
  BEFORE the resolver existed (the fixture generators survived the
  session recycle); the gates assert the twelve endpoint pairs exactly.
- Language gates at the sprint HEAD: **`lang_rust_gate` 7/7**
  (structural counts, 12 edge pairs, LCOV composition measured 10/10
  with OC007 only on the comp-35 function at 100 percent coverage,
  manifest tier with the crates.io snapshot) and **`rust_corpus_gate`
  6/6** (integrity, parse clean, determinism, resolver realism, rule
  honesty, wall 1.56s against the 60s budget).
- The docs gate caught the drift as designed: README/ARCHITECTURE pack
  rows and the derived LOC table re-recorded (135 files / 35,095 lines,
  32/32 items).

## Where the work stands

The Rust descope ledger carries the recorded boundaries only (macro-
generated declarations; `extern crate` declarations as a non-channel;
re-exports as import-channel data; no workspace-root manifest
resolution). The next language per the handoff order is Ruby -- write
the contract first. Track B items B1/B2 still await an explicit user
decision.
