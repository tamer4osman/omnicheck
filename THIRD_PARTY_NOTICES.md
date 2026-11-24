# Third-party notices

All vendored components are used as unmodified sources at pinned refs,
fetched by `scripts/vendor.sh` into `third_party/`.

| Component | Version / commit | License | Upstream |
| --- | --- | --- | --- |
| tree-sitter runtime | v0.27.2 (3fb7be49145acfb07e33b61ed5689e2b6771b2e9) | MIT | https://github.com/tree-sitter/tree-sitter |
| tree-sitter-javascript grammar | v0.25.0 (44c892e0be055ac465d5eeddae6d3e194424e7de) | MIT | https://github.com/tree-sitter/tree-sitter-javascript |
| tree-sitter-python grammar | v0.25.0 (293fdc02038ee2bf0e2e206711b69c90ac0d413f) | MIT | https://github.com/tree-sitter/tree-sitter-python |
| tree-sitter-typescript grammar (typescript + tsx) | v0.23.2 | MIT | https://github.com/tree-sitter/tree-sitter-typescript |
| tree-sitter-go grammar | v0.25.0 | MIT | https://github.com/tree-sitter-grammars/tree-sitter-go |
| tree-sitter-java grammar | v0.23.5 (94703d5a6bed02b98e438d7cad1136c01a60ba2c) | MIT | https://github.com/tree-sitter/tree-sitter-java |
| tree-sitter-c-sharp grammar | v0.24.0 | MIT | https://github.com/tree-sitter-grammars/tree-sitter-c-sharp |
| tree-sitter-rust grammar | v0.24.0 | MIT | https://github.com/tree-sitter/tree-sitter-rust |
| tree-sitter-ruby grammar | v0.23.1 | MIT | https://github.com/tree-sitter/tree-sitter-ruby |
| tree-sitter-php grammar | v0.24.2 | MIT | https://github.com/tree-sitter/tree-sitter-php |
| tree-sitter-c grammar | v0.23.4 | MIT | https://github.com/tree-sitter/tree-sitter-c |
| tree-sitter-cpp grammar | v0.23.4 | MIT | https://github.com/tree-sitter/tree-sitter-cpp |
| tree-sitter-kotlin grammar | 0.3.8 | MIT | https://github.com/fwcd/tree-sitter-kotlin |

tree-sitter runtime note: the library is compiled as the single-TU amalgam
(`lib/src/lib.c`) with `_DEFAULT_SOURCE` for POSIX extensions.

tree-sitter-go provenance note (SPRINT-9, 2026-11-20): the adoption
environment could not reach `github.com` over the git protocol for new
repositories (clone, codeload and CDN mirrors all blocked beyond the
already-vendored repos), so the vendored content was taken from the PyPI
sdist of the pinned version -- `tree_sitter_go-0.25.0.tar.gz`, sha256
`a7466e9b8d94dda94cae8d91629f26edb2d26166fd454d4831c3bf6dfa2e8d68` -- which
packages the upstream `tree-sitter-grammars/tree-sitter-go` tag `v0.25.0`
sources unmodified (`src/parser.c` + `src/tree_sitter/` headers; the
grammar ships no external scanner). `scripts/vendor.sh` carries a codeload
tarball fallback so a normal environment re-vendors the same pinned tag
directly from upstream. The upstream commit SHA for the tag is not
recorded here because the adoption environment could not query it; the
tag itself is the pin. Never edited after vendoring.

tree-sitter-java provenance note (SPRINT-11, 2026-11-20): the adoption
environment could reach upstream again, and the grammar lives in the MAIN
`tree-sitter` org (like javascript/python/typescript), not the grammars org
named in the pre-adoption contract -- verified by probing the codeload
endpoints (tree-sitter-grammars/tree-sitter-java does not exist). Vendored
from the codeload tarball of the pinned tag
`https://codeload.github.com/tree-sitter/tree-sitter-java/tar.gz/refs/tags/v0.23.5`
(sha256 `cb199e0faae4b2c08425f88cbb51c1a9319612e7b96315a174a624db9bf3d9f0`),
content-identical to the tag checkout; tag commit
`94703d5a6bed02b98e438d7cad1136c01a60ba2c` (lightweight tag, verified with
`git ls-remote`). Vendored shape mirrors tree-sitter-go: `src/parser.c` +
`src/tree_sitter/parser.h` (+ the MIT `LICENSE`); the grammar ships NO
external scanner (verified against the pinned tag's src/ listing).
`scripts/vendor.sh` vendors the same pinned ref. Never edited after
vendoring.

tree-sitter-c-sharp provenance note (SPRINT-13, 2026-11-20): same
adoption environment, same git-protocol restriction for new
repositories. The vendored content came from the npm package tarball of
the pinned version (`tree-sitter-c-sharp-0.24.0.tgz`, sha256
`8077a5ccc21f071ee92444dce678bd9a1f2908e5d4c4bd3d63f395c5316f81fe`),
which packages the upstream `tree-sitter-grammars/tree-sitter-c-sharp`
tag `v0.24.0` sources unmodified. Cross-provenance check recorded: the
PyPI sdist of the same version (`tree_sitter_c_sharp-0.24.0.tar.gz`,
sha256 `cf51785f8558c5d669bf326012a0bfb1bb75d4d273ab5352b5cd9c633de5e436`)
ships a `src/parser.c` byte-identical to the npm package's
(sha256 `96ed4b0b304899bf8f9135d789ba4064451d3adb114fa3e6ff50f9398e689c45`
in both); the npm package additionally carries `src/scanner.c` (sha256
`73fa56cec9d323a51636f02b6672848f6a301a1523d9e8b91a965792273abf33`) --
the grammar is the first vendored one WITH an external scanner
(interpolated strings, optional semicolons), compiled by the
`oc_add_grammar` scanner branch -- and its own `src/tree_sitter/`
header copy. LANGUAGE_VERSION 15. Never edited after vendoring.


tree-sitter-rust provenance note (SPRINT-15, 2026-11-21): vendored at
`third_party/tree-sitter-rust/` from the npm binding package tarball of
the pinned version (`tree-sitter-rust-0.24.0.tgz`, sha256
`4248da0b9ea40fec13ef010822e7b92a9d1ebdf74f6f2733539b1dbbb086c5bb`,
recorded in `scripts/vendor.sh`), which packages the upstream
`tree-sitter/tree-sitter-rust` v0.24.0 tag (the MAIN tree-sitter org --
probed: the grammars org has no rust repo). The npm `src/parser.c` is
byte-identical to the codeload tarball of the same tag (sha256
`a0f424079fd2ba8fe6ae29538e4a773e43c7a1271e6962eb6e8185f67a5c0db9` in
both) and the npm `src/scanner.c` likewise (sha256
`77faa4943da96dcb17d9c2a735bdbf7702287b52a1d2971117b167e47e318620`) --
the second vendored grammar WITH an external scanner, compiled by the
`oc_add_grammar` scanner branch. LANGUAGE_VERSION 15 (verified: the
vendored runtime v0.27.2 accepts it). Never edited after vendoring.


tree-sitter-ruby provenance note (SPRINT-16, 2026-11-22): vendored at
`third_party/tree-sitter-ruby/` from the npm binding package tarball of
the pinned version (`tree-sitter-ruby-0.23.1.tgz`, sha256
`1f0f0c100e31ddf650962ebb1f1fb8420bdbabccd49582f6e731ea7478db639d`,
recorded in `scripts/vendor.sh`), which packages the upstream
`tree-sitter/tree-sitter-ruby` v0.23.1 tag (the MAIN tree-sitter org;
the grammar pre-dates the grammars-org migration and has always lived
there). The npm `src/parser.c` is byte-identical to the codeload tarball
of the same tag (sha256
`4ce468358b6f4e25a35c8cf6bc0eaf60665bc22d602f8c939323c2347255cd15` in
both) and the npm `src/scanner.c` likewise (sha256
`e7a6196d6e78bf4c6728502e924c867dee5d851c6253e43fcdb8ba169009bc58`) --
the THIRD vendored grammar WITH an external scanner (heredocs, delimited
literals), compiled by the `oc_add_grammar` scanner branch.
LANGUAGE_VERSION 14 (verified: the vendored runtime v0.27.2 accepts it).
Never edited after vendoring.


tree-sitter-cpp provenance note (SPRINT-21, 2026-11-23): vendored at
`third_party/tree-sitter-cpp/` from the pinned v0.23.4 git tag of the
MAIN tree-sitter org (the same version number as the C pin), with the
npm binding package tarball of the same version
(`tree-sitter-cpp-0.23.4.tgz`, sha256
`615d152f70e7a6bed8f15a93c3c0c264c40dc71c1b6a1445646fff9439e776bc`,
recorded in `scripts/vendor.sh` as the sha256-verified final fallback)
cross-checked BYTE-IDENTICAL against the tag checkout for
`src/parser.c`, `src/scanner.c`, `src/node-types.json`, `grammar.js` and
`LICENSE` at adoption. The grammar ships an EXTERNAL scanner
(`src/scanner.c` -- raw strings, angle-bracket disambiguation), compiled
by the `oc_add_grammar` scanner branch (the fifth vendored grammar with
one). The FULL C grammar is inlined at generation time (`grammar.js`
requires `tree-sitter-c/grammar`), so the generated parser.c is
self-contained. LANGUAGE_VERSION 14, accepted by the vendored runtime
v0.27.2. Never edited after vendoring.

tree-sitter-kotlin provenance note (SPRINT-22, 2026-11-23): the vendored
grammar is the canonical community Kotlin grammar -- the MAIN tree-sitter
org ships no Kotlin grammar -- pinned at the fwcd/tree-sitter-kotlin tag
`0.3.8` (the tag carries NO v prefix). The npm binding package of the SAME
version (fwcd's own `tree-sitter-kotlin`, registry.npmjs.org,
`tree-sitter-kotlin-0.3.8.tgz`, sha256
`e2da896c8316af1d491afc69b49173da97373bb757d07f11c455a0963c62e6b0`) is the
sha256-verified alternate provenance: its `src/parser.c`, `src/scanner.c`,
`src/node-types.json`, `src/grammar.json` and `LICENSE` were verified
byte-identical to the git-tag checkout at adoption (the SPRINT-13
cross-check discipline). LANGUAGE_VERSION 14, accepted by the vendored
runtime v0.27.2. MIT licence (the upstream LICENSE, vendored
unmodified under `third_party/tree-sitter-kotlin/`).

npm-tarball alternate provenance (SPRINT-13, 2026-11-20): the SPRINT-13
m1-replay drift leg caught the environment's github codeload endpoint
throttled for the grammars-org repos (tree-sitter-go, tree-sitter-c-sharp) --
neither git clone nor codeload was reachable for new fetches. Every grammar
now carries a FINAL fallback in `scripts/vendor.sh` to the npm binding
package tarball of the pinned version, sha256-verified before extraction;
each npm parser.c was byte-verified identical to the vendored tree at
adoption. The npm sha256 pins live in `scripts/vendor.sh` (javascript,
python, typescript, go, java, c-sharp). The tree-sitter RUNTIME keeps
codeload as its only fallback: no alternate pinned provenance exists at
v0.27.2 (recorded honestly in the script).
## Vendored: OmniDecide-C (triage provider, vendor decision L8)

tree-sitter-php provenance note (SPRINT-19, 2026-11-22): vendored at
`third_party/tree-sitter-php/` from the npm binding package tarball of
the pinned version (`tree-sitter-php-0.24.2.tgz`, sha256
`fdd22530d3845540ba74a1199d3f67204a608c99994bb423645d3f0cfdaead94`,
recorded in `scripts/vendor.sh`), which packages the upstream
`tree-sitter/tree-sitter-php` v0.24.2 tag (the MAIN tree-sitter org).
The npm `php/src/parser.c`, `php/src/scanner.c`, `php/src/node-types.json`,
`php_only/src/parser.c` and `common/scanner.h` are byte-identical to the
codeload tarball of the same tag (SPRINT-19 cross-check, the Ruby/Rust
provenance discipline). LANGUAGE_VERSION 15, accepted by the vendored
runtime v0.27.2. The v0.22+ repo layout keeps `php/`, `php_only/` and
`common/` subdirectories; OmniCheck compiles ONLY `php/src/parser.c` +
`php/src/scanner.c` -- the MIXED php+html grammar -- and leaves
`php_only/` as uncompiled vendored bytes. The scanner's shared header
includes RELATIVELY (`../../common/scanner.h` from `php/src/`), so no
extra include dir is needed (proven by the SPRINT-19 adoption build).
Never edited after vendoring.

tree-sitter-c provenance note (SPRINT-20, 2026-11-22): vendored at
`third_party/tree-sitter-c/` from the pinned v0.23.4 git tag of the MAIN
tree-sitter org, with the npm binding package tarball of the same
version (`tree-sitter-c-0.23.4.tgz`, sha256
`31f07c7c29748a2cf93ac1e0e1363324689f5ebf39c5b012081cb677a1392f5a`,
recorded in `scripts/vendor.sh` as the sha256-verified final fallback)
cross-checked BYTE-IDENTICAL against the tag checkout for
`src/parser.c`, `src/node-types.json`, `grammar.js` and `LICENSE` at
adoption. The grammar ships NO external scanner (src/ holds parser.c,
grammar.json, node-types.json and the tree_sitter/ headers only), so
`oc_add_grammar` compiles `src/parser.c` alone. LANGUAGE_VERSION 15,
accepted by the vendored runtime v0.27.2. Never edited after vendoring.


| Component | Version / commit | License | Upstream |
| --- | --- | --- | --- |
| OmniDecide-C | v2.0.0 (561fdb8ae43a4610d3df92cde5ba0a0ac5f63393) | Apache-2.0 | https://github.com/tamer4osman/omnidecide-c |

Vendored 2026-11-20 under `third_party/omnidecide/` (committed in-repo,
unmodified sources; integrity-pinned by `VENDOR.sha256`, verified by
`scripts/vendor.sh`). Source archive `omnidecide-c-main.zip`, sha256
`cd12263c9e6d9654b519dc7ac330a00d40affa58be6c013a2c70bf4fd59648fd`.
Provenance, exclusion list and per-release re-audit policy:
`third_party/omnidecide/PROVENANCE.md`. Validated before vendoring: all
583 kernel checks green from the archive tree (smoke_test 373 +
spine_test 210, run twice, 2026-11-19/20), and the vendored build
reproduces them in-repo in every cell plus 583 checks under
ASan+UBSan+LSan. The provider shim (`tools/omnidecide/oc_odk_provider.c`)
is the only first-party binary linked against the kernel; the runtime
seam stays the spawn boundary (`$OC_OMNIDECIDE_BIN`, opt-in `--triage`,
fail-open). Upstream upgrades are manual vendor steps per PROVENANCE.md.

### tree-sitter-swift (SPRINT-23, wave-3 continues)

- Upstream: https://github.com/alex-pinkus/tree-sitter-swift
- Pinned ref: `0.7.1-with-generated-files` (tag, no v prefix) -- the
  generated parser files ship on the dedicated tag; the plain `0.7.1`
  tag carries the grammar sources only.
- License: MIT (c) 2021 alex-pinkus (vendored `LICENSE`).
- Canonicality note: the tree-sitter ORG's `tree-sitter/tree-sitter-swift`
  is an ABANDONED UNRELATED project (Rob Rix, v0.0.1; its README says
  "Status - Abandoned. Use alex-pinkus/experimental-tree-sitter-swift
  instead", which redirects to alex-pinkus/tree-sitter-swift). Every
  npm `tree-sitter-swift` version publishes FROM alex-pinkus's repo
  (the npm `repository` field pins it); the npm latest 0.7.1 publishes
  from the `0.7.1-with-generated-files` tag.
- Dual-source byte-identity (verified at adoption, sha256 both sides):
  `src/parser.c` `6755b167...`, `src/scanner.c` `a30d8d36...`,
  `src/node-types.json` `17755276...`, `src/grammar.json`
  `6e2a8176...`, `LICENSE` `3533cec1...` -- byte-equal between the git
  tag tarball and the npm `tree-sitter-swift@0.7.1` tarball
  (registry.npmjs.org; the npm tarball's own sha256
  `abd89284f1b0f1375ba5efaa9ddf99ad4b16ac91fc7751ef10c5f013cca06f8c`
  is recorded in `scripts/vendor.sh` as the fallback pin).
- LANGUAGE_VERSION 14 (the generated parser); the alternative-ABI
  emissions (`src/parser_abi13.c`, `src/parser_abi14.c`) stay vendored
  bytes, uncompiled. The external scanner (`src/scanner.c`, 33
  external tokens) is picked up by `oc_add_grammar` automatically.
- The npm-tarball fallback in `scripts/vendor.sh` needs the PLAIN
  npm version (`0.7.1`), passed as the explicit 6th fetch argument
  (the tag and the npm version differ for this grammar).
- KNOWN UPSTREAM DEFECT at the pin (recorded SPRINT-23, 2026-11-24,
  found by the sanitize leg): the external scanner allocates its state
  with `calloc(0, sizeof(struct ScannerState))`
  (`src/scanner.c:237`, a zero-byte heap region) and
  `tree_sitter_swift_external_scanner_serialize` /
  `_deserialize` then read and write the 4-byte state word out of
  bounds (`src/scanner.c:251` / the `length == 4` branch). Under ASan
  this is a hard heap-buffer-overflow abort on the first Swift parse.
  Upstream master fixed it (`calloc(1, ...)` plus an OOM guard), but
  no npm twin exists to re-prove the dual-source pin against (npm
  latest is still 0.7.1, published 2025-06-23), so the pin stays and
  the vendored bytes stay UNMODIFIED. Treatment: the project-owned
  build glue (`third_party/CMakeLists.txt`) excludes ONLY
  `tree-sitter-swift/src/scanner.c` from ASan instrumentation under
  the sanitize preset; first-party code and every
  other vendored scanner remain fully instrumented. Revisit on the
  next tagged release that carries the upstream fix.
- SECOND UPSTREAM DEFECT at the pin (same sanitize leg): the
  operator-suppressor mask loop shifts `1 << suppressor` as a signed
  int (`src/scanner.c:514`) — at shift 31 the shift is UB and the mask
  word is SIGN-EXTENDED into the uint64_t test, so the token-suppressor
  set differs from the upstream-correct spelling; under the sanitize
  build this flips a real fixture parse from clean to `had_error` (a
  behavioral divergence, not just a report). Upstream master uses
  `1ULL << suppressor`. Same treatment: the carve-out also drops UBSan
  for this one translation unit; vendored bytes unmodified; revisit on
  the next tagged release.

### tree-sitter-kotlin (SPRINT-22, wave-3 opens) -- sanitizer note

- KNOWN UPSTREAM SHAPE at the pin (classified SPRINT-23, 2026-11-24,
  first surfaced by the sanitize leg):
  `tree_sitter_kotlin_external_scanner_serialize`
  (`src/scanner.c:517`) calls `memcpy(buffer, stack->contents,
  stack->size)` with `contents == NULL` when the stack is empty --
  `memcpy(dst, NULL, 0)`, a nonnull-attribute UBSan warning (a
  recoverable runtime warning under the project's UBSan build, not an
  abort; no bytes are copied; no memory-safety impact). The allocation
  itself is correct (`ts_calloc(1, sizeof(Stack))`). Suites pass; the
  warning is the documented upstream shape at the pinned 0.3.8 tag.
  Revisit on the next tagged release.

### tree-sitter-scala (SPRINT-24, wave-3 continues)

- Vendored from the pinned **v0.23.4** tag of
  tree-sitter/tree-sitter-scala (the MAIN tree-sitter org's grammar),
  with the npm binding package `tree-sitter-scala@0.23.4`
  cross-verified byte-equal to the git tag for
  parser.c/scanner.c/node-types.json/grammar.json/LICENSE. The npm
  tarball is the final vendor.sh fallback (sha256
  `c4c92b40d00b7d3a2ca1412ebadefae52963dd000f0e2d683afa572eff1bb171`).
- THE 0.24.0 TWIN WAS REJECTED: the npm 0.24.0 package was regenerated
  from a different commit than the v0.24.0 git tag (20285 vs 19568
  parser states; different symbol sets) -- the dual-source discipline
  requires byte identity, so the pin stays at v0.23.4. Nothing at
  0.23.4 is modified.
- The external scanner (src/scanner.c: the significant-indentation
  outdent tokens, string-interpolation delimiters) was audited CLEAN
  at adoption against the Swift lessons: `ts_calloc(1, ...)`, no bit
  shifts, bounded serialize/deserialize. The sanitize cell runs it
  fully instrumented (no carve-out).
