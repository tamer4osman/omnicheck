# OmniCheck Security Fixes

This tree carries the security remediation of OmniCheck v0.3.0 (commit
`51d4e49`) produced by the dedicated security assessment. All five High
findings (H-1 ... H-5) are fixed, plus two Medium-hygiene defects found
while patching. Every fix keeps the project contract: **fail loud, never
fail open; byte-identical output on legitimate input.**

## Fixed findings

| ID | Severity | File | Defect | Fix |
|----|----------|------|--------|-----|
| H-1 | High (CVSS 7.5) | `src/json.c` | The JSON parser reallocated and copied its key/value/item vectors on **every** append inside an arena that never reclaims: a 30,000-field `package.json` costed O(n^2) bytes (measured 3,626 MiB RSS -> OOM). | Geometric doubling (`grow_ptr_vec`, start 16) with a hard 1,000,000 field/item budget (`OC_JSON_MAX_ENTRIES`) that fails the parse loudly. |
| H-2 | High (CVSS 7.5) | `src/parser_ts.c` | `rust_use_expand` recursed once per use-tree nesting level with no depth limit; a 500 KB Rust file with a 100,000-deep `use` tree blew the C stack (SIGSEGV, ASan-confirmed). | Explicit depth budget `OC_RUST_USE_MAX_DEPTH 256`; exceeding it aborts the parse loudly (`OC_ERR_PARSE`). |
| H-3 | High (CVSS 7.5) | `src/coverage.c` | The Go coverprofile loader derived a malloc size from the record's end line; a 47-byte input claiming `1.1,1000000000.0` requested ~4 GB (measured 3,615 MiB -> SIGKILL). | `OC_COV_MAX_LINE 10,000,000` sanity bound; hostile records fail the load loudly (`OC_ERR_USAGE`), matching the OpenCover loader's existing bound. |
| H-4 | High (CVSS 8.1) | `src/mutate.c` | The mutation engine spliced mutants straight into working-tree sources (`fopen "wb"`) and restored only on the happy path: SIGKILL/SIGINT/CI-timeout during a run **permanently corrupted the user's source file** (`a == b` stayed `return 0`). | Four crash-safety layers: (1) fsync'd journal `<file>.oc-mutate-journal` armed before the first splice, carrying the pristine permission bits; (2) atomic temp-file+rename writes with the target's mode preserved via `fchmod` (a plain `rename` from `fopen("wb")` would silently drop the exec bit -- caught by `test_sprint5`'s in-source mutation of an executable fixture) plus `fsync` before rename; (3) SIGINT/SIGTERM/SIGHUP/SIGQUIT handlers that restore the journal (async-signal-safe) and exit 128+sig, killing the suite's process group; (4) startup recovery that restores pristine bytes AND mode from a journal left by a dead run before the pre-flight suite runs. |
| H-5 | High (CVSS 7.5) | `src/parser_ts.c` | Adversarially deep syntax trees made the vendored tree-sitter query engine quadratic: a 1 MB nested-call JS chain hung >150 s (gprof: 95 % in `ts_query_cursor__advance`). | One linear arena-stack DFS (`ts_max_depth`) measures tree depth before any cursor runs; deeper than `OC_TS_MAX_TREE_DEPTH 8192` the file is refused loudly before the quadratic cost is paid. Legitimate code sits orders of magnitude below (deepest pinned fixture ~2k levels). |
| M-10 | Medium (hygiene) | `src/coverage.c`, `src/dup.c` | `qsort(NULL, 0, ...)` on empty file sets is undefined behaviour (found while patching H-3). | NULL/empty guards around the three affected `qsort` call sites. |
| T16 | Medium (hygiene) | `src/coverage.c` | Same `qsort` UB in the second loader path (`oc_coverage_load`). | Same guard. |

## Verification evidence

Adversarial fixtures from the assessment report, re-run after the fix
(harness: `patches/kill_highs_verification_harness.py`, RLIMIT_AS
2,500 MiB; ASan build run without the cap):

| Fixture | Before | After (Release) | After (ASan) |
|---------|--------|-----------------|--------------|
| T1: 30,000-dep `package.json` | 3,626 MiB -> OOM | **47 MiB / 0.6 s / exit 1** | 419 MiB / 5.2 s / exit 1 |
| T5: 47-byte Go coverprofile bomb | 3,615 MiB -> SIGKILL | **46 MiB / 0.6 s / exit 2 (loud)** | 420 MiB / 5.2 s / exit 2 |
| T6b: 200,000-deep nested calls | >150 s hang | **1.1 s / exit 2 (loud)** | 11.9 s / exit 2 |
| T6c: 20,000-deep nested arrows | hang | **0.7 s / exit 2 (loud)** | 6.6 s / exit 2 |
| T7: 100,000-deep Rust use-tree | SIGSEGV at ~1.6 GB | **101 MiB / 0.8 s / exit 2 (loud)** | 506 MiB / 7.9 s / exit 2 |
| T8: SIGKILL + SIGINT during mutate | source file permanently corrupted | journal left -> next run recovers pristine bytes; SIGINT restores instantly, exit 130 | same, full pass |

* Both presets build clean: `cmake --preset default` (Release) and
  `cmake --preset sanitize` (ASan + UBSan). Sanitizers report **zero**
  errors on the hot paths (T1, T7) and the full PoC suite passes 6/6 on
  both binaries.
* `ctest`: **39/39 pass** on both presets -- zero regressions.
* **Byte-stability sweep**: `oc check` output is byte-identical (and
  exit-code-identical) between the pristine v0.3.0 binary and the fixed
  binary across 16 corpora (examples + the C, C++, C#, Go, Java, JS,
  Kotlin, PHP, Python, Ruby, Rust, Scala, Swift, TS, TSX fixture trees).
  Coverage composition (`istanbul/v8/pycov`) likewise identical.

## Behavioural notes

* Deep/hostile inputs now fail with a specific, actionable message and
  exit code 2 (usage/parse error) instead of crashing or hanging -- the
  same loud-failure contract the rest of the engine already follows.
* `oc mutate` creates two transient files next to a target while a
  mutant is applied: `<file>.oc-mutate-journal` (pristine bytes +
  pristine permission bits) and `<file>.oc-mutate-tmp` (atomic-replace
  staging). Both are removed on every normal path; a journal that
  survives a crash is consumed by the next run's startup recovery.
* Splice/restore/journal-recovery all preserve the target file's
  permission bits (verified: an executable fixture stays 0755 through
  the full 39-test suite, which mutates it in source).
* The JSON parser accepts up to 1,000,000 object fields + array items
  per document; beyond that it fails with
  `document exceeds the 1,000,000 field/item budget`.
