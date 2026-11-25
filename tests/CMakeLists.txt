#Unit + integration tests.test_cli and test_triage receive paths to the
#built artifacts so tests run the exact binaries users run.
set(OC_TEST_SOURCES test_arena.c test_str.c test_json.c test_fingerprint.c test_finding.c test_graph
        .c test_pack.c test_discover.c test_passes.c test_triage.c test_cli.c test_parse_error
        .c test_sprint1.c test_sprint2.c test_sprint3.c test_sprint4.c test_lsp.c test_mcp
        .c test_sprint5.c test_fuzz.c test_sprint9.c test_sprint10.c test_sprint11.c test_sprint13
        .c test_sprint14.c test_sprint15.c test_sprint16.c test_sprint17.c test_sprint19
        .c test_sprint20.c test_sprint21.c test_sprint22.c test_sprint23.c test_sprint24
        .c test_sprint25.c)

    foreach (t ${OC_TEST_SOURCES}) get_filename_component(name ${t} NAME_WE) add_executable(${
        name} ${t}) target_link_libraries(${name} PRIVATE oc) oc_set_warnings(${
        name}) target_compile_definitions(${name} PRIVATE _POSIX_C_SOURCE = 200809L _DEFAULT_SOURCE) if (APPLE) target_compile_definitions(${
        name} PRIVATE _DARWIN_C_SOURCE) endif() add_test(NAME ${name} COMMAND ${name}) endforeach()

        target_compile_definitions(test_triage PRIVATE OC_MOCK_SHIM = "${CMAKE_SOURCE_DIR}/tools/"
                                                                      "mock_omnidecide."
                                                                      "py" OC_TRIAGE_FIXTURES = "${"
                                                                                                "CM"
                                                                                                "AK"
                                                                                                "E_"
                                                                                                "SO"
                                                                                                "UR"
                                                                                                "CE"
                                                                                                "_D"
                                                                                                "IR"
                                                                                                "}/"
                                                                                                "te"
                                                                                                "st"
                                                                                                "s/"
                                                                                                "fi"
                                                                                                "xt"
                                                                                                "ur"
                                                                                                "es"
                                                                                                "_t"
                                                                                                "ri"
                                                                                                "ag"
                                                                                                "e") target_compile_definitions(
            test_cli PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint5 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                                                                      "${CMAKE_SOURCE_DIR}")
            target_compile_definitions(test_fuzz PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-"
                                                                     "cli>" OC_REPO_ROOT = "${"
                                                                                           "CMAKE_"
                                                                                           "SOURCE_"
                                                                                           "DIR}") target_compile_definitions(
                test_parse_error PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                    "${CMAKE_SOURCE_DIR}")
                target_compile_definitions(test_sprint1 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-"
                                                                            "cli>" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(
                    test_sprint2 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                        "${CMAKE_SOURCE_DIR}")
                    target_compile_definitions(test_sprint3 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-"
                                                                                "cli"
                                                                                ">" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(
                        test_sprint4 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                            "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint9 PRIVATE
                                                                                  OC_OC_BIN =
                                                                                      "$<TARGET_"
                                                                                      "FILE:oc-"
                                                                                      "cli"
                                                                                      ">" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}")
                        target_compile_definitions(test_sprint10 PRIVATE OC_OC_BIN = "$<TARGET_"
                                                                                     "FILE:oc-"
                                                                                     "cli"
                                                                                     ">" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(
                            test_sprint11 PRIVATE OC_OC_BIN =
                                "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                                    "${CMAKE_SOURCE_DIR}")
                            target_compile_definitions(
                                test_sprint13 PRIVATE OC_OC_BIN =
                                    "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                                        "${CMAKE_SOURCE_DIR}")
                                target_compile_definitions(
                                    test_sprint14 PRIVATE OC_OC_BIN =
                                        "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                                            "${CMAKE_SOURCE_DIR}")
                                    target_compile_definitions(
                                        test_lsp PRIVATE OC_OC_BIN =
                                            "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                                                "${CMAKE_SOURCE_DIR}")
                                        target_compile_definitions(
                                            test_mcp PRIVATE OC_OC_BIN =
                                                "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                                                    "${CMAKE_SOURCE_DIR}")
#test_passes reaches into the internal pass registry.
                                            target_include_directories(test_passes PRIVATE ${
                                                                           CMAKE_SOURCE_DIR} /
                                                                       src)

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
#Real - provider triage tests(vendored OmniDecide - C kernel, decision L8).
#The two odk_ *targets are the KERNEL's own self-checks running unmodified
#from the vendored tree(fixture paths are root - relative by contract, so
#their working directory is the vendored root).They carry no first - party
#warning profile and are the upstream hygiene gate; test_triage_real is
#first - party and exercises the real provider through both seams.
#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
                                                if (TARGET oc - omnidecide -
                                                    provider) set(ODK_DIR
                                                                  "${CMAKE_SOURCE_DIR}/third_party/"
                                                                  "omnidecide")
                                                    foreach (odk_test odk_smoke_test odk_spine_test) string(
                                                        REGEX REPLACE
                                                        "odk_(.*)_test"
                                                        "tests/\\1_test.c" odk_src
                                                        "${odk_test}") add_executable($ {
                                                        odk_test
                                                    } "${ODK_DIR}/${odk_src}") target_link_libraries(${
                                                        odk_test} PRIVATE omnidecide) add_test(NAME ${
                                                        odk_test} COMMAND ${
                                                        odk_test} WORKING_DIRECTORY "${ODK_DIR}")
                                                        endforeach() add_executable(
                                                            test_triage_real test_triage_real
                                                                .c) target_link_libraries(test_triage_real PRIVATE oc)
                                                            oc_set_warnings(test_triage_real) target_compile_definitions(
                                                                test_triage_real PRIVATE
                                                                    _POSIX_C_SOURCE = 200809L _DEFAULT_SOURCE OC_OC_BIN =
                                                                        "$<TARGET_FILE:oc-"
                                                                        "cli>" OC_PROVIDER_BIN =
                                                                            "$<TARGET_FILE:oc-"
                                                                            "omnidecide-"
                                                                            "provider"
                                                                            ">" OC_REPO_ROOT =
                                                                                "${CMAKE_SOURCE_"
                                                                                "DIR"
                                                                                "}" OC_TRIAGE_CORPUS =
                                                                                    "${CMAKE_"
                                                                                    "SOURCE_DIR}/"
                                                                                    "example"
                                                                                    "s" OC_TEST_TMPDIR =
                                                                                        "${CMAKE_"
                                                                                        "CURRENT_"
                                                                                        "BINARY_"
                                                                                        "DIR}") add_test(NAME test_triage_real
                                                                                                             COMMAND test_triage_real)

#Audit - channel tests(memo L1 / L2 / L6) : the explain auditor contract,
#the run - artifacts channel(default OFF, stdout never changes), and
#byte - identical replay from the captured request pin.
                                                                add_executable(
                                                                    test_triage_audit test_triage_audit
                                                                        .c) target_link_libraries(test_triage_audit PRIVATE oc)
                                                                    oc_set_warnings(test_triage_audit) target_compile_definitions(
                                                                        test_triage_audit PRIVATE
                                                                            _POSIX_C_SOURCE = 200809L _DEFAULT_SOURCE OC_OC_BIN =
                                                                                "$<TARGET_FILE:oc-"
                                                                                "cli"
                                                                                ">" OC_PROVIDER_BIN =
                                                                                    "$<TARGET_FILE:"
                                                                                    "oc-omnidecide-"
                                                                                    "provider"
                                                                                    ">" OC_EXPLAIN_BIN =
                                                                                        "$<TARGET_"
                                                                                        "FILE:oc-"
                                                                                        "omnidecide"
                                                                                        "-explain"
                                                                                        ">" OC_REPO_ROOT =
                                                                                            "${"
                                                                                            "CMAKE_"
                                                                                            "SOURCE"
                                                                                            "_DIR"
                                                                                            "}" OC_TRIAGE_CORPUS =
                                                                                                "${"
                                                                                                "CM"
                                                                                                "AK"
                                                                                                "E_"
                                                                                                "SO"
                                                                                                "UR"
                                                                                                "CE"
                                                                                                "_D"
                                                                                                "IR"
                                                                                                "}/"
                                                                                                "ex"
                                                                                                "am"
                                                                                                "pl"
                                                                                                "e"
                                                                                                "s" OC_TEST_TMPDIR =
                                                                                                    "${CMAKE_CURRENT_BINARY_DIR}")
                                                                        add_test(
                                                                            NAME test_triage_audit COMMAND
                                                                                test_triage_audit) endif()

                                                                            set_tests_properties(
                                                                                test_cli
                                                                                    PROPERTIES ENVIRONMENT
                                                                                "ASAN_OPTIONS="
                                                                                "detect_leaks=1")

                                                                                target_compile_definitions(test_sprint15 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint16 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint17
                                                                                                                                                                                                                                                                                                                                                                     PRIVATE
                                                                                                                                                                                                                                                                                                                                                                         OC_OC_BIN =
                                                                                                                                                                                                                                                                                                                                                                             "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint19 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint20 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint21 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint22
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint23
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            PRIVATE
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    "${CMAKE_SOURCE_DIR}") target_compile_definitions(test_sprint24 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT = "${CMAKE_SOURCE_DIR}")
                                                                                    target_compile_definitions(
                                                                                        test_sprint25 PRIVATE OC_OC_BIN = "$<TARGET_FILE:oc-cli>" OC_REPO_ROOT =
                                                                                            "${"
                                                                                            "CMAKE_"
                                                                                            "SOURCE"
                                                                                            "_DIR"
                                                                                            "}") if (TARGET
                                                                                                         oc -
                                                                                                     omnidecide -
                                                                                                     provider)
                                                                                        target_compile_definitions(
                                                                                            test_sprint25 PRIVATE OC_PROVIDER_BIN =
                                                                                                "$<"
                                                                                                "TA"
                                                                                                "RG"
                                                                                                "ET"
                                                                                                "_F"
                                                                                                "IL"
                                                                                                "E:"
                                                                                                "oc"
                                                                                                "-o"
                                                                                                "mn"
                                                                                                "id"
                                                                                                "ec"
                                                                                                "id"
                                                                                                "e-"
                                                                                                "pr"
                                                                                                "ov"
                                                                                                "id"
                                                                                                "er"
                                                                                                ">") endif()

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
#BUGFIX N - 5 : the Sprint - 24 Scala fixtures under tests / fixtures / scala and
#tests / fixtures / scala_resolver are GENERATED SOURCES
#(scripts / gen_fixtures_scala.py).test_sprint24 runs `oc check`/`oc graph`
#against them, so a source archive shipped without the generated trees
#failed 1 of its 10 checks.The suite now regenerates them on demand:
#when the sentinel fixture is missing at build time, the generator runs
#before the test target is built.When the fixtures are present(git
#checkouts, re - runs) nothing happens-- byte - identical sources in, the
#determinism contract is untouched.
#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
                                                                                            find_program(OC_PYTHON_EXECUTABLE python3) if (
                                                                                                OC_PYTHON_EXECUTABLE
                                                                                                    AND NOT EXISTS
                                                                                                "${"
                                                                                                "CM"
                                                                                                "AK"
                                                                                                "E_"
                                                                                                "SO"
                                                                                                "UR"
                                                                                                "CE"
                                                                                                "_D"
                                                                                                "IR"
                                                                                                "}/"
                                                                                                "te"
                                                                                                "st"
                                                                                                "s/"
                                                                                                "fi"
                                                                                                "xt"
                                                                                                "ur"
                                                                                                "es"
                                                                                                "/s"
                                                                                                "ca"
                                                                                                "la"
                                                                                                "/d"
                                                                                                "ec"
                                                                                                "ls"
                                                                                                ".s"
                                                                                                "ca"
                                                                                                "l"
                                                                                                "a")
                                                                                                add_custom_target(
                                                                                                    oc_scala_fixtures COMMAND
                                                                                                        ${CMAKE_COMMAND} -
                                                                                                    E echo
                                                                                                    "Generating Scala fixtures (BUGFIX N-5, gen_fixtures_scala.py)" COMMAND
                                                                                                        $ {
                                                                                                            OC_PYTHON_EXECUTABLE
                                                                                                        } "${CMAKE_SOURCE_DIR}/scripts/gen_fixtures_scala.py" WORKING_DIRECTORY
                                                                                                          "${CMAKE_SOURCE_DIR}/scripts" VERBATIM)
                                                                                                    add_dependencies(
                                                                                                        test_sprint24
                                                                                                            oc_scala_fixtures)
                                                                                                        endif()
