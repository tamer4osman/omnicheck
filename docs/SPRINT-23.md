# SPRINT-23 — Swift, the 14th language (wave-3 continues), adopted in one session

Series state at open: SPRINT-22 closed with Kotlin as the 13th language
(the eighth full-scope pack); the tree carried the restored + re-proven
archive commits (`5c0cb96` restore from
`omnicheck-sprint22-kotlin.tar.gz`, sha256 `041794bf...` verified;
`c8c6799` the re-proof evidence). This sprint adopts Swift as the 14th
language and the NINTH full-scope pack (OC001–OC011 all enabled), in
one session, by the standing recipe: contract first, vendor pin,
probe-first, adoption, fixtures, tests, gates, docs, archive, commit.

## 1. The contract (recipe step 1)

`docs/LANGUAGES.md` gained the full Swift section BEFORE the adoption
code existed (the nine-subsection Kotlin shape): the grammar pin, the
function/symbol/spans semantics, the parameter shapes, the decision
table, the resolver v1-swift design, the module surface + the entry
fact, coverage, the supply-tier delineation, and the honesty
invariants. The section records every probe fact and every measured
grammar boundary; where the design leans on precedent the precedent is
named (the C# constructor and accessor precedents, the Kotlin elvis
and sibling-walk precedents, the Rust match_arm discipline, the C# 9
top-level-statements precedent).

## 2. The vendor pin (dual-source byte-identity)

- Repo: **alex-pinkus/tree-sitter-swift**, tag
  **`0.7.1-with-generated-files`** (no v prefix; the generated parser
  files ship on the dedicated tag). The tree-sitter ORG's
  tree-sitter-swift is an abandoned unrelated project (its README says
  so); every npm `tree-sitter-swift` version publishes from
  alex-pinkus — the canonicality check is recorded in
  `THIRD_PARTY_NOTICES.md`.
- npm twin: **`tree-sitter-swift@0.7.1`** (the npm latest, published
  2025-06-23, FROM the `-with-generated-files` tag). The five contract
  files are byte-identical between the GitHub tag tarball and the npm
  tarball (verified twice: the pre-adoption research download AND the
  adoption-time `cmp`):
  `src/parser.c` `6755b167...`, `src/scanner.c` `a30d8d36...`,
  `src/node-types.json` `17755276...`, `src/grammar.json`
  `6e2a8176...`, `LICENSE` `3533cec1...`.
- LANGUAGE_VERSION **14**, accepted by the vendored runtime (ABI window
  13..15). External scanner (`src/scanner.c`, 33 external tokens) picked
  up by `oc_add_grammar` automatically. The alternative-ABI emissions
  (`parser_abi13.c`/`parser_abi14.c`) stay vendored bytes, uncompiled.
- `scripts/vendor.sh` gained the pin (ref + npm sha256 fallback) and a
  SIXTH fetch argument carrying the PLAIN npm version — the tag and the
  npm version differ for this grammar, the only grammar where that is
  so.

## 3. The probe (probe-first, ten scenarios)

`scripts/probe_swift.c` (ten scenarios, compiled against the vendored
runtime with parser AND scanner) verified every contract shape BEFORE
the adoption code existed. The probe caught THREE contract-source
mistakes that would have shipped wrong adoption code:

1. The generated parser labels a parameter's TYPE (and a function's
   return type) with the SAME `name` field as the declaration's own
   name — field-name lookup cannot distinguish them; the extraction is
   POSITIONAL (the named child after the `:` token).
2. A protocol requirement's default body parses as a SIBLING wrapper
   (a second `protocol_function_declaration` with the `body` field, a
   DIRECT protocol_body child) — NOT nested inside its requirement; the
   promotion query pairs them with the adjacency anchor (`.`).
3. The parameter default value trails AT THE LIST LEVEL (the Kotlin
   shape verbatim), not inside the parameter node.

Measured grammar boundaries recorded in the contract: import aliases
do not parse (`import struct Kit.Tag as T`); the `#if` directive
conditions flatten onto the source_file (the directive-builtin
exclusion exists for exactly this); the property `name`-field pattern
parses in TWO shapes (direct `simple_identifier` and the
`bound_identifier` wrapper — the walk handles both).

## 4. The adoption

- `packs/swift/pack.json`: `.swift`, all eleven passes, `entry_points`
  EMPTY by design (the entry claim is the top-level-statement parse
  fact).
- Parser: the Swift descriptor (14 registered languages), the symbol /
  import / export / comment / accessor / top queries, the positional
  parameter extraction, `swift_import_shape` (whole-module /
  submodule / kind-scoped member; the module is the FIRST segment),
  `swift_is_private` (private AND fileprivate — the C static rule's
  Swift spelling), `swift_accessor_name` (the parent climb; the
  synthesized `Prop.get`/`Prop.set`/`Prop.willSet`/`Prop.didSet`), the
  init/deinit synthesized spellings, the directive-builtin exclusion
  for the entry fact, and the `Package.swift` manifest-target walk
  (`oc_surface_hint_push_swift_target` — the new hint channel).
- Resolver v1-swift (`src/imports.c`): the declared-fact index keyed
  on manifest-declared target facts; per-file module assignment by the
  DECLARED path (explicit `path:` as written, else the SPM documented
  default `Sources/<name>`); wholesale fan-out on the exact module
  match; the honest no-hints degradation; set semantics.
- Surface: the Swift marking branch (wholesale vs named claims) in
  `src/surface.c`; the hint-struct extension in `include/oc/surface.h`.
- Build wiring: `third_party/CMakeLists.txt`
  (`oc_add_grammar(swift tree-sitter-swift)`), the root CMakeLists
  link line + the status message (which also gained the missing
  `+ kotlin` the SPRINT-22 message had dropped).

## 5. Fixtures, tests, gates

- `scripts/gen_fixtures_swift.py`: corpus A (17 files, the SPM layout:
  Package.swift + five declared targets + nine module-less loose
  files; the hand-counted comp fixtures rollup = 14 / sum26 = 35; the
  dup clone pair with the header-shape trick the OC008 10-line window
  demands) and corpus B (15 files, 9 contracted edges: the wholesale
  fan-out, the kind-scoped member claim, the submodule first-segment
  rule, @testable, the DEFAULT path, the cycle, the unresolved decoys)
  plus the Cobertura composition fixture. Three fixture drafts were
  corrected against the measured scans (the TODO word in the header
  comment, the longfn repeated-statement window storm, the dup-b clone
  shape) — every published count is measured, not assumed.
- `tests/test_sprint23.c` — **180 checks, 0 failures** (the 37th
  suite): symbols (incl. the init/deinit synthesis and the protocol
  default body), accessors (incl. the empty-didSet absence pin), the
  parameter shapes (external labels, the variadic marker, defaults),
  the hand-counted decision inventory, the import shapes (the two-Mod
  bind records), the surface, the entry fact + the directive
  exclusion, the manifest targets, the resolver end to end (9 edges +
  the no-hints degradation), the pack shape, the corpus pins, and the
  C/C++/Kotlin freeze legs. The stale language-count pins in
  test_cli/test_sprint1 bumped 13 → 14 with the sprint note. The
  registration step (this session's close) fixed two authored-vs-built
  drifts in the staged legs: the JSON literals were spaced
  (`"files_analyzed": 17`) where the serializer is compact
  (`"files_analyzed":17`, the SPRINT-22 convention), and the inline
  entry-fact manifest declared an EMPTY `targets: []` while expecting
  the declared target "X" to be recorded — the manifest now declares
  `.target(name: "X")`.
- Gates: `lang_swift_gate.py` — **9/9** (corpus A/B contracts, the
  coverage composition 34/3 + CRAP(sum26) == 35, the supply-tier
  honesty, the accessor + entry-fact pins, the C/C++/Kotlin freeze
  legs); `swift_corpus_gate.py` — **7/7** on
  **apple/swift-argument-parser 1.5.0** (GitHub tag tarball, sha256
  `946a4cf7...` pinned, corpus digest `3bb0ac6f...`, 194 files, 146
  .swift): parse honesty (146 analyzed, **28 parse-error files — the
  PINNED raw-source boundary, classified**: 27 the
  attribute-empty-parens shape (`@Flag()` / `@Argument()` /
  `@Option()` / `@OptionGroup()` — the pinned grammar's largest gap on
  this corpus), 1 directive-adjacent), determinism ×5 byte-identical,
  resolver honesty (**3294 edges, zero targets outside the analyzed
  set**; OC006 == zero(101) − claimed(3, each with a top-level
  statement shape — the two manifests carry real
  `package.targets.append(...)` code and are honestly claimed)), rule
  honesty (OC001 18, OC002 38, OC003 0 — SPM forbids target cycles and
  the corpus is acyclic, the honest zero —, OC004 7, OC005 0, OC006
  98, OC008 900, OC011 0), the supply tier ABSENT (no OSV Swift
  ecosystem exists and none is invented), no header routing, wall
  ~0.9 s < 60 s single-core.
- The freeze legs re-proven: lang_c 7/7, lang_cpp 9/9, lang_kotlin
  9/9 (the kotlin gate's own C/C++ freeze legs included) — the C, C++
  and Kotlin corpora scan UNCHANGED with swift registered.
- Tests: **37/37 suites pass** (ctest).

### 5a. The sanitize cell (SPRINT-21 discipline), and two pinned upstream scanner defects

The sanitize preset (Debug + ASan + UBSan + LSan, SPRINT-19/20 full-cell
discipline) surfaced TWO upstream defects in the pinned Swift external
scanner — the first vendored-code sanitizer findings of the series — plus
one recoverable upstream warning in the Kotlin scanner:

1. **Swift scanner, memory safety**: `calloc(0, sizeof(struct
   ScannerState))` (`src/scanner.c:237`) allocates a ZERO-byte region
   and serialize/deserialize then read/write the 4-byte state word out
   of bounds (`src/scanner.c:251`) — a hard ASan heap-buffer-overflow
   abort on the first Swift parse. Upstream master fixed it
   (`calloc(1, ...)` + an OOM guard); no npm twin exists to re-prove
   the dual-source pin against (npm latest is still 0.7.1,
   published 2025-06-23), so the pin STAYS and the vendored bytes stay
   UNMODIFIED.
2. **Swift scanner, UB with behavioral fallout**: the
   operator-suppressor mask loop shifts `1 << suppressor` as a signed
   int (`src/scanner.c:514`); at shift 31 the int shift is UB and the
   mask word is SIGN-EXTENDED into the uint64_t test. Upstream master
   uses `1ULL << suppressor`. A corollary measured on this tree: the
   `#if compiler(...)` DIRECTIVE path is build-configuration unstable —
   the same bytes parse clean at -O2 and carry an error node at -O0
   inside the full binary — so the unit suite pins only
   build-stable manifest shapes (the directive-builtin exclusion stays
   exercised at corpus scale: the pinned corpus's root manifest carries
   a real `#if` and its parse boundary is the corpus gate's classified
   28/142 pin).
3. **Kotlin scanner, recoverable**: `memcpy(buffer, stack->contents,
   stack->size)` with `contents == NULL` on the empty-stack serialize
   path (`src/scanner.c:517`) — a nonnull-attribute UBSan warning, not
   an abort, no bytes copied, no memory-safety impact; the allocation
   itself is correct. Suites pass; classified as the documented
   upstream shape at the pinned 0.3.8 tag.

Treatment (all three recorded in `THIRD_PARTY_NOTICES.md`): the
project-owned build glue (`third_party/CMakeLists.txt`) excludes ONLY
`tree-sitter-swift/src/scanner.c` from ASan and UBSan instrumentation
under the sanitize preset — one translation unit, both defects named,
revisit on the next tagged release. First-party code and every other
vendored scanner remain fully instrumented. The sanitize cell runs
GREEN with the carve-out: **37/37 suites, 0 failures, sequential** —
test_fuzz is the longest leg at 563.6 s (the full-tree corpus leg
under ASan), the Swift suite 62.8 s; the cell total is ~23 minutes
chunked into 600 s windows (the SPRINT-20 chunked-window convention).

## 6. Docs and evidence

- `docs/LANGUAGES.md` Swift section (nine subsections + the honesty
  invariants); `docs/ARCHITECTURE.md` grew the Swift pack paragraph
  (the resolver design, the entry fact, the supply delineation);
  README stats re-derived (167 files / 55,523 lines, the language list
  14); `THIRD_PARTY_NOTICES.md` Swift provenance note (the
  canonicality correction, the dual-source hashes, the npm-version
  fallback detail); `.gitignore` carries the corpus provisioning
  entries.
- Docs audit: `scripts/audit_docs.py` — **38/38 items, 100 percent,
  loc match=True** (167 files / 55,523 lines).
- Evidence verdicts: `docs/evidence/lang_swift_gate_verdict.json`,
  `docs/evidence/swift_corpus_gate_verdict.json` (both written by the
  gates on this tree); the refreshed C/C++/Kotlin verdicts from the
  freeze-leg re-proof.

## 7. The pinned corpus (provisioning recipe)

`swift-argument-parser_1.5.0.tar.gz` (sha256
`946a4cf7bdd2e4f0f8b82864c56332238ba3f0a929c6d1a15f55affdb10634e6`)
lives UNTRACKED in the repo root (gitignored; the extraction
`swift-argument-parser-1.5.0/` likewise). Provision: `curl -sSL -o
swift-argument-parser_1.5.0.tar.gz
https://github.com/apple/swift-argument-parser/archive/refs/tags/1.5.0.tar.gz
&& tar xzf swift-argument-parser_1.5.0.tar.gz` — the gate prints this
hint and NEVER downloads (the analysis universe is the pinned archive;
the corpus digest `3bb0ac6f...` is the reproducibility anchor).

## 8. Session notes

- Two sandbox recycles across the sprint's life (the SPRINT-22 lesson
  held both times: the restore was committed FIRST, so nothing was
  lost); the final close ran from the verified 3.3 MB tracked-only
  artifact through the restore → vendor.sh → register → gates →
  sanitize → docs → snapshot pipeline end to end.
- The vendored `third_party/tree-sitter-swift/` (and kotlin, and every
  other grammar) is gitignored by design and comes back via
  `scripts/vendor.sh` on the restore path; the four pinned corpus
  tarballs re-provision by the recipe in section 7 (all four sha256
  pins verified on the restore).
- Queued decisions (unchanged): the v1.0 tag manual step (formal M5
  date unchanged, 2027-12-31; now carrying the 14-pack M5 re-proof
  decision); wave 3 continues with Scala.
