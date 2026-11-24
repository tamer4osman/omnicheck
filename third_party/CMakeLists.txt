# Vendored third-party code. Sources are fetched by scripts/vendor.sh at
# pinned refs recorded in THIRD_PARTY_NOTICES.md. Do not edit files here.
set(TS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter")

# tree-sitter runtime: single-TU amalgam (lib.c includes all translation
# units, including wasm_store.c which is a no-op stub on native targets).
add_library(tree-sitter STATIC "${TS_DIR}/lib/src/lib.c")
# The runtime uses POSIX extensions (le16toh, fdopen) guarded by
# _DEFAULT_SOURCE; our strict -std=c11 build hides them otherwise.
target_compile_definitions(tree-sitter PRIVATE _DEFAULT_SOURCE)
target_include_directories(tree-sitter PRIVATE
    "${TS_DIR}/lib/src"
    "${TS_DIR}/lib/include"
)
target_include_directories(tree-sitter SYSTEM INTERFACE
    "${TS_DIR}/lib/include"
    "${TS_DIR}/lib/src"
)

# Grammar parsers. Each grammar ships its own tree_sitter/parser.h copy under
# src/, so its src/ directory must be on the include path for the target.
function(oc_add_grammar name dir)
    set(gram_sources "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/src/parser.c")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/src/scanner.c")
        list(APPEND gram_sources "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/src/scanner.c")
    endif()
    add_library(ts-grammar-${name} STATIC ${gram_sources})
    target_include_directories(ts-grammar-${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/src"
        "${TS_DIR}/lib/src"
    )
    target_include_directories(ts-grammar-${name} SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/src"
    )
endfunction()

oc_add_grammar(javascript tree-sitter-javascript)
oc_add_grammar(python tree-sitter-python)
# tree-sitter-typescript ships two grammars from one repo (shared common/).
oc_add_grammar(typescript tree-sitter-typescript/typescript)
oc_add_grammar(tsx tree-sitter-typescript/tsx)
# tree-sitter-go (SPRINT-9, Track D wave 1): single generated parser.c,
# no external scanner; vendored from the pinned v0.25.0 sources (see
# THIRD_PARTY_NOTICES.md for the provenance note).
oc_add_grammar(go tree-sitter-go)
# tree-sitter-java (SPRINT-11, Track D wave 1): single generated parser.c,
# no external scanner; vendored from the pinned v0.23.5 tag of the MAIN
# tree-sitter org (see THIRD_PARTY_NOTICES.md for the provenance note,
# including the org correction against the pre-adoption contract).
oc_add_grammar(java tree-sitter-java)
# tree-sitter-c-sharp (SPRINT-13, Track D wave 1): generated parser.c
# (LANGUAGE_VERSION 15) PLUS an external scanner (src/scanner.c --
# interpolated strings, optional semicolons), which oc_add_grammar
# picks up automatically; vendored from the pinned v0.24.0 sources (see
# THIRD_PARTY_NOTICES.md for the provenance note). The sdist carries a
# per-grammar tree_sitter/parser.h copy, so the grammar's own src/ is
# self-contained on the include path.
oc_add_grammar(csharp tree-sitter-c-sharp)
# tree-sitter-rust (SPRINT-15, Track D wave 1): generated parser.c PLUS an
# external scanner (src/scanner.c -- raw strings, hash lifetimes), which
# oc_add_grammar picks up automatically; vendored from the pinned v0.24.0
# sources of the MAIN tree-sitter org (see THIRD_PARTY_NOTICES.md for the
# provenance note, including the npm-tarball path and the codeload
# byte-identity cross-check).
oc_add_grammar(rust tree-sitter-rust)
# tree-sitter-ruby (SPRINT-16, Track D wave 1): generated parser.c
# (LANGUAGE_VERSION 14) PLUS an external scanner (src/scanner.c --
# heredocs, delimited string/regex/array literal start & end tokens),
# which oc_add_grammar picks up automatically; vendored from the pinned
# v0.23.1 sources of the MAIN tree-sitter org (see THIRD_PARTY_NOTICES.md
# for the provenance note, including the npm-tarball path and the
# codeload byte-identity cross-check).
oc_add_grammar(ruby tree-sitter-ruby)
# tree-sitter-php (SPRINT-19, Track E wave-2 completion): generated
# parser.c (LANGUAGE_VERSION 15) PLUS an external scanner (src/scanner.c
# -- template text, interpolation braces), which oc_add_grammar picks up
# automatically; vendored from the pinned v0.24.2 sources of the MAIN
# tree-sitter org (see THIRD_PARTY_NOTICES.md for the provenance note,
# including the npm-tarball path and the codeload byte-identity
# cross-check). The v0.22+ repo layout keeps php/, php_only/ and common/
# subdirectories; the MIXED php+html grammar (php/) is the one compiled --
# php_only/ stays vendored bytes. The scanner's shared header is
# included RELATIVELY ("../../common/scanner.h" from php/src/), so no
# extra include dir is needed (proven by the SPRINT-19 adoption build;
# the probe carried -I common as belt-and-braces only).
oc_add_grammar(php tree-sitter-php/php)
# tree-sitter-c (SPRINT-20, wave-2): generated parser.c ONLY -- the
# grammar ships NO external scanner (src/ holds parser.c, grammar.json,
# node-types.json and tree_sitter/; verified at adoption, so
# oc_add_grammar's scanner pickup finds nothing); vendored from the
# pinned v0.23.4 sources of the MAIN tree-sitter org (see
# THIRD_PARTY_NOTICES.md for the provenance note, including the
# npm-tarball path and the byte-identity cross-check).
oc_add_grammar(c tree-sitter-c)
# tree-sitter-cpp (SPRINT-21, wave-2 close-out): generated parser.c PLUS
# an external scanner (src/scanner.c -- raw strings, angle-bracket
# disambiguation), which oc_add_grammar picks up automatically; the C
# grammar is inlined at generation time, so src/ is self-contained.
# Vendored from the pinned v0.23.4 tag of the MAIN tree-sitter org (see
# THIRD_PARTY_NOTICES.md for the provenance note, including the
# npm-tarball path and the byte-identity cross-check).
oc_add_grammar(cpp tree-sitter-cpp)
# tree-sitter-kotlin (SPRINT-22, wave-3 opens): generated parser.c
# (LANGUAGE_VERSION 14) PLUS an external scanner (src/scanner.c --
# automatic semicolons, string-interpolation start/end/content, nested
# multiline comments, the safe-navigation token), which oc_add_grammar
# picks up automatically; the grammar is self-contained (src/ holds
# parser.c, scanner.c, grammar.json, node-types.json, tree_sitter/).
# Vendored from the pinned 0.3.8 tag of fwcd/tree-sitter-kotlin (the
# canonical community grammar -- the MAIN tree-sitter org has no Kotlin
# grammar; see THIRD_PARTY_NOTICES.md for the provenance note,
# including the npm-tarball path and the byte-identity cross-check).
oc_add_grammar(kotlin tree-sitter-kotlin)
# tree-sitter-swift (SPRINT-23, wave-3 continues): generated parser.c
# (LANGUAGE_VERSION 14) PLUS an external scanner (src/scanner.c -- block
# comments, raw string parts, implicit/explicit semicolons, the operator
# and directive tokens), which oc_add_grammar picks up automatically;
# the grammar is self-contained (src/ holds parser.c, parser_abi13.c,
# parser_abi14.c, scanner.c, grammar.json, node-types.json,
# tree_sitter/). Vendored from the pinned 0.7.1-with-generated-files tag
# of alex-pinkus/tree-sitter-swift (the CANONICAL Swift grammar: the
# tree-sitter org's tree-sitter-swift is an abandoned unrelated project
# -- its own README says so -- and every npm tree-sitter-swift version
# publishes from alex-pinkus; the tag carries NO v prefix). The npm
# binding package tree-sitter-swift@0.7.1 was cross-verified byte-equal
# to the git tag for parser.c/scanner.c/node-types.json/grammar.json/
# LICENSE (see THIRD_PARTY_NOTICES.md for the provenance note). The
# npm-tarball fallback needs the PLAIN version (the tag and the npm
# version differ for this grammar), passed as the explicit 6th arg.
oc_add_grammar(swift tree-sitter-swift)
# tree-sitter-swift sanitizer carve-out (SPRINT-23): the pinned tag's
# external scanner carries TWO upstream defects. (1) A memory-safety
# defect: tree_sitter_swift_external_scanner_create allocates the
# scanner state with calloc(0, sizeof(...)) (scanner.c:237, a zero-byte
# region) and serialize/deserialize then read and write the 4-byte
# state word out of bounds (scanner.c:251) -- under ASan a hard
# heap-buffer-overflow abort on every Swift parse. (2) Undefined-behavior
# defect with REAL behavioral fallout: the operator-suppressor mask loop
# shifts `1 << suppressor` as a signed int (scanner.c:514); at shift 31
# the int shift is UB and the mask word is SIGN-EXTENDED into the
# uint64_t test, so the scanner suppresses a different token set than
# the upstream-correct `1ULL` spelling produces -- under the sanitize
# build this flips a real parse from clean to had_error (the UBSan
# report is not pedantry, it is the divergence). Upstream master fixed
# both (calloc(1, ...) + an OOM guard; `1ULL << suppressor`), but no
# npm twin exists to re-prove the dual-source pin against (npm latest
# is still 0.7.1, published 2025-06-23), so the pin stays and the
# vendored bytes stay UNMODIFIED. Treatment: the project-owned build
# glue excludes ONLY this one translation unit from ASan and UBSan
# instrumentation under the sanitize preset; first-party code and every
# other vendored scanner remain fully instrumented. Revisit on the next
# tagged release that carries the upstream fixes.
if(OC_SANITIZE_ADDRESS)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-swift/src/scanner.c"
        PROPERTIES COMPILE_OPTIONS "-fno-sanitize=address;-fno-sanitize=undefined")
endif()
# tree-sitter-scala (SPRINT-24, wave-3 continues): generated parser.c
# (LANGUAGE_VERSION 14) PLUS an external scanner (src/scanner.c -- the
# significant-indentation outdent tokens and string-interpolation
# delimiters), which oc_add_grammar picks up automatically; the grammar
# is self-contained (src/ holds parser.c, scanner.c, grammar.json,
# node-types.json, tree_sitter/). Vendored from the pinned v0.23.4 tag
# of the MAIN tree-sitter org (see THIRD_PARTY_NOTICES.md for the
# provenance note). The 0.24.0 npm twin was regenerated from a different
# commit than the v0.24.0 git tag (20285 vs 19568 parser states), so the
# pin stays at v0.23.4 where the npm 0.23.4 binding package was
# cross-verified byte-equal to the git tag for parser.c/scanner.c/
# node-types.json/grammar.json/LICENSE. The scanner was audited CLEAN at
# adoption against the Swift lessons: ts_calloc(1, ...) (not calloc(0)),
# no bit shifts, bounded serialize/deserialize -- no sanitizer carve-out.
oc_add_grammar(scala tree-sitter-scala)
