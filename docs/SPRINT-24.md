# SPRINT-24 — Scala, the 15th language (wave-3 continues), adopted in one session

Series state at open: SPRINT-23 closed with Swift as the 14th language
(the ninth full-scope pack); the tree carried the restored archive
commit (`a7240cb` restore from `omnicheck-sprint23-swift.tar.gz`,
sha256 `57417cc7...` verified, after the sandbox recycle rolled
`work/` back to SPRINT-21 for the third time — the artifact-restore
path recovered the full state and the restore was committed BEFORE
anything else, the SPRINT-22 lesson). This sprint adopts Scala as the
15th language and the TENTH full-scope pack (OC001–OC011 all enabled),
in one session, by the standing recipe: vendor pin, probe-first,
adoption, fixtures, tests, gates, docs, archive, commit.

## 1. The vendor pin (dual-source byte-identity, with a REJECTED version)

- Repo: **tree-sitter/tree-sitter-scala** (the MAIN tree-sitter org's
  grammar, per the SPRINT-23 close's candidate note), tag
  **`v0.23.4`**. The npm binding package `tree-sitter-scala@0.23.4`
  was cross-verified byte-equal to the git tag for
  parser.c/scanner.c/node-types.json/grammar.json/LICENSE.
- **THE 0.24.0 PIN WAS REJECTED AT THE PIN STEP** (the Swift-lesson
  discipline applied BEFORE vendoring): the npm 0.24.0 package was
  regenerated from a different commit than the v0.24.0 git tag —
  parser state counts differ (20285 vs 19568) and the symbol sets
  diverge. The dual-source discipline requires byte identity, so the
  pin stays at v0.23.4 where the two sources agree. Recorded at the
  vendor.sh fetch line and in the pack notes.
- Scanner audited CLEAN against the two SPRINT-23 upstream defects
  BEFORE adoption: `ts_calloc(1, sizeof(Scanner))` (not calloc(0)),
  ZERO bit shifts, serialize bounded by the
  `(indents.size + 3) * sizeof(int16_t)` guard, deserialize bounded by
  `size < length`. No sanitizer carve-out.

## 2. The probe (probe-first, ten scenarios)

`scripts/probe_scala.c` parses ten scenarios against the pinned
grammar (top-level declarations brace + Scala 3 enum/given/extension
indented, function shapes, class shapes, the decision inventory,
imports + package, the main shapes, scanner shapes, surface shapes,
the error boundary). It caught ONE contract mistake before any
adoption code existed: the probe's first draft used `typealias Handler
= ...` (Kotlin syntax) and the pinned grammar flagged it ERROR —
Scala's alias spelling is `type X = ...`. Every other contract fact
was confirmed by the live parse: the dotted `path` field children
(SEPARATE identifier segments joined by anonymous `.` tokens — and
the anonymous dots THEMSELVES carry the path field, a quirk the
adoption debug caught later), the import trailing shapes
(namespace_wildcard / namespace_selectors / as_renamed_identifier),
the parameter name/type/default_value FIELDS, the operator_identifier
NAMED operator node inside infix_expression, the template_body wrapper
inside object_definition (the main-fact query compiles only WITH the
wrapper — the query-compile failure the first build caught),
case_clause arms inside the catch's case_block, and the two comment
node kinds (comment / block_comment).

## 3. The adoption

- `OC_LANG_SCALA = 12` (include/oc/imports.h);
  `OC_PARSER_MAX_LANGS` 14 → **15** (the cap that silently dropped the
  15th descriptor — the "no grammar 'scala' compiled" error the first
  smoke test caught);
  `oc_add_grammar(scala tree-sitter-scala)` with the provenance
  comment; the descriptor row (functions / imports / exports /
  CX_KINDS_SC / package / comments / main).
- Symbols: `function_definition` with its body field (the Kotlin
  with-body discipline); the bodyless `function_declaration` (trait /
  abstract members) is NOT a symbol. Parameters: the fielded
  `parameter`/`class_parameter` shapes with the variadic
  repeated_parameter_type collapsing to `...`.
- Decision kinds: if_expression, case_clause (every match/catch arm —
  NO separate catch kind), for/while/do-while, and **infix_expression
  C-gated to `&&`/`||` only** — the operator is a NAMED
  operator_identifier, so the shared anonymous-token ops mechanism
  cannot express it; the Java switch-group family-special-case pattern
  applies in `count_complexity`.
- Imports: `scala_import_expand` — the Rust use-tree / PHP
  use-declaration complete-in-C channel. BOTH wildcard spellings
  normalize to `.*`; selector groups expand to one record per
  selector; renames (`as` and `=>`) bind the ORIGINAL name; plain
  paths bind the last segment for ≤2 segments and stay NULL (the
  ambiguous-member whole-file claim) for 3+. The anonymous-dot
  path-field quirk is handled (only NAMED path children join).
- Resolver: **v1-scala shares the v1-kotlin implementation** —
  Scala's package model is decoupled from the file system exactly like
  Kotlin's, so the declared-fact machinery (package clauses +
  top-level names as hints; wildcard fan-out; single requires the
  declared symbol; member = both readings; set semantics) applies
  verbatim; the imports.c dispatch and the surface.c marking branch
  carry the shared-family branches.
- Surface: the compilation_unit-anchored exports (def / class /
  object / trait / enum / type alias / simple val+var patterns), the
  `private` filter via the access_modifier wrapper (the Kotlin
  spelling; `protected` stays surface), every export a hint symbol.
- Entry: the DUAL main parse fact (the object-main definition and the
  `@main` annotation shape), name-checked in the consumer branch.

## 4. Fixtures, tests, gates

- `scripts/gen_fixtures_scala.py` generates
  `tests/fixtures/scala` (13 files: the hand-counted rollup comp 14 /
  sum26 comp 38 / 63-line longRunner / credential / decls symbol
  boundaries with the Scala 3 enum + @main / kit + tools + widget
  import targets / main_app entry hub / todo / dup pair / types with
  the extension + given shapes), `tests/fixtures/scala_resolver`
  (10 files: the import-shape matrix incl. BOTH wildcard spellings,
  the selector pair, the Scala 3 tail rename, the member shape's both
  readings, the cycle pair, the decoys, the default package), and
  `tests/fixtures_sprint24/cobertura_scala.xml`.
- `tests/test_sprint24.c` — the 36th suite, **196 checks, 0 failures**
  (eight unit legs + six corpus legs: symbols, params, the
  hand-counted decisions incl. the Scala 3 indented match, the import
  shapes + binds, the surface + private filter, the main fact, the
  inline declared-fact resolver (6 edges, set semantics, the honest
  degradation), the pack shape, corpus A counts, the graph pins, corpus
  B, the coverage composition (OC007 on sum26, CRAP=1482, the fully
  covered rollup silent), the honest empty supply tier, determinism).
- `scripts/acceptance/lang_scala_gate.py` — 9 criteria ALL PASS
  (parse clean + pack loaded; byte-identical ×10; the contracted rule
  counts with the EXACT OC006 set; the two import edges + no routing
  block; the resolver corpus B exact endpoint pairs; the coverage
  composition; the supply-tier honesty; the shape pins incl. the
  extension-method symbol; the Kotlin freeze leg — the SPRINT-22
  counts unchanged with the scala pack registered).
- `scripts/acceptance/scala_corpus_gate.py` — the pinned reference
  corpus **scala/scala3 3.3.1** (the canonical Scala codebase: the
  compiler, library, scaladoc and the test suite; 14246 .scala + 22
  .sc + 582 vendored .java files; tarball sha256
  `7532561a...` pinned, extraction digest `dce540a7...` pinned). 6
  criteria ALL PASS: integrity, the pinned raw-source boundary
  (1489/14872 files, 7232 error nodes, NINE classified classes), byte
  determinism, resolver honesty (131995 edges, ZERO targets outside
  the analyzed set, the OC006 set exactly the zero-in-degree set minus
  the 292 live-claimed entries), the rule counts (OC001 808, OC002
  857, OC003 12 — real cycles at corpus scale incl. a 46-file cycle in
  the compiler backend, OC004 980, OC005 13094, OC006 4135, OC008 622,
  OC010 2 — the Ruby Gemfile tier on the corpus's own Gemfile, the
  SCALA contribution zero), and the wall budget (168s best vs 300s,
  single-core).
- ctest **38/38** in the default cell.

## 5. The sanitize cell

The SPRINT-19/20/23 discipline: the full 38-suite run under
ASan+UBSan+LSan (Debug + the sanitize preset, sequential). The Scala
adoption carries NO carve-out: the pinned scanner was audited clean at
adoption and the sanitize cell runs fully instrumented — the clean run
is itself the evidence the audit was complete.

## 6. Docs and evidence

- `docs/LANGUAGES.md` gained the full Scala section (the
  nine-subsection shape): the pin (with the REJECTED 0.24.0 narrative),
  the scanner audit, symbols/spans, parameters, the decision table,
  the shared declared-fact resolver + the import-shape matrix, module
  surface + the dual entry fact, coverage, the supply-tier
  delineation, and the honesty invariants (the pinned corpus boundary
  included).
- `docs/ARCHITECTURE.md` gained the scala pack paragraph; README
  carries the SPRINT-24 statistics row set (172 files / 58,269 lines,
  the audit-verified derivation), the 15-language enumeration, the
  Scala adoption bullet, and the 38-test cell count.
- `THIRD_PARTY_NOTICES.md` gained the scala provenance note.
- Evidence JSONs in `docs/evidence/`: lang_scala_gate_verdict.json,
  scala_corpus_gate_verdict.json, m2/m3/m4/m5 gate verdicts,
  docs_completeness.json — all written by the gates on THIS tree.

## 7. The pinned corpus (provisioning recipe)

```
curl -fsSL -o scala3_3.3.1.tar.gz \
  https://github.com/scala/scala3/archive/refs/tags/3.3.1.tar.gz
sha256sum -c <<< "7532561a2b2eac3b3e2340e2469bbd2b6225cae4d8251e9d49cb8af3c0e435a6  scala3_3.3.1.tar.gz"
tar xzf scala3_3.3.1.tar.gz   # -> scala3-3.3.1/
python3 scripts/acceptance/scala_corpus_gate.py --oc build/default/oc --workdir . --runs 2
```

## 8. Session notes

- The sandbox recycled THREE times across the sprint's life; the
  restore was executed first and committed (`a7240cb`) before any
  adoption work — the SPRINT-22 lesson holds.
- The stale-vendor husk failure (interrupted `git clone`s leaving
  empty `.git`-only grammar directories that vendor.sh "already
  present"-skips) bit the restore: nine grammar trees were husks.
  The fix is operational (delete the husks, re-vendor); the pin
  verification (`verify_vendor_pins.py`, 51/51 identical) proves the
  restored bytes.
- The `OC_PARSER_MAX_LANGS 14` cap silently dropped the 15th
  descriptor — the descriptor loop caps at the constant, so the new
  language registered as absent at runtime ("no grammar 'scala'
  compiled into this binary"). The constant now carries the
  SPRINT-24 comment; the smoke test caught it immediately.
- The main-fact query for the object-main shape initially compiled
  WITHOUT the template_body wrapper and failed
  TSQueryErrorStructure — the grammar requires the body wrapper node
  in the query pattern (probed: function_definition never appears as
  a DIRECT child of object_definition).
- The corpus-gate wall (168s) exceeds the kotlin corpus gate's 10s —
  scala3 is seven times ktor's file count; the budget criterion is
  300s and the measured number is pinned.
