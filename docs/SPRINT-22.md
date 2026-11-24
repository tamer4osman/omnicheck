# SPRINT-22 -- wave-3 opens: Kotlin adopted (full scope, one session), the declared-fact resolver

**Date:** 2026-11-23 · **Base:** SPRINT-21 evidence HEAD (`c798530`,
the restored SPRINT-20 archive + the two SPRINT-21 commits) ·
**User direction:** "Make the Kotlin in one session" -- the same
one-session discipline as SPRINT-21: restore/baseline → vendor → probe
→ adoption → tests → gates → docs → archive. Mid-sprint the sandbox was
recycled twice (all working-tree edits lost once, recovered by
re-applying every edit from the session record and re-verifying the
vendored grammar byte-identity against the npm pin); the committed tree
is the recovered, fully re-proven state.

## 1. The adopted architecture

Kotlin is the THIRTEENTH registered language and the EIGHTH full-scope
pack (`packs/kotlin/pack.json`, extensions `.kt .kts .ktm`, OC001-OC011
all enabled) -- never a JVM-pack extension of Java (the
one-language-one-pack invariant). Wave-2's freezes are honored: the C
and C++ packs scan their SPRINT-20/21 corpora UNCHANGED with kotlin
registered (freeze leg criterion 9 of the language gate, re-proven
again by the standalone C/C++ gates after every late change).

## 2. Vendor + probe (before any adoption code)

`fwcd/tree-sitter-kotlin` **0.3.8** (the canonical community grammar --
the MAIN tree-sitter org ships no Kotlin grammar; the tag carries NO v
prefix) vendored at `third_party/tree-sitter-kotlin/`; the npm binding
package of the SAME version (fwcd's own `tree-sitter-kotlin`,
`tree-sitter-kotlin-0.3.8.tgz`, sha256 `e2da896c...`) recorded in
`scripts/vendor.sh` as the sha256-verified final fallback and
cross-checked BYTE-IDENTICAL to the git tag for `src/parser.c`,
`src/scanner.c`, `src/node-types.json`, `src/grammar.json` and
`LICENSE`. LANGUAGE_VERSION 14, accepted by the vendored runtime
v0.27.2. The EXTERNAL scanner (`src/scanner.c` -- automatic semicolons,
string-interpolation start/end/content, nested multiline comments, the
safe-navigation token) is picked up by `oc_add_grammar` automatically.

`scripts/probe_kotlin.c` (ten probe scenarios) verified every contract
shape BEFORE the adoption code existed: the declaration shapes (all of
class/interface/enum are class_declaration nodes; object_declaration;
property_declaration with the val/var binding_pattern_kind; type_alias),
the function shapes (extension functions: the receiver lives inside the
receiver type so the declaration's ONLY direct simple_identifier is the
name; generic/suspend/inline modifiers as separate children), the
parameter shapes (the FIELD-LESS parameter node; the default value
trailing at the LIST level as an anonymous `=` SIBLING; the vararg
`parameter_modifiers` sibling), the decision nodes (if_expression with
else-if nested, when_entry, for/while/do-while, catch_block,
elvis_expression, the conjunction/disjunction containers), the import
shapes (single/wildcard/alias/member; the dotted path inside ONE
identifier node), the main fact (source_file-anchored; a member
`fun main` claims nothing), the scanner shapes (templates, raw strings,
nested comments, safe-nav -- all clean), and the error-recovery
boundary. Three probe-source bugs were caught and fixed during probing
(the vararg position, two C-isms) and two genuine grammar boundaries
were measured and documented: the in-range-when-after-expression-entry
restriction and the one-line class-body MISSING automatic-semicolon.

## 3. The adoption (this session)

- **Descriptor** (`OC_LANG_KOTLIN`, the 13th registered language;
  `OC_PARSER_MAX_LANGS` 12 -> 13; `include/oc/imports.h` gains
  `OC_LANG_KOTLIN = 10`): symbols are `function_declaration`s WITH a
  `function_body` -- free functions, methods, extension functions,
  generic/suspend/inline functions. Body-less declarations (abstract/
  interface members) are NOT symbols (Kotlin has no prototypes; the C
  prototype discipline). Lambdas and anonymous functions are anonymous
  constructs (decisions count inward). The name is the declaration's
  only direct `simple_identifier`.
- **Accessors** (the C# SPRINT-14 accounting-hole precedent): property
  accessors with a real body ARE symbols named `Prop.get`/`Prop.set`.
  The pinned grammar parses the accessor as a SIBLING of its
  property_declaration (both children of the body node) -- the owner is
  the nearest PRECEDING property sibling (probed:
  ts_node_parent(getter) is class_body; the C# parent-of shape does not
  transfer). Accessor decisions count (tracked.set comp 2).
- **Signatures** (`extract_params_kotlin`): the field-less `parameter`
  node (name = direct simple_identifier, type = the first type-ish
  child as written, default flag via the list-level `=` sibling); the
  vararg parameter collapses to the `...` marker.
- **Complexity**: `CX_KINDS_KT` = if_expression (else-if nested),
  when_entry (EVERY arm counts including else -- the Rust match_arm
  discipline), for/while/do-while, catch_block, elvis_expression, and
  the conjunction/disjunction containers (Kotlin has NO shared
  binary_expression; as kinds each named node counts once per
  occurrence -- identical semantics to the ops mechanism).
- **Imports** (`Q_KT_IMPORTS` + `kotlin_import_shape`): single (1-2
  segments binds the last segment, the Java single-type record), the
  AMBIGUOUS member shape (3+ segments: NULL bind, whole-file claim, the
  C++ include discipline; the resolver matches BOTH readings), the
  wildcard (`.*` suffix, the Java marker), the alias (binds the
  ORIGINAL last segment -- Kotlin cannot alias a member).
- **Resolver v1-kotlin: the DECLARED-FACT resolver** (the sprint's
  central design decision). Kotlin's package model is DECOUPLED from
  the file system (no package==directory rule exists; javac enforces
  one for Java, kotlinc does not), so NO directory probe is honest.
  Resolution keys on the DECLARED parse facts recorded as parse hints:
  the per-file package_header clause (the Go package_clause channel,
  `Q_KT_PACKAGE`) and the per-file top-level declaration names (the C#
  namespace-hint channel, NEW `oc_surface_hint_push_symbol`, fed by the
  exports loop). Wildcard -> package fan-out; single -> files whose
  declared package matches AND that declare the symbol (union fan-out,
  set semantics, lexicographic order); member -> both readings. The
  hints reach the resolver through a new plumbing seam:
  `oc_imports_resolve_ex_hints` (the plain `_ex` delegates with NULL --
  every Kotlin specifier stays unresolved there, the honest
  degradation), `oc_resolve_ctx_set_hints`, and the lazy declared-fact
  index (sorted by (package, path), binary-searched). The scan path and
  the surface build both pass the hints. Real cycles are found: OC003
  fired 18 times on the pinned corpus -- the second pack after Java
  with live cycles at corpus scale.
- **Surface** (`Q_KT_EXPORTS` + the direct source_file parent check +
  `kt_is_private`): top-level named declarations; private is
  file-private (never surface); members, enum entries and the anonymous
  companion object never surface. Every exported name is also a
  hint-symbol record.
- **Main fact** (`Q_KT_MAIN`): a source_file-anchored function
  DEFINITION named `main` (the Rust shape; both entry spellings match;
  a member `fun main` claims nothing). `entry_points` is EMPTY BY
  DESIGN (the m3 loader requires PRESENCE, not non-emptiness).
- **Pack** (`packs/kotlin/pack.json`, the EIGHTH full-scope pack):
  extensions `.kt .kts .ktm`; OC001-OC011 enabled.
- **Supply tier -- the honest delineation**: Kotlin has NO canonical
  manifest reader in v1. Gradle build files are NOT pins (a plugins
  block is not a version-pinned artifact; libs.versions.toml is a
  catalog, not a lockfile). OC009/OC010 are enabled pack data but the
  kotlin pack contributes ZERO manifest rows on any scan; on a
  Kotlin-only scan the supply block is ABSENT (on a mixed scan the
  block may carry the OTHER packs' tiers -- the Java gradle reader
  scanned 124 build.gradle.kts on the pinned corpus -- while the
  Kotlin contribution stays zero); no OSV ecosystem exists and none is
  invented. OC007 accepts Cobertura-shaped XML through the EXISTING
  reader (Kover emits JaCoCo-compatible XML; the JaCoCo reader is a
  recorded descope v1).

## 4. Tests, fixtures, gates

- `tests/test_sprint22.c` -- **222 checks, 0 failures**, wired as the
  36th suite: symbols/spans (free, method, extension, object member,
  generic/suspend/inline; body-less and lambdas and properties never
  symbols), the accessor promotion (cached.get comp 1, tracked.set
  comp 2 with its if; the initializer-only property is no accessor),
  parameters (named/type-as-written/default-flag/vararg-marker), the
  decision table (hand-counted: rollup comp 14, the two-arm when comp
  3), the import-channel shapes (specs + binds for all five shapes),
  the surface (top-level only, private excluded, members excluded, enum
  entries excluded; the hint-symbol channel mirrors the exports), the
  main fact, the declared-fact resolver END TO END on the inline
  corpus (9 edges exact: the two-file package union, the wildcard
  fan-out, the alias pair, the member shape, the cycle pair, the
  default-package claim; the decoys unresolved; the no-hints
  degradation = zero edges; the duplicate-record set semantics), corpus
  A contracted counts (analyzed 14, OC001 1, OC002 2, OC004 1, OC006
  10 exact file set, OC008 2, OC011 1, no header_routing block), corpus
  A graph pins (comp 14/35, the accessor symbols, 3 edges), corpus B
  counts (OC003 1, OC006 5, OC005 0 by construction), the Cobertura
  composition (18 total / 2 measured / ONE OC007, CRAP == comp == 35),
  the honest supply tier, determinism, and the pack shape.
- Fixtures: `tests/fixtures/kotlin/` (14 hand-counted structural files
  -- the rollup, the comp-35 sum26, the 63-line longRunner, the symbol
  boundary decls, the accessor types, the TODO, the dup pair, the
  credential, the main hub + its three targets, the import channels) +
  `tests/fixtures/kotlin_resolver/` (11 files: the two-file package,
  the wildcard user, the alias, the member shape, the cycle pair, the
  decoys, the default-package pair) + `tests/fixtures_sprint22/
  cobertura_kotlin.xml`; `scripts/gen_fixtures_kotlin.py` generates
  them with in-file hand-count assertions (the SPRINT-20 discipline).
- Gates (all PASS, verdicts in `docs/evidence/`):
  - **Language gate Kotlin** `scripts/acceptance/lang_kotlin_gate.py`
    -- 9/9: corpus A parse clean + pack loaded; byte-identical x10;
    contracted rule counts with the EXACT OC006 file set; three import
    edges and NO header_routing block; corpus B resolver v1-kotlin
    exact (9 edges, the decoys unresolved, OC003=1, OC005 empty by
    construction, OC006=5); the Cobertura composition; supply-tier
    honesty; the accessor + main shape pins; the C/C++ freeze legs
    (both prior corpora unchanged with kotlin registered).
  - **Corpus gate Kotlin** `scripts/acceptance/kotlin_corpus_gate.py`
    -- 7/7 on **ktorio/ktor 3.0.1** (the GitHub tag tarball, sha256
    `aa0d7b80...` pinned, corpus digest `ab00070c...`, 1861 .kt + 129
    .kts files, wall ~10.6s): corpus integrity; parse honesty
    (analyzed 2015, **119 parse-error files -- the PINNED raw-source
    boundary, classified**: 42 the function-type-with-receiver shape
    (the pinned grammar's largest gap), 21 Kotlin-1.9+ syntax
    (expect/actual, fun interface, `..<`, delegation-by), 6 the suspend
    function-type shape, 19 the vendored curl cinterop headers (the C
    pack's own pinned boundary), 5 missing-token-only, 1 the gradle-kts
    `the<T>` call, 25 residual; no canonical Kotlin corpus parses 100%
    clean with this pin); determinism x5 byte-identical; resolver
    honesty (**114635 declared-fact edges** -- the wildcard fan-out at
    Kotlin package scale, EVERY target inside the analyzed set; OC006
    == zero(572) - claimed(1, the test example with a main-definition
    shape, measured live)); rule honesty (OC001 115, OC002 36, OC003
    18, OC004 35, OC005 0, OC006 571, OC007/009/010 silent, OC008 628,
    OC011 32; supply = the Java gradle tier only, 124 manifests);
    header-routing honesty (default_c 23 -- ktor's cinterop headers
    route to C, no cpp extension exists); wall ~10.6s < 60s
    single-core.
  - **Language gate C re-proven** 7/7 unchanged; **Corpus gate C
    re-proven** 6/6; **Language gate C++ re-proven** 9/9; **Corpus gate
    C++ re-proven** 7/7 (all four re-run after every late adoption
    change).
- The full suite: **36/36 tests pass** (test_sprint22 wired as the 36th
  suite; the two stale language-count pins in test_cli/test_sprint1
  bumped 12 -> 13 with the sprint note).

## 5. Docs

`docs/LANGUAGES.md` Kotlin section (the full contract: nine sections +
honesty invariants, every shape fact VERIFIED; the adoption-status
table carries the Kotlin row) · `docs/ARCHITECTURE.md`
registered-languages paragraph (the `kotlin` clause; the stale
SPRINT-20 .h boundary note corrected) · `THIRD_PARTY_NOTICES.md`
kotlin table row + provenance note (the canonical community grammar;
the npm cross-check) · README refresh: 12 -> 13 languages/packs, 35 ->
36 tests per cell (headline + matrix + mermaid + cheat sheet), the
first-party LOC table re-derived (**161 files / 51,923 lines**), the
shipped-packs list, the headline numbers. `scripts/audit_docs.py`:
**37/37 items, 100 percent, loc match=True**.

## 6. Environment notes (this sandbox)

- Two sandbox recycles during the sprint; all source edits were lost
  once and recovered by re-applying the session record, re-vendoring
  the grammar (byte-identity re-verified against the npm pin), and
  re-running every gate and suite. The committed tree is the recovered
  state; the lesson is recorded: commit early, commit often --
  the recovery cost was real.
- The pinned corpus tarball (`ktor_3.0.1.tar.gz`) and its extraction
  live UNTRACKED in the repo root (gitignored; the gate hint prints
  the provisioning recipe; the gates never download).
- The vendored `third_party/tree-sitter-kotlin/` is gitignored by
  design and comes back via `scripts/vendor.sh` on the restore path.
