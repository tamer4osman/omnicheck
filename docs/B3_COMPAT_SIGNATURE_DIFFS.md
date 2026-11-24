# B3 -- compat signature diffs: options package (DECIDED: Option B)

Status: DECIDED 2026-11-22 (first session of the deferred-upgrade
track). Protocol per `OmniCheck_Project_Plan.md` section 9.3 item 4:
present options + evidence, get a decision, then build. Sections 1-6
below are the evidence and options as presented; the decision record
is section 7. Build is authorized as the NEXT session's sprint unit,
not this one. Definitions recovered from plan section 9.3 item 4
(see also `docs/TRACK_B_OWNERSHIP.md`).

## 1. What exists today (evidence)

- `oc compat` v0 (`src/compat.c`, 331 lines; contract in
  `include/oc/compat.h`): pure public-surface diff between two
  analysis roots, both sides analyzed with the SAME pinned grammars.
  BREAKING kinds: `removed-export`, `removed-module`,
  `removed-function`. Additions are informational, never breaking.
  Exit codes: 0 SAFE, 1 BREAKING, 2 tool error. Report
  `omnicheck.compat_report/v0`, sorted (kind, path, symbol) --
  deterministic by construction.
- CLI: `oc compat --baseline DIR --current DIR [--packs-dir DIR]`
  (README "compat" section).
- M4 rehearsal proof (`scripts/acceptance/m4_gate.py` criterion 3,
  evidence `docs/evidence/gate_day/M4_2026-11-19/`): v1 -> v2 is
  BREAKING with exit 1 (removed-export + removed-function
  `going_away`); v1 -> v2safe (additions only) is SAFE with exit 0.
- Fixtures: `tests/fixtures/compat/{v1,v2,v2safe}` -- `pkg.py` +
  `util.js` only (the two launch languages).
- The graph carries NO parameter metadata (verified this session: no
  arity/param fields on `OC_NODE_FUNCTION`; the only "params" hits are
  LSP JSON-RPC message fields). Rust trait methods are already
  captured via `function_signature_item` nodes, so the query layer
  has extraction hooks to build on.

## 2. The gap B3 closes (evidence)

- Descope record: `docs/SPRINT-5.md` line 178 -- "Compat signature
  diffs: no parameter metadata in the graph; removed-symbol detection
  only (recorded post-M4)" -- restated in the compat.h honest-scope
  note.
- Failure class today: `f(a,b)` -> `f(a,b,c)` (arity change),
  `f(a,b)` -> `f(a,b)` with a declared type change, or a formal-param
  reorder `f(a,b)` -> `f(b,a)` -- all verdict SAFE today, because
  only symbol REMOVAL is detected.
- Risk-class tie-in (stated carefully): the M4 mutation run's sole
  survivor is the unchecked arg-swap -- the same risk class
  (parameter order/count sensitivity), different layer (a call-site
  mutant vs a declaration-level diff). B3 adds a deterministic net
  over the declaration layer; it does not change mutation verdicts.
- Language landscape (9 shipped packs, 7 grammar families -- the TS
  family ts-js/tsx/typescript shares one grammar): fully declared
  types (Go, Java, C#, Rust), annotated-when-present (TypeScript,
  Python), untyped in practice (JavaScript, TSX, Ruby). Any signature
  comparison must be honest about which fields are comparable per
  language.

## 3. Options

### Option A -- arity-only, all packs

Add `arity` (parameter count) to function nodes; extraction is
param-list child count in the per-language function queries. New
breaking kind `changed-param-count` with the policy table below.
Catches count changes only; misses type changes and same-arity
reorders/renames. Workload: smallest -- one schema field, 7 grammar
query updates, compat diff + tests + fixtures, M4 criterion
extension, docs. Roughly 0.5-0.7 of a language-sprint unit.

### Option B -- typed-where-declared (RECOMMENDED)

One signature schema: ordered parameter list (name, type) with NULL
types where the language or annotation is absent; per-language fill
rates are reported honestly. New breaking kinds:
`changed-param-count`; `changed-param-type` (fires only when BOTH
sides carry comparable types); `param-reordered` (>= 2 params, types
comparable); `param-renamed` (named-arg-capable languages only, per
the policy table). Report bumps to `omnicheck.compat_report/v1` (v0
retired; consumers are CI scripts, changelog entry included).
Workload: roughly 1.5-2 language-sprint units including per-family
fixtures and the standing 4x31 matrix rerun. Key property: ONE schema
bump, no second pass later -- Option A is a strict subset, so A now
would force a v2 later.

### Option C -- B plus policy knobs and full signature blocks

Everything in B, plus: a per-language compat-policy section in
`pack.json`, a `--compat-policy` CLI override, and report v1 carrying
full old/new signature blocks per symbol. Workload: roughly 2-2.5
units; most CI-flexible, largest tested surface. No demonstrated
consumer for the knobs yet -- the same reasoning that originally
descoped signature diffs applies to the extra surface.

### Option D -- reject B3 for v1.0

Keep v0 as the contract; record "rejected direction: signature diffs"
with the reason (removed-symbol detection covers the CI gate's
demonstrated use; no consumer demand for more). Cheapest; closes the
item permanently; reopenable later as a new decision.

## 4. Policy pre-commitments (required before build, options A-C)

Fixed BEFORE fixtures exist, mirroring the TRIAGE_CALIBRATION
pre-commitment discipline:

- Added param WITH a default (languages with defaults: Python,
  JS/TS family, Ruby, C#) -> informational. Added param WITHOUT a
  default -> BREAKING.
- Languages without defaults (Go, Rust): any arity change BREAKING.
- Java overloads: arity is a property of the signature SET; new
  overloads are additions (informational), removing the last
  overload for an arity is BREAKING.
- Type change with both sides typed -> BREAKING; any NULL on either
  side -> not comparable, not flagged.
- Reorder of >= 2 comparable params -> BREAKING.
- Formal-param rename -> BREAKING only for named-arg-capable
  languages (Python, C#), informational elsewhere.
- Generic types: compare raw source-order type text; no semantic
  resolution, no compiler dependency (determinism).

## 5. Gate and determinism impact (options A-C)

- Report version bump is a compat-artifact change only: the `oc
  check` report digest and the 10k determinism audit are untouched
  (compat is a separate command, not in the check pipeline).
- `m4_gate.py` criterion 3 extends with signature fixtures; the
  formal M4 gate date (2027-09-30) is unchanged; rehearsal evidence
  updates only.
- docs completeness recount, README compat section, CHANGELOG --
  the standing mechanics.
- Matrix rerun 4x31 per the standing per-cell mechanism; sanitizer
  cell included.

## 6. Recommendation

Option B. Reasons: (1) the honest full fix in one schema bump --
A is a strict subset that forces a second report version later;
(2) the register-not-train recipe standardizes per-language
extraction, bounding the marginal cost over A; (3) the pre-committed
policy table keeps verdict logic deterministic and inside the
evidence discipline; (4) C's extra knobs have no demonstrated
consumer yet.

Decision needed: pick A, B, C or D; then schedule the build session
or defer it.

## 7. Decision record (2026-11-22)

The user picked, via the structured decision round closing the first
session of the deferred-upgrade track:

- **Scope: Option B -- typed-where-declared.** One signature schema
  (ordered name, type, NULL-when-absent), type/reorder/rename diffs
  fire only where both sides are comparable, report bumps to
  `omnicheck.compat_report/v1`, section 4's policy table is BINDING
  for the build contract.
- **Timing: build as the next session.** This session records the
  decision only; no implementation lands here. A/C/D are rejected:
  A because it is a strict subset that forces a second report
  version later, C because the knobs have no demonstrated consumer,
  D because the full fix is affordable and closes the gap honestly.
- Build-session shape (preview, binding scope): language-contract
  section first (per repo convention), graph schema + descriptor
  extraction across the 7 grammar families, compat v1 diff kinds +
  policy table, per-family fixtures, unit tests, m4_gate.py
  criterion-3 extension, 4x31 matrix rerun, docs completeness
  recount, CHANGELOG. Sprint record lands as docs/SPRINT-17.md.
