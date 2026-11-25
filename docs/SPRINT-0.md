# Sprint 0 report (executed 2026-11-17)

Scope: plan WBS 1.1-1.2 (repository + build system + sanitizer CI) plus a
deliberate head start on the M1 Walking Skeleton.

## Delivered

| Plan item | Status |
| --- | --- |
| 1.1 repository skeleton | done: layout, MIT license, README, git history |
| 1.2 build system + sanitizer CI | done: CMake presets, strict warnings, ASan/UBSan preset, GitHub Actions workflow |
| 3.1 walking skeleton (head start) | done end-to-end: discovery -> tree-sitter parse -> unified graph -> 2 passes -> deterministic report |
| 6.x OmniDecide integration seam (head start) | done: sidecar protocol v0, client with guardrails, mock provider, 4 failure modes tested |

## Verification

- `ctest --preset default`: 11/11 binaries green (gcc 14.2, `-O3`).
- `ctest --preset sanitize`: 11/11 green under ASan + UBSan + LeakSanitizer.
- Determinism: byte-identical reports across runs (asserted by test_cli).
- Demo: `oc check examples --packs-dir packs` yields 6 stable findings across
  JavaScript and Python.

## Bugs the harness caught during Sprint 0 (kept as evidence)

1. `oc_graph_init` did not initialize the new doc side-table fields; the
   first `oc_graph_doc` call dereferenced stack garbage. Manifested as
   optimization-dependent segfaults/spins (Debug segfault, -O2/-O3 spin,
   ASan clean). Lesson recorded: every struct field added later needs an
   init-site review.
2. The tree-sitter parser stored its language once at backend init, so every
   file after the first pack iteration was parsed with the wrong grammar
   (JavaScript was being parsed as Python). Fixed by selecting the grammar
   per parse call.
3. JSON writer emitted a stray comma between keys and values (comma logic
   shared between keys and values without an "in key" state).
4. LeakSanitizer flagged the pack-set array -- a destructor was missing.
5. The discovery layer filtered by pack extensions, which made the
   `files_skipped_unknown_type` counter structurally always zero. Discovery
   is now pack-agnostic by design.

## Re-verification after workspace restore (2026-11-18)

The working tree was rebuilt from the archived tarball and the full matrix
re-run from scratch on a fresh toolchain (gcc 14.2.0, clang 19.1.7,
CMake 4.4.5):

| Cell | Result |
| --- | --- |
| gcc Release, `-Werror` | 11/11 tests green |
| gcc Debug + ASan/UBSan/LSan, `-Werror` | 11/11 green |
| clang Release, `-Werror` | 11/11 green |
| clang Debug + ASan/UBSan/LSan, `-Werror` | 11/11 green |

Determinism beyond the in-suite assertion: `oc check examples --packs-dir
packs` output is byte-identical across repeated runs and across compilers
(verified with `cmp` between gcc and clang reports).

## Next sprint (WBS 2.x)

1. TypeScript grammar + pack update (2.2), including `@decorator` handling.
2. Import statements in queries + file-to-file IMPORTS edges (3.2 prep).
3. Graph-backed passes: unused-export, cycle detection (T1 lite).
4. CRAP-lite scaffold: per-function complexity tracking (feeds Ch4 CRAP-ms
   flagship once coverage arrives in WBS 5.x).
5. Performance budget run: 10k-file synthetic repo, target < 90 s single-core.

## Gate status

M1 (Walking Skeleton, due 2026-12-31): the skeleton slice already runs
end-to-end ahead of schedule; remaining M1 scope is the resolver + import
edges and the first two graph passes above.
