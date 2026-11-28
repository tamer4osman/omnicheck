# T1–T7: OmniDecide integration features — reconstructed spec

Status: requirements reconstruction, 2026-11-28.
Implementation status in this dev tree: **NOT STARTED** (verified 2026-11-28:
no `--triage-*` flags, no `brain`/`pin`/`diff` commands, no feature headers in
`src/` or `include/oc/`).

## Where this spec comes from

1. **Authoritative**: the agreed plan, quoted verbatim by the product owner
   in session (the "What was built (T1–T7, in the agreed order)" list).
2. Kernel contract docs in this tree: `third_party/omnidecide/docs/`
   (`EXPLAIN.md`, `REPLAY.md`, `SPINE-API.md`, `MODEL.md`, `LEARNING.md`,
   `FEATURES.md`).
3. `docs/SPRINT-8.md` — L-track adoption record (explain sidecar shipped,
   replay ritual, sidecar precedence).
4. Reference code only: an earlier partial generation survives in
   `omnicheck-v0.3.0-github-ready.tar.gz` (`src/brain.c`, `explain.c`,
   `lexicon.c`, `reportdiff.c`, `sha256.c`, `spine.c` + tests). It diverges
   from this spec (see divergence log) and its tree fails 31/44 of its own
   tests (tree-sitter stripped) — it is a code reference, NOT evidence of
   done work.

## Agreed build order

T2 → T1 → T3 → T4 → T5 → T6 → T7

## Global contract (applies to all seven)

* **Byte-identical protocol-v0**: triage responses must produce
  byte-identical report bytes whether the provider is embedded or the
  sidecar. Embedded and sidecar paths share ONE response-application code
  path.
* **Sidecar absolute precedence**: when `OC_OMNIDECIDE_BIN` is set, the
  sidecar wins over the embedded kernel (SPRINT-8 L-record;
  `include/oc/triage_embed.h` in the reference tree).
* **Fail loud, never fail open** for resource rejections (T5, T7); fail-open
  is allowed only where the spec says so (T1 explain is advisory).
* **The kernel never decides identity**: introduced/resolved/persistent is
  decided by finding fingerprints (T3).
* **Rank arithmetic untouched**: explain never changes ranking (T1).
* **Kernel is stateless and deterministic**; the brain is opt-in pure-data
  memory with no randomness and no wall clock (`LEARNING.md`).
* **Persistent-store writes reuse the H-4 crash-safety discipline**
  (fsync'd journal, atomic temp+rename, permission-bit preservation).

## T2 — OC_EMBED_OMNIDECIDE (first)

* CMake option `OC_EMBED_OMNIDECIDE`, **default ON**, links the vendored
  kernel in-process (`third_party/omnidecide`).
* New flag `--triage-embedded`: runs the embedded kernel behind the
  byte-identical protocol-v0 contract.
* Sidecar precedence preserved (see global contract).
* Tests: response equality against the real provider binary; report bytes
  identical embedded vs sidecar.
* Reference: `triage_embed.h`, `src/spine.c`, `tests/test_triage_embed.c`
  in the tarball generation (default OFF there — must be ON here).

## T1 — `--triage-explain` (9 kernel rationale channels)

* Attaches per-finding rationale from the kernel explain API
  (`docs/EXPLAIN.md`), exactly 9 channels per option:
  `tf_cosine`, `jaccard`, `trigram`, `whole`, `best_chunk`, `contextual`,
  `directional`, `fused`, `probability`.
* Fail-open: an explain failure must not fail the scan; advisory only.
* Rank arithmetic untouched.
* Acceptance from the plan: verified on 308/308 real findings.
* Reference: `tools/omnidecide/oc_odk_explain.c` (sidecar already in this
  tree), `docs/EXPLAIN.md`.

## T3 — `oc diff` (introduced / resolved / persistent) + `--semantic`

* Classifies findings between two reports by fingerprint; the kernel never
  decides identity.
* `--semantic` adds advisory scores (clearly marked advisory in output).
* Exit code gates on introduced findings (exit 1 when any introduced), per
  the CLI exit contract {0,1,2}.
* Reference: `src/reportdiff.h/.c` + `tests/test_reportdiff.c` in the
  tarball generation (plain `diff` command there, no `--semantic` flag —
  must be extended to the spec).

## T4 — `oc brain` + `--triage-brain`

* Subcommands: `create`, `record`, `advise`, `info`, `entries`, `discard`,
  `reset`, `save` (learning loop per `docs/LEARNING.md`: evaluate → record
  reward in [-1, 1]).
* `--triage-brain` applies the brain store during triage.
* Every report records: store path + sha256 + epoch.
* Atomic writes reuse the H-4 discipline (global contract).
* Reference: `src/brain.h/.c` + `tests/test_brain.c` (flags there
  `--brain`/`--brain-file` — rename to the spec).

## T5 — `oc pin create` / `oc pin replay` (REPLAY.md semantics)

* Implements `third_party/omnidecide/docs/REPLAY.md`: pin record carries
  `schema_fp`, `kernel_version`, `brain_snapshot_id`, `context_hash`,
  `inputs_ref`, `request_json` (byte-exact request document), `decision`,
  `confidence`, `tier`, `replay_class`, `timestamp`, `registrations`.
* Replay classes: `exact` (bit-level identity required), `semantic` (same
  decision, bytes may differ), `foreign` (lineage only, not re-derived).
* Acceptance from the plan: a real pin replays exact; a tampered decision
  exits 1; a schema mismatch refuses loud
  (`registrations_match: false`, exit 1).
* Two replays of the same pin produce byte-identical reports.
* Reference: tarball `src/cli/main.c` `pin` command + `src/sha256.c`.

## T6 — `oc check --format llm`

* Emits the kernel-minified Markdown report with token estimates.
* Deterministic bytes; same findings as `--format json`.
* Reference: tarball `src/cli/main.c` (`format` `"llm"`) +
  `tests/test_llm.c`.

## T7 — `--triage-lexicon` / `--triage-model`

* Loads lexicon and scoring-model resources with their sha256 recorded in
  the report; rejected resources fail loud with the kernel's reason.
* Model file format per `docs/MODEL.md` (v1: 128-byte little-endian
  `ODKMODL1`; v2 adds feature weights). Without a model the kernel uses
  built-in constants — output byte-identical to a build without model
  support.
* Reference: `src/lexicon.h/.c` + `tests/test_lexicon.c` (flags there
  `--lexicons`/`--lexicon-file`/`--model` — rename to the spec).

## Acceptance for the whole set

1. Full `ctest` green, including the new suites
   (`test_triage_embed`, `test_brain`, `test_lexicon`, `test_llm`,
   `test_reportdiff`, `test_spine`).
2. Byte-stability sweep unaffected when features are off/at defaults.
3. T1 acceptance: 308/308 real findings explained.
4. T5 acceptance: exact replay byte-identical; tamper exits 1; schema
   mismatch refuses loud.
5. Repackage both tarballs (binary + source), regenerate the sha256
   manifest, document the features in README.md.
6. Commit the whole set to git.

## Divergence log (tarball generation vs this spec)

| Aspect | Tarball generation | This spec |
| --- | --- | --- |
| Flags | `--brain`, `--brain-file`, `--lexicons`, `--lexicon-file`, `--model`, `--pin`, `--triage` | `--triage-embedded`, `--triage-explain`, `--triage-brain`, `--triage-lexicon`, `--triage-model`, `--semantic` |
| `OC_EMBED_OMNIDECIDE` default | OFF | ON |
| `--semantic` on diff | absent | required |
| REPLAY.md semantics doc | not referenced | normative for T5 |
| Tree-sitter | stripped (31/44 tests fail) | full vendored grammars |
