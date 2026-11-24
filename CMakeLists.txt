cmake_minimum_required(VERSION 3.16)
project(omnicheck VERSION 0.3.0 LANGUAGES C)

# OmniCheck (working name) -- a deterministic, multi-language static analysis
# engine in C11, with language packs and an OmniDecide triage sidecar seam.
# See README.md and docs/ARCHITECTURE.md.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OC_WERROR "Treat compiler warnings as errors" OFF)
option(OC_BUILD_TESTS "Build the test suite" ON)
option(OC_WITH_TREE_SITTER "Build tree-sitter parsing backends (requires vendored sources)" ON)
option(OC_SANITIZE_ADDRESS "Build with AddressSanitizer" OFF)
option(OC_SANITIZE_UNDEFINED "Build with UndefinedBehaviorSanitizer" OFF)

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

# ---------------------------------------------------------------------------
# Core static library: everything except the CLI entry point.
# ---------------------------------------------------------------------------
set(OC_CORE_SOURCES
    src/arena.c
    src/common.c
    src/str.c
    src/json.c
    src/fingerprint.c
    src/finding.c
    src/graph.c
    src/imports.c
    src/pathutil.c
    src/tsconfig.c
    src/surface.c
    src/coverage.c
    src/dup.c
    src/pack.c
    src/buildtag.c
    src/discover.c
    src/parser.c
    src/pass.c
    src/passes/long_function.c
    src/passes/todo_comment.c
    src/passes/import_cycle.c
    src/passes/complexity.c
    src/passes/unused_export.c
    src/passes/unreferenced_module.c
    src/passes/crap.c
    src/passes/duplicate_code.c
    src/passes/dep_vuln.c
    src/passes/licence.c
    src/passes/secret.c
    src/supply.c
    src/mutate.c
    src/compat.c
    src/triage.c
    src/scan.c
    src/lsp.c
    src/mcp.c
    src/report.c
)

add_library(oc STATIC ${OC_CORE_SOURCES})
target_include_directories(oc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(oc PRIVATE _POSIX_C_SOURCE=200809L)
oc_set_warnings(oc)

# ---------------------------------------------------------------------------
# tree-sitter backends (vendored under third_party/ via scripts/vendor.sh)
# ---------------------------------------------------------------------------
if(OC_WITH_TREE_SITTER)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/lib/src/parser.c")
        add_subdirectory(third_party)
        target_compile_definitions(oc PUBLIC OC_WITH_TREE_SITTER=1)
        target_sources(oc PRIVATE src/parser_ts.c)
        target_link_libraries(oc PRIVATE tree-sitter ts-grammar-javascript ts-grammar-python
                                       ts-grammar-typescript ts-grammar-tsx ts-grammar-go
                                       ts-grammar-java ts-grammar-csharp ts-grammar-rust
                                       ts-grammar-ruby ts-grammar-php ts-grammar-c
                                       ts-grammar-cpp ts-grammar-kotlin ts-grammar-swift
                                       ts-grammar-scala)
        message(STATUS "OmniCheck: tree-sitter backends enabled (runtime + javascript + python "
                        "+ typescript + tsx + go + java + csharp + rust + ruby + php + c + cpp "
                        "+ kotlin + swift + scala)")
    else()
        message(WARNING
            "tree-sitter sources not found under third_party/. Building WITHOUT AST parsing; "
            "'oc check' will fail at runtime. Run scripts/vendor.sh, then reconfigure.")
    endif()
endif()

# ---------------------------------------------------------------------------
# OmniDecide-C triage provider (vendored under third_party/omnidecide/,
# decision L8 of docs/OMNIDECIDE_LEVERAGE.md -- see its PROVENANCE.md).
# The kernel builds as a plain subdirectory (upstream CMake, unmodified);
# the protocol-v0 provider shim is the ONLY first-party target that links
# it. The seam stays opt-in at runtime: triage activates only when
# OC_OMNIDECIDE_BIN points at the provider (or any protocol-v0 binary).
# ---------------------------------------------------------------------------
option(OC_WITH_OMNIDECIDE "Build the vendored OmniDecide-C triage provider" ON)
if(OC_WITH_OMNIDECIDE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/omnidecide/CMakeLists.txt")
        # Upstream options default OFF as a subdirectory; pin them anyway so
        # future upstream option changes cannot silently widen our build.
        set(OMNIDECIDE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(OMNIDECIDE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/omnidecide)
        add_executable(oc-omnidecide-provider tools/omnidecide/oc_odk_provider.c)
        target_link_libraries(oc-omnidecide-provider PRIVATE omnidecide)
        oc_set_warnings(oc-omnidecide-provider)
        # Post-hoc AUDITOR companion (memo L1): dump/check mode over the
        # same protocol-v0 request; MAY exit 2 on mismatch (auditors fail
        # closed, pipeline components fail open). Never used by the scan.
        add_executable(oc-omnidecide-explain tools/omnidecide/oc_odk_explain.c)
        target_link_libraries(oc-omnidecide-explain PRIVATE omnidecide)
        oc_set_warnings(oc-omnidecide-explain)
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/omnidecide/CMakeLists.txt"
             _odk_cmake_txt)
        string(REGEX MATCH "project\\(omnidecide VERSION ([0-9.]+)" _odk_ver "${_odk_cmake_txt}")
        message(STATUS "OmniCheck: OmniDecide-C v${CMAKE_MATCH_1} vendored; "
                       "provider target oc-omnidecide-provider, auditor "
                       "oc-omnidecide-explain")
    else()
        message(STATUS "OmniCheck: OmniDecide kernel not vendored "
                       "(third_party/omnidecide missing); triage provider NOT built. "
                       "The seam stays opt-in via OC_OMNIDECIDE_BIN with any protocol-v0 provider.")
    endif()
endif()

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(oc-cli src/cli/main.c)
set_target_properties(oc-cli PROPERTIES OUTPUT_NAME oc)
target_link_libraries(oc-cli PRIVATE oc)
oc_set_warnings(oc-cli)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(OC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
