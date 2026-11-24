# OmniDecide-C vendored tree — provenance and re-audit policy

Vendored: 2026-11-20, decision L8 of docs/OMNIDECIDE_LEVERAGE.md
(vendor-vs-pin resolved as **vendor**).

| Field | Value |
| --- | --- |
| Upstream | https://github.com/tamer4osman/omnidecide-c |
| Version | v2.0.0 |
| Upstream commit | 561fdb8ae43a4610d3df92cde5ba0a0ac5f63393 |
| Source archive | omnidecide-c-main.zip (GitHub codeload snapshot of `main`) |
| Archive sha256 | cd12263c9e6d9654b519dc7ac330a00d40affa58be6c013a2c70bf4fd59648fd |
| License | Apache-2.0 (LICENSE, NOTICE, THIRD_PARTY_NOTICES.md vendored unmodified) |
| Validated | 583/583 kernel checks green from the archive tree (smoke_test 373 + spine_test 210), twice, 2026-11-19/20 |

## What is vendored (and what is not)

Included: `odk-core/`, `odk-brain/`, `odk-spine/`, `include/`, `schema/`,
`tests/`, `examples/`, `docs/`, and the top-level build/license files
(`CMakeLists.txt`, `Makefile`, `LICENSE`, `NOTICE`, `README.md`,
`CHANGELOG.md`, `THIRD_PARTY_NOTICES.md`, `SECURITY.md`, `CONTRIBUTING.md`).

Excluded, with reasons:

- `build/` — build artifacts, never vendored.
- `golden/` (12 MB) — golden replay corpus consumed only by
  `tests/golden_tool.c` / `tests/golden_spine.c`, which are excluded with
  it. The replay audit is an upstream-release ritual, not an in-repo build
  step (see re-audit policy).
- `e2e/` — Playwright web demo, unrelated to the library.
- `.git*`, `.github` — upstream CI/SCM metadata.

`VENDOR.sha256` pins every included file. Do NOT edit any file here;
OmniCheck integrates the tree unmodified (see `third_party/../CMakeLists.txt`
at the repo root). Kernel upgrades are manual vendor steps: replace the
tree, update `PROVENANCE.md` + `VENDOR.sha256` + `THIRD_PARTY_NOTICES.md`,
re-run the full check battery (below), and record the evidence.

## Re-audit policy (per upstream release)

1. **In-repo (every build, every gate):** the vendored sources build as
   CMake subdirectory target `omnidecide`; `odk_smoke_test` (373 checks)
   and `odk_spine_test` (210 checks) run as ctest with working directory
   at the vendored root (fixture paths are root-relative by contract);
   `test_triage_real` proves the real provider end to end.
2. **Full replay audit (per upstream release, scratch tree):** unpack the
   pinned archive, run the kernel's own complete battery INCLUDING the
   golden replay suite from the unpacked root, and compare the archive
   sha256 against this file. Command sketch (outside the OmniCheck tree):

       sha256sum omnidecide-c-main.zip   # must equal the sha256 above
       unzip omnidecide-c-main.zip && cd omnidecide-c-main
       make test && make -C . spine_test && ./build/spine_test
       # + golden replay suite per docs/GOLDEN.md in the archive tree

3. **Integrity in-repo:** `scripts/vendor.sh` verifies `VENDOR.sha256`
   against the working tree, so accidental edits to the vendored sources
   are caught at contributor setup time.

## Determinism note

The kernel is stateless by construction (the adaptive brain is opt-in and
never enabled by the OmniCheck provider — the provider passes a fixed
`ODK_LANG_EN` config and never touches `odk_brain`), and the vendored build
keeps upstream's `-ffp-contract=off`, the flag that makes kernel results
bit-identical across compilers and optimization levels. This is the
precondition for OmniCheck's byte-determinism guarantee with triage enabled.
