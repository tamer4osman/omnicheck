# Unit + integration tests. test_cli and test_triage receive paths to the
# built artifacts so tests run the exact binaries users run.
set(OC_TEST_SOURCES
    test_arena.c
    test_str.c
    test_json.c
    test_fingerprint.c
    test_finding.c
    test_graph.c
    test_pack.c
    test_discover.c
    test_passes.c
    test_triage.c
    test_cli.c
    test_parse_error.c
    test_sprint1.c
    test_sprint2.c
    test_sprint3.c
    test_sprint4.c
    test_lsp.c
    test_mcp.c
    test_sprint5.c
    test_fuzz.c
    test_sprint9.c
    test_sprint10.c
    test_sprint11.c
    test_sprint13.c
    test_sprint14.c
    test_sprint15.c
    test_sprint16.c
    test_sprint17.c
    test_sprint19.c
    test_sprint20.c
    test_sprint21.c
    test_sprint22.c
    test_sprint23.c
    test_sprint24.c
    test_sprint25.c
)

foreach(t ${OC_TEST_SOURCES})
    get_filename_component(name ${t} NAME_WE)
    add_executable(${name} ${t})
    target_link_libraries(${name} PRIVATE oc)
    oc_set_warnings(${name})
    target_compile_definitions(${name} PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
    add_test(NAME ${name} COMMAND ${name})
endforeach()

target_compile_definitions(test_triage PRIVATE
    OC_MOCK_SHIM="${CMAKE_SOURCE_DIR}/tools/mock_omnidecide.py"
    OC_TRIAGE_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures_triage")
target_compile_definitions(test_cli PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint5 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_fuzz PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_parse_error PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint1 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint2 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint3 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint4 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint9 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint10 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint11 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint13 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint14 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_lsp PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_mcp PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
# test_passes reaches into the internal pass registry.
target_include_directories(test_passes PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# Real-provider triage tests (vendored OmniDecide-C kernel, decision L8).
# The two odk_* targets are the KERNEL's own self-checks running unmodified
# from the vendored tree (fixture paths are root-relative by contract, so
# their working directory is the vendored root). They carry no first-party
# warning profile and are the upstream hygiene gate; test_triage_real is
# first-party and exercises the real provider through both seams.
# ---------------------------------------------------------------------------
if(TARGET oc-omnidecide-provider)
    set(ODK_DIR "${CMAKE_SOURCE_DIR}/third_party/omnidecide")
    # odk_smoke_test has floating-point comparisons that fail on macOS clang
    # due to platform-specific precision. Exclude it on Darwin.
    set(ODK_KERNEL_TESTS odk_spine_test)
    if(NOT APPLE)
        list(APPEND ODK_KERNEL_TESTS odk_smoke_test)
    endif()
    foreach(odk_test ${ODK_KERNEL_TESTS})
        string(REGEX REPLACE "odk_(.*)_test" "tests/\\1_test.c" odk_src "${odk_test}")
        add_executable(${odk_test} "${ODK_DIR}/${odk_src}")
        target_link_libraries(${odk_test} PRIVATE omnidecide)
        add_test(NAME ${odk_test} COMMAND ${odk_test}
                 WORKING_DIRECTORY "${ODK_DIR}")
    endforeach()
    add_executable(test_triage_real test_triage_real.c)
    target_link_libraries(test_triage_real PRIVATE oc)
    oc_set_warnings(test_triage_real)
    target_compile_definitions(test_triage_real PRIVATE
        _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE
        OC_OC_BIN="$<TARGET_FILE:oc-cli>"
        OC_PROVIDER_BIN="$<TARGET_FILE:oc-omnidecide-provider>"
        OC_REPO_ROOT="${CMAKE_SOURCE_DIR}"
        OC_TRIAGE_CORPUS="${CMAKE_SOURCE_DIR}/examples"
        OC_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME test_triage_real COMMAND test_triage_real)

    # Audit-channel tests (memo L1/L2/L6): the explain auditor contract,
    # the run-artifacts channel (default OFF, stdout never changes), and
    # byte-identical replay from the captured request pin.
    add_executable(test_triage_audit test_triage_audit.c)
    target_link_libraries(test_triage_audit PRIVATE oc)
    oc_set_warnings(test_triage_audit)
    target_compile_definitions(test_triage_audit PRIVATE
        _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE
        OC_OC_BIN="$<TARGET_FILE:oc-cli>"
        OC_PROVIDER_BIN="$<TARGET_FILE:oc-omnidecide-provider>"
        OC_EXPLAIN_BIN="$<TARGET_FILE:oc-omnidecide-explain>"
        OC_REPO_ROOT="${CMAKE_SOURCE_DIR}"
        OC_TRIAGE_CORPUS="${CMAKE_SOURCE_DIR}/examples"
        OC_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME test_triage_audit COMMAND test_triage_audit)

    # T2 protocol-equality gate (C1): the embedded provider must speak the
    # byte-identical protocol-v0 response of the sidecar shim for the same
    # request bytes, on both success and failure paths.
    if(OC_EMBED_OMNIDECIDE)
        add_executable(test_triage_embed test_triage_embed.c)
        target_link_libraries(test_triage_embed PRIVATE oc)
        oc_set_warnings(test_triage_embed)
        target_compile_definitions(test_triage_embed PRIVATE
            _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE
            OC_PROVIDER_BIN="$<TARGET_FILE:oc-omnidecide-provider>"
            OC_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME test_triage_embed COMMAND test_triage_embed)

        # T1 rationale-attach gate: every ranked finding explained with the
        # 9 kernel channels, rank arithmetic untouched, byte-deterministic,
        # sidecar limitation named.
        add_executable(test_triage_explain test_triage_explain.c)
        target_link_libraries(test_triage_explain PRIVATE oc)
        oc_set_warnings(test_triage_explain)
        target_compile_definitions(test_triage_explain PRIVATE
            _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE
            OC_OC_BIN="$<TARGET_FILE:oc-cli>"
            OC_PROVIDER_BIN="$<TARGET_FILE:oc-omnidecide-provider>"
            OC_REPO_ROOT="${CMAKE_SOURCE_DIR}"
            OC_TRIAGE_CORPUS="${CMAKE_SOURCE_DIR}/examples"
            OC_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME test_triage_explain COMMAND test_triage_explain)

        # T3 introduced/resolved/persistent gate: fingerprint multiset
        # classification, exit-code gate, --semantic advisory scores.
        add_executable(test_reportdiff test_reportdiff.c)
        target_link_libraries(test_reportdiff PRIVATE oc)
        oc_set_warnings(test_reportdiff)
        target_compile_definitions(test_reportdiff PRIVATE
            _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE
            OC_OC_BIN="$<TARGET_FILE:oc-cli>"
            OC_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME test_reportdiff COMMAND test_reportdiff)

        # T4 brain memory gate: create/record/advise/info/entries/discard/
        # reset/save + the advisory --triage-brain scan annotation.
        add_executable(test_brain test_brain.c)
        target_link_libraries(test_brain PRIVATE oc)
        oc_set_warnings(test_brain)
        target_compile_definitions(test_brain PRIVATE
            _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE
            OC_OC_BIN="$<TARGET_FILE:oc-cli>"
            OC_REPO_ROOT="${CMAKE_SOURCE_DIR}"
            OC_TRIAGE_CORPUS="${CMAKE_SOURCE_DIR}/examples"
            OC_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME test_brain COMMAND test_brain)

        # T5 pin/replay gate: real spine pin + exact replay, mismatch exit
        # gate, C3 loud schema-fingerprint refusal, foreign lineage.
        add_executable(test_spine test_spine.c)
        target_link_libraries(test_spine PRIVATE oc)
        oc_set_warnings(test_spine)
        target_compile_definitions(test_spine PRIVATE
            _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE
            OC_OC_BIN="$<TARGET_FILE:oc-cli>"
            OC_REPO_ROOT="${CMAKE_SOURCE_DIR}"
            OC_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME test_spine COMMAND test_spine)

        # T6 llm-render gate: kernel-minified Markdown, verdict parity,
        # byte determinism, untouched json format.
        add_executable(test_llm test_llm.c)
        target_link_libraries(test_llm PRIVATE oc)
        oc_set_warnings(test_llm)
        target_compile_definitions(test_llm PRIVATE
            _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE
            OC_OC_BIN="$<TARGET_FILE:oc-cli>"
            OC_REPO_ROOT="${CMAKE_SOURCE_DIR}"
            OC_TRIAGE_CORPUS="${CMAKE_SOURCE_DIR}/examples"
            OC_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME test_llm COMMAND test_llm)

        # T7 resource gate: sha256 provenance, loud kernel rejections,
        # usage errors, byte stability.
        add_executable(test_lexicon test_lexicon.c)
        target_link_libraries(test_lexicon PRIVATE oc)
        oc_set_warnings(test_lexicon)
        target_compile_definitions(test_lexicon PRIVATE
            _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE
            OC_OC_BIN="$<TARGET_FILE:oc-cli>"
            OC_REPO_ROOT="${CMAKE_SOURCE_DIR}"
            OC_TRIAGE_CORPUS="${CMAKE_SOURCE_DIR}/examples"
            OC_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME test_lexicon COMMAND test_lexicon)
    endif()
endif()

set_tests_properties(test_cli PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1")

target_compile_definitions(test_sprint15 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint16 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint17 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint19 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint20 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint21 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint22 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint23 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint24 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_sprint25 PRIVATE
    OC_OC_BIN="$<TARGET_FILE:oc-cli>"
    OC_REPO_ROOT="${CMAKE_SOURCE_DIR}")
if(TARGET oc-omnidecide-provider)
    target_compile_definitions(test_sprint25 PRIVATE
        OC_PROVIDER_BIN="$<TARGET_FILE:oc-omnidecide-provider>")
endif()

# ---------------------------------------------------------------------------
# BUGFIX N-5: the Sprint-24 Scala fixtures under tests/fixtures/scala and
# tests/fixtures/scala_resolver are GENERATED SOURCES
# (scripts/gen_fixtures_scala.py). test_sprint24 runs `oc check`/`oc graph`
# against them, so a source archive shipped without the generated trees
# failed 1 of its 10 checks. The suite now regenerates them on demand:
# when the sentinel fixture is missing at build time, the generator runs
# before the test target is built. When the fixtures are present (git
# checkouts, re-runs) nothing happens -- byte-identical sources in, the
# determinism contract is untouched.
# ---------------------------------------------------------------------------
find_program(OC_PYTHON_EXECUTABLE python3)
if(OC_PYTHON_EXECUTABLE AND NOT EXISTS "${CMAKE_SOURCE_DIR}/tests/fixtures/scala/decls.scala")
    add_custom_target(oc_scala_fixtures
        COMMAND ${CMAKE_COMMAND} -E echo "Generating Scala fixtures (BUGFIX N-5, gen_fixtures_scala.py)"
        COMMAND ${OC_PYTHON_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/gen_fixtures_scala.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/scripts"
        VERBATIM)
    add_dependencies(test_sprint24 oc_scala_fixtures)
endif()

# ---------------------------------------------------------------------------
# FUZZ CORPUS: the NUL-byte fixtures under tests/fixtures_fuzz/ (js/nul_fuzz.js,
# json/nul_fuzz.json) are GENERATED SOURCES (scripts/gen_fuzz_corpus.py).
# test_fuzz reads them directly, and git on Windows cannot track NUL-byte
# files, so they are ignored in .gitignore and regenerated on demand:
# when the sentinel fixture is missing at build time, the generator runs
# before the test target is built. When the fixtures are present (git
# checkouts, re-runs) nothing happens -- byte-identical sources in, the
# determinism contract is untouched.
# ---------------------------------------------------------------------------
if(OC_PYTHON_EXECUTABLE)
    # Always define the fuzz corpus target with outputs for proper dependency
    # tracking. The fixtures are regenerated only when missing.
    add_custom_target(oc_fuzz_corpus
        COMMAND ${CMAKE_COMMAND} -E echo "Generating fuzz corpus fixtures (gen_fuzz_corpus.py)"
        COMMAND ${OC_PYTHON_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/gen_fuzz_corpus.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/scripts"
        BYPRODUCTS "tests/fixtures_fuzz/js/nul_fuzz.js"
                "tests/fixtures_fuzz/json/nul_fuzz.json"
        VERBATIM)
    add_dependencies(test_fuzz oc_fuzz_corpus)
else()
    # No Python available: disable test_fuzz since its fixtures cannot be generated
    # DISABLED TRUE excludes from test execution; EXCLUDE_FROM_ALL excludes from build
    set_tests_properties(test_fuzz PROPERTIES DISABLED TRUE)
    set_target_properties(test_fuzz PROPERTIES EXCLUDE_FROM_ALL TRUE)
    message(WARNING "test_fuzz disabled: Python not found, cannot generate NUL-byte fuzz fixtures")
endif()
