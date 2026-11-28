#include "oc/cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oc/compat.h"
#include "oc/reportdiff.h"
#include "oc/spine.h"
#include "oc/coverage.h"
#include "oc/discover.h"
#include "oc/dup.h"
#include "oc/finding.h"
#include "oc/graph.h"
#include "oc/imports.h"
#include "oc/json.h"
#include "oc/lsp.h"
#include "oc/mcp.h"
#include "oc/mutate.h"
#include "oc/pack.h"
#include "oc/parser.h"
#include "oc/pass.h"
#include "oc/report.h"
#include "oc/scan.h"
#include "oc/surface.h"
#include "oc/triage.h"

/*
 * OmniCheck CLI (working name; see docs/SPRINT-0.md for sprint scope).
 *
 * Commands:
 *   oc check [paths...]   analyze files, emit deterministic JSON report
 *   oc graph [paths...]   dump the unified graph as JSON
 *   oc packs              list loaded language packs as JSON
 *
 * Exit codes: 0 = clean, 1 = findings at/above --fail-on, 2 = tool error
 * or files that did not parse cleanly (see parse_errors in the report).
 */

static void print_usage(FILE *out) {
    fprintf(out,
            "omnicheck %s -- deterministic multi-language static analysis\n"
            "\n"
            "Usage:\n"
            "  oc check [paths...] [options]\n"
            "  oc graph [paths...] [--packs-dir DIR] [--coverage FILE]...\n"
            "  oc packs [--packs-dir DIR]\n"
            "  oc lsp [--packs-dir DIR] [--config K=V]      LSP 3.17 server over stdio\n"
            "  oc mcp [--packs-dir DIR] [--config K=V]      MCP tools server over stdio\n"
            "  oc compat --baseline DIR --current DIR       breaking-change gate (WBS 4.6)\n"
            "  oc diff --baseline R1.json --current R2.json introduced/resolved/persistent\n"
            "      report diff by fingerprint; --semantic adds advisory kernel scores (T3)\n"
            "  oc brain create|record|advise|info|entries|discard|reset|save\n"
            "      advisory experience memory (T4)\n"
            "  oc pin create --request JSON [--class C]   decision pin (T5)\n"
            "  oc pin replay --id PIN | oc replay --id PIN  replay per REPLAY.md\n"
            "  oc pin list                                  deterministic listing\n"
            "  oc mutate [paths...] --test-cmd CMD          mutation engine (WBS 4.5)\n"
            "  oc --version | --help\n"
            "\n"
            "Options:\n"
            "  --packs-dir DIR   language pack directory (default: $OC_PACKS_DIR or ./packs)\n"
            "  --config K=V      pass option, e.g. --config OC001.max_lines=40\n"
            "  --coverage FILE   compose a coverage report (Istanbul, V8 or coverage.py\n"
            "                    JSON, go test -coverprofile text); repeatable;\n"
            "                    enables OC007 and graph coverage\n"
            "                    fields plus the CRAP leaderboard\n"
            "  --osv-snapshot FILE  vendored OSV snapshot for OC009 dep-vuln\n"
            "  --triage          enable the OmniDecide rank-only sidecar (fail-open)\n"
            "  --triage-embedded run triage with the in-process embedded kernel\n"
            "                    (byte-identical protocol-v0; the sidecar keeps\n"
            "                    absolute precedence when OC_OMNIDECIDE_BIN is set)\n"
            "  --triage-explain  attach the 9 kernel rationale channels per\n"
            "                    finding (requires --triage-embedded; fail-open)\n"
            "  --triage-brain    consult the brain memory for advisory\n"
            "                    experience-adjusted ranks (fail-open)\n"
            "  --triage-lexicon FILE  kernel lexicon pack for triage (repeatable;\n"
            "                    sha256 recorded in the report; a kernel refusal\n"
            "                    is loud)\n"
            "  --triage-model FILE    kernel scoring-model file (128-byte v1);\n"
            "                    sha256 recorded; a kernel refusal is loud\n"
            "  --fail-on LEVEL   info|warn|error (default warn) exit-1 threshold\n"
            "  --format FMT      json|human|llm (default json; llm = kernel-minified\n"
            "                    Markdown with token estimates, T6)\n"
            "\n"
            "mutate options:\n"
            "  --test-cmd CMD    suite command run per mutant (required)\n"
            "  --timeout S       per-mutant timeout in seconds (default 30)\n"
            "  --cache FILE      per-mutant verdict cache (content-hash keyed)\n"
            "  --max-mutants N   cap executed mutants (0 = all; default 0)\n"
            "\n"
            "Exit codes: 0 clean, 1 findings >= threshold (check) or BREAKING\n"
            "            (compat) or surviving mutants (mutate),\n"
            "            2 tool error or file(s) that failed to parse cleanly.\n",
            OC_VERSION_STRING);
}

static int fail_with_status(const oc_status *st) {
    if (st && st->msg[0])
        fprintf(stderr, "omnicheck: error: %s\n", st->msg);
    else
        fprintf(stderr, "omnicheck: error: %s\n", st ? oc_code_str(st->code) : "unknown");
    return 2;
}

/* --------------------------------------------------------------- report */

typedef struct check_options {
    const char *packs_dir;
    int want_triage;
    int want_triage_embedded;
    int want_triage_explain;
    int want_triage_brain;
    const char *const *triage_lexicons;
    size_t n_triage_lexicons;
    const char *triage_model;
    oc_severity fail_on;
    oc_config cfg;
    const char *const *paths;
    size_t n_paths;
    const char *const *coverage;
    size_t n_coverage;
    const char *osv_snapshot;
    const char *format; /* "json" | "human" */
} check_options;

static int cmd_check(const check_options *opt, oc_status *st) {
    /* Sprint 4: the pipeline lives in oc_scan_run (shared with the LSP
     * and MCP servers); this command renders the result and applies the
     * exit-code policy. Byte output is unchanged. */
    oc_scan_options sopt;
    memset(&sopt, 0, sizeof(sopt));
    sopt.packs_dir = opt->packs_dir;
    sopt.paths = opt->paths;
    sopt.n_paths = opt->n_paths;
    sopt.coverage = opt->coverage;
    sopt.n_coverage = opt->n_coverage;
    sopt.osv_snapshot = opt->osv_snapshot;
    sopt.cfg = &opt->cfg;
    sopt.want_triage = opt->want_triage;
    sopt.want_triage_embedded = opt->want_triage_embedded;
    sopt.want_triage_explain = opt->want_triage_explain;
    sopt.want_triage_brain = opt->want_triage_brain;
    sopt.triage_lexicons = opt->triage_lexicons;
    sopt.n_triage_lexicons = opt->n_triage_lexicons;
    sopt.triage_model = opt->triage_model;

    oc_scan_result res;
    memset(&res, 0, sizeof(res));
    if (oc_scan_run(&sopt, &res, st) != OC_OK)
        return fail_with_status(st);

    int exit_code = 0;
    if (strcmp(opt->format, "human") == 0) {
        oc_report_write_human(stdout, &res);
    } else if (strcmp(opt->format, "llm") == 0) {
        /* T6: kernel-minified Markdown with token estimates. An explicit
         * request on a build without the embedded kernel refuses loud. */
        if (!oc_report_llm_available()) {
            fprintf(stderr,
                    "omnicheck: --format llm requires the embedded OmniDecide "
                    "kernel (rebuild with -DOC_EMBED_OMNIDECIDE=ON)\n");
            exit_code = 2;
        } else {
            oc_report_write_llm(stdout, &res);
            /* Same exit-code policy as the JSON render below. */
            if (res.n_parse_errors > 0)
                exit_code = 2;
            else if (res.findings.n > 0 &&
                     oc_findings_max_severity(&res.findings) >= opt->fail_on)
                exit_code = 1;
        }
    } else {
        oc_jsonw w;
        oc_jsonw_init(&w);
        oc_report_write_json(&w, &res);
        if (w.buf.data)
            fputs(w.buf.data, stdout);
        fputc('\n', stdout);
        oc_jsonw_destroy(&w);
    }

    /* Exit-code precedence (BUG-1 fix): a run with unparsed files can never
     * report "clean" (0) or findings-only (1) -- partial analysis is a tool
     * honesty failure and dominates. M1 kill-switch criterion #2. */
    if (res.n_parse_errors > 0) {
        fprintf(stderr,
                "omnicheck: warning: %zu file(s) did not parse cleanly; "
                "findings cover only the parseable portions (see parse_errors)\n",
                res.n_parse_errors);
        exit_code = 2;
    } else if (res.findings.n > 0 && oc_findings_max_severity(&res.findings) >= opt->fail_on) {
        exit_code = 1;
    }

    oc_scan_result_destroy(&res);
    return exit_code;
}

/* ------------------------------------------------------------------- pin */

/* T5: `oc pin create|add|list|replay` + `oc replay --id`. The spine owns
 * decision semantics and ids; the store is OmniCheck's own document.
 * Own flag namespace: dispatches before the flat parser. */
static int cmd_pin(int argc, char **argv, oc_status *st) {
    /* oc pin create --request JSON [--class C] [--spine-file F]
       oc pin list [--spine-file F]
       oc pin replay --id PIN_ID [--spine-file F]  (the create alias
       "add" is accepted for compatibility with the reference generation) */
    const char *sub = (argc >= 3) ? argv[2] : NULL;
    const char *spine_file = NULL, *request = NULL, *cls = NULL, *id = NULL;
    if (!sub || (strcmp(sub, "add") != 0 && strcmp(sub, "create") != 0 &&
                 strcmp(sub, "list") != 0 && strcmp(sub, "replay") != 0)) {
        fprintf(stderr,
                "omnicheck: pin requires a subcommand: create|replay|list\n");
        return 2;
    }
    for (int i = 3; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--spine-file") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --spine-file requires a path\n");
                return 2;
            }
            spine_file = argv[++i];
        } else if (strcmp(a, "--request") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --request requires a JSON document\n");
                return 2;
            }
            request = argv[++i];
        } else if (strcmp(a, "--class") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --class requires exact|semantic|foreign\n");
                return 2;
            }
            cls = argv[++i];
        } else if (strcmp(a, "--id") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --id requires a pin id\n");
                return 2;
            }
            id = argv[++i];
        } else {
            fprintf(stderr, "omnicheck: unexpected pin argument '%s'\n", a);
            return 2;
        }
    }
    if (strcmp(sub, "list") == 0)
        return oc_spine_cmd_list(spine_file, stdout, st) == OC_OK ? 0 : 2;
    if (strcmp(sub, "replay") == 0) {
        int verdict = 0;
        oc_code rc = oc_spine_cmd_replay(spine_file, id, stdout, &verdict, st);
        if (rc != OC_OK)
            return 2;
        return verdict; /* 0 = exact/semantic/foreign, 1 = mismatch */
    }
    if (!request) {
        fprintf(stderr,
                "omnicheck: pin create requires --request '{\"primitive\":...}'\n");
        return 2;
    }
    return oc_spine_cmd_add(spine_file, request, cls ? cls : "exact", stdout,
                            st) == OC_OK
               ? 0
               : 2;
}

static int cmd_replay(int argc, char **argv, oc_status *st) {
    /* oc replay --id PIN_ID [--spine-file F] */
    const char *spine_file = NULL, *id = NULL;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--spine-file") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --spine-file requires a path\n");
                return 2;
            }
            spine_file = argv[++i];
        } else if (strcmp(a, "--id") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --id requires a pin id\n");
                return 2;
            }
            id = argv[++i];
        } else {
            fprintf(stderr, "omnicheck: unexpected replay argument '%s'\n", a);
            return 2;
        }
    }
    int verdict = 0;
    oc_code rc = oc_spine_cmd_replay(spine_file, id, stdout, &verdict, st);
    if (rc != OC_OK)
        return 2;
    return verdict; /* 0 = exact/semantic/foreign, 1 = mismatch */
}

/* ------------------------------------------------------------------ brain */

/* T4: `oc brain` subcommand dispatcher. Explicit commands fail loud
 * (they are the human interface, not pipeline components). */
static int cmd_brain(int argc, char **argv, oc_status *st) {
    const char *sub = (argc >= 3) ? argv[2] : NULL;
    const char *brain_file = NULL, *report = NULL, *fp = NULL, *outcome = NULL;
    long index = -1;
    int have_index = 0;
    static const char *const SUBS[] = {"create", "record", "advise", "info",
                                       "entries", "discard", "reset", "save"};
    int known = 0;
    for (size_t k = 0; k < sizeof(SUBS) / sizeof(*SUBS); k++)
        if (sub && strcmp(sub, SUBS[k]) == 0)
            known = 1;
    if (!known) {
        fprintf(stderr,
                "omnicheck: brain requires a subcommand: "
                "create|record|advise|info|entries|discard|reset|save\n");
        return 2;
    }
    for (int i = 3; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--brain-file") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --brain-file requires a path\n");
                return 2;
            }
            brain_file = argv[++i];
        } else if (strcmp(a, "--report") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --report requires a path\n");
                return 2;
            }
            report = argv[++i];
        } else if (strcmp(a, "--fingerprint") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --fingerprint requires a value\n");
                return 2;
            }
            fp = argv[++i];
        } else if (strcmp(a, "--outcome") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                        "omnicheck: --outcome requires accepted|fixed|dismissed\n");
                return 2;
            }
            outcome = argv[++i];
        } else if (strcmp(a, "--index") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --index requires a number\n");
                return 2;
            }
            char *end = NULL;
            index = strtol(argv[++i], &end, 10);
            if (!end || end == argv[i] || *end != '\0' || index < 0) {
                fprintf(stderr, "omnicheck: --index requires a non-negative number\n");
                return 2;
            }
            have_index = 1;
        } else {
            fprintf(stderr, "omnicheck: unexpected brain argument '%s'\n", a);
            return 2;
        }
    }
    if (strcmp(sub, "create") == 0)
        return oc_brain_cmd_create(brain_file, stdout, st) == OC_OK ? 0 : 2;
    if (strcmp(sub, "info") == 0)
        return oc_brain_cmd_info(brain_file, stdout, st) == OC_OK ? 0 : 2;
    if (strcmp(sub, "entries") == 0)
        return oc_brain_cmd_entries(brain_file, stdout, st) == OC_OK ? 0 : 2;
    if (strcmp(sub, "reset") == 0)
        return oc_brain_cmd_reset(brain_file, stdout, st) == OC_OK ? 0 : 2;
    if (strcmp(sub, "save") == 0)
        return oc_brain_cmd_save(brain_file, stdout, st) == OC_OK ? 0 : 2;
    if (strcmp(sub, "discard") == 0) {
        if (!have_index) {
            fprintf(stderr,
                    "omnicheck: brain discard requires --index N (see 'oc brain "
                    "entries')\n");
            return 2;
        }
        return oc_brain_cmd_discard(brain_file, index, stdout, st) == OC_OK ? 0 : 2;
    }
    if (strcmp(sub, "advise") == 0) {
        if (!report) {
            fprintf(stderr, "omnicheck: brain advise requires --report REPORT.json\n");
            return 2;
        }
        return oc_brain_cmd_advise(brain_file, report, stdout, st) == OC_OK ? 0 : 2;
    }
    /* record */
    if (!report || !fp || !outcome) {
        fprintf(stderr,
                "omnicheck: brain record requires --report REPORT.json "
                "--fingerprint FP --outcome accepted|fixed|dismissed\n");
        return 2;
    }
    return oc_brain_cmd_record(brain_file, report, fp, outcome, stdout, st) ==
                   OC_OK
               ? 0
               : 2;
}

/* ----------------------------------------------------------------- graph */

static int cmd_graph(const char *packs_dir, const char *const *paths, size_t n_paths,
                     const char *const *coverage, size_t n_coverage, oc_status *st) {
    oc_arena arena;
    oc_arena_init(&arena);
    int exit_code = 0;
    oc_parser *parser = NULL;
    oc_graph graph;
    oc_graph_init(&graph, &arena);
    oc_path_vec files;
    oc_paths_init(&files);
    size_t parse_error_files = 0; /* init before any goto cleanup */
    oc_import_vec imports;
    oc_imports_init(&imports);
    oc_bind_vec binds;
    oc_surface_binds_init(&binds);
    oc_export_vec exports;
    oc_surface_exports_init(&exports);
    oc_hint_vec hints;
    oc_surface_hints_init(&hints);
    oc_coverage cov;
    oc_coverage_init(&cov);

    oc_pack_set packs;
    oc_packs_init(&packs, &arena);
    if (oc_packs_load_dir(&packs, packs_dir, st) != OC_OK) {
        exit_code = fail_with_status(st);
        goto cleanup;
    }

    if (oc_discover_files(paths, n_paths, &packs, &arena, &files, NULL, st) != OC_OK) {
        exit_code = fail_with_status(st);
        goto cleanup;
    }

    parser = oc_parser_new(&arena, st);
    if (!parser) {
        exit_code = fail_with_status(st);
        goto cleanup;
    }

    for (size_t i = 0; i < files.n; i++) {
        const oc_pack *pack = oc_packs_match_path(&packs, files.items[i]);
        if (!pack)
            continue;
        char *data = NULL;
        size_t len = 0;
        if (oc_read_file(files.items[i], &data, &len, st) != OC_OK) {
            exit_code = fail_with_status(st);
            goto cleanup;
        }
        /* SPRINT-21: the C/C++ header routing -- one routing decision
         * per file everywhere (the scan's grammar choice is
         * authoritative), so `oc graph` edges agree with `oc check`. */
        pack = oc_packs_route_source(&packs, files.items[i], data, len,
                                     (const char *const *)files.items, files.n, &arena, NULL);
        if (!pack) {
            free(data);
            continue;
        }
        oc_parser_input in;
        memset(&in, 0, sizeof(in));
        in.path = files.items[i];
        in.source = data;
        in.len = len;
        in.grammar = pack->grammar;
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        out.imports = &imports;
        out.binds = &binds;
        out.exports = &exports;
        out.hints = &hints;
        oc_code rc = oc_parser_parse_file(parser, &in, &graph, &out, st);
        free(data);
        if (rc != OC_OK) {
            exit_code = fail_with_status(st);
            goto cleanup;
        }
        if (out.diag.had_error)
            parse_error_files++;
    }

    long n_edges = oc_imports_resolve_ex_hints(&graph, &imports, &arena, paths, n_paths, &hints);
    if (n_edges < 0) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory resolving imports");
        exit_code = fail_with_status(st);
        goto cleanup;
    }

    if (n_coverage > 0 && oc_coverage_load(&cov, coverage, n_coverage, &arena, st) != OC_OK) {
        exit_code = fail_with_status(st);
        goto cleanup;
    }

    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_graph_write_json(&graph, n_coverage > 0 ? &cov : NULL, &w);
    if (w.buf.data)
        fputs(w.buf.data, stdout);
    fputc('\n', stdout);
    oc_jsonw_destroy(&w);

    if (parse_error_files > 0) {
        fprintf(stderr,
                "omnicheck: warning: %zu file(s) did not parse cleanly; "
                "the graph covers only the parseable portions\n",
                parse_error_files);
        exit_code = 2;
    }

cleanup:
    oc_surface_exports_destroy(&exports);
    oc_surface_binds_destroy(&binds);
    oc_surface_hints_destroy(&hints);
    oc_coverage_destroy(&cov);
    oc_imports_destroy(&imports);
    oc_packs_destroy(&packs);
    oc_paths_destroy(&files);
    oc_graph_destroy(&graph);
    oc_parser_free(parser);
    oc_arena_destroy(&arena);
    return exit_code;
}

/* ----------------------------------------------------------------- packs */

static int cmd_packs(const char *packs_dir, oc_status *st) {
    oc_arena arena;
    oc_arena_init(&arena);
    int exit_code = 0;

    oc_pack_set packs;
    oc_packs_init(&packs, &arena);
    if (oc_packs_load_dir(&packs, packs_dir, st) != OC_OK) {
        exit_code = fail_with_status(st);
        goto cleanup;
    }
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_packs_write_json(&packs, &w);
    if (w.buf.data)
        fputs(w.buf.data, stdout);
    fputc('\n', stdout);
    oc_jsonw_destroy(&w);

cleanup:
    oc_packs_destroy(&packs);
    oc_arena_destroy(&arena);
    return exit_code;
}

/* ------------------------------------------------------------------ main */

/* Appends one path to a dynamically grown array (BUGFIX N-1). The
 * previous fixed 256-slot array SILENTLY DROPPED every path past the
 * cap: `oc check f0 ... f300` analyzed 256 roots with no diagnostic at
 * all. Growth failures are reported loudly and abort with exit 2 --
 * never a silent truncation. */
static int push_path(const char ***paths, size_t *n, size_t *cap, const char *p) {
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 16;
        const char **np = (const char **)realloc(*paths, nc * sizeof(*np));
        if (!np)
            return 0;
        *paths = np;
        *cap = nc;
    }
    (*paths)[(*n)++] = p;
    return 1;
}

int oc_cli_main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (strcmp(cmd, "--version") == 0) {
        printf("omnicheck %s\n", OC_VERSION_STRING);
        return 0;
    }
    if (strcmp(cmd, "check") != 0 && strcmp(cmd, "graph") != 0 && strcmp(cmd, "packs") != 0 &&
        strcmp(cmd, "lsp") != 0 && strcmp(cmd, "mcp") != 0 && strcmp(cmd, "compat") != 0 &&
        strcmp(cmd, "diff") != 0 && strcmp(cmd, "brain") != 0 &&
        strcmp(cmd, "pin") != 0 && strcmp(cmd, "replay") != 0 &&
        strcmp(cmd, "mutate") != 0) {
        fprintf(stderr, "omnicheck: unknown command '%s'\n\n", cmd);
        print_usage(stderr);
        return 2;
    }

    const char *packs_dir = getenv("OC_PACKS_DIR");
    if (!packs_dir || packs_dir[0] == '\0')
        packs_dir = "./packs";

    check_options opt;
    memset(&opt, 0, sizeof(opt));
    opt.packs_dir = packs_dir;
    opt.fail_on = OC_SEV_WARN;
    opt.format = "json";
    oc_config_init(&opt.cfg);

    const char **paths = NULL; /* dynamically grown (BUGFIX N-1) */
    size_t paths_cap = 0;
    const char *cov_files[64];
    const char *default_paths[1] = {"."};
    const char *opt_baseline = NULL, *opt_current = NULL, *opt_test_cmd = NULL;
    int opt_semantic = 0;
    const char *lex_files[16];
    size_t n_lex = 0;
    const char *opt_model = NULL;
    const char *opt_cache = NULL;
    long opt_timeout = 0;
    size_t opt_max_mutants = 0;
    size_t n_paths = 0, n_cov = 0;
    int exit_code = 0;
    oc_status st;
    oc_status_clear(&st);

    /* T5: the pin/replay commands carry their OWN flag namespace
     * (--request, --class, --id, --spine-file): dispatch before the flat
     * parser. */
    if (strcmp(cmd, "pin") == 0) {
        exit_code = cmd_pin(argc, argv, &st);
        if (exit_code == 2 && st.code != OC_OK)
            fail_with_status(&st);
        goto out;
    }
    if (strcmp(cmd, "replay") == 0) {
        exit_code = cmd_replay(argc, argv, &st);
        if (exit_code == 2 && st.code != OC_OK)
            fail_with_status(&st);
        goto out;
    }

    /* T4: the brain commands carry their OWN flag namespace
     * (--brain-file, --report, --fingerprint, --outcome, --index), so
     * they dispatch BEFORE the flat check/mutate flag parser below --
     * the same early-dispatch discipline the reference generation used. */
    if (strcmp(cmd, "brain") == 0) {
        exit_code = cmd_brain(argc, argv, &st);
        if (exit_code == 2 && st.code != OC_OK)
            fail_with_status(&st);
        goto out;
    }

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--packs-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --packs-dir requires a value\n");
                exit_code = 2;
                goto out;
            }
            opt.packs_dir = argv[++i];
        } else if (strcmp(a, "--coverage") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --coverage requires a file path\n");
                exit_code = 2;
                goto out;
            }
            if (n_cov < sizeof(cov_files) / sizeof(cov_files[0]))
                cov_files[n_cov++] = argv[++i];
            else {
                fprintf(stderr, "omnicheck: too many --coverage files (max 64)\n");
                exit_code = 2;
                goto out;
            }
        } else if (strcmp(a, "--osv-snapshot") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --osv-snapshot requires a file path\n");
                exit_code = 2;
                goto out;
            }
            opt.osv_snapshot = argv[++i];
        } else if (strcmp(a, "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --config requires K=V\n");
                exit_code = 2;
                goto out;
            }
            const char *kv = argv[++i];
            const char *eq = strchr(kv, '=');
            if (!eq || eq == kv) {
                fprintf(stderr, "omnicheck: --config expects KEY=VALUE, got '%s'\n", kv);
                exit_code = 2;
                goto out;
            }
            char key[64], val[64];
            size_t klen = (size_t)(eq - kv);
            if (klen >= sizeof(key) || strlen(eq + 1) >= sizeof(val)) {
                fprintf(stderr, "omnicheck: --config key/value too long\n");
                exit_code = 2;
                goto out;
            }
            memcpy(key, kv, klen);
            key[klen] = '\0';
            snprintf(val, sizeof(val), "%s", eq + 1);
            if (!oc_config_set(&opt.cfg, key, val)) {
                fprintf(stderr, "omnicheck: too many --config options\n");
                exit_code = 2;
                goto out;
            }
        } else if (strcmp(a, "--format") == 0) {
            if (i + 1 >= argc ||
                (strcmp(argv[i + 1], "json") != 0 &&
                 strcmp(argv[i + 1], "human") != 0 &&
                 strcmp(argv[i + 1], "llm") != 0)) {
                fprintf(stderr, "omnicheck: --format expects json|human|llm\n");
                exit_code = 2;
                goto out;
            }
            opt.format = argv[++i];
        } else if (strcmp(a, "--triage") == 0) {
            opt.want_triage = 1;
        } else if (strcmp(a, "--triage-embedded") == 0) {
            opt.want_triage = 1;
            opt.want_triage_embedded = 1;
        } else if (strcmp(a, "--triage-explain") == 0) {
            opt.want_triage = 1;
            opt.want_triage_explain = 1;
        } else if (strcmp(a, "--triage-brain") == 0) {
            opt.want_triage = 1;
            opt.want_triage_brain = 1;
        } else if (strcmp(a, "--triage-lexicon") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --triage-lexicon requires a path\n");
                exit_code = 2;
                goto out;
            }
            if (n_lex >= sizeof(lex_files) / sizeof(lex_files[0])) {
                fprintf(stderr,
                        "omnicheck: too many --triage-lexicon options (max %zu)\n",
                        sizeof(lex_files) / sizeof(lex_files[0]));
                exit_code = 2;
                goto out;
            }
            lex_files[n_lex++] = argv[++i];
        } else if (strcmp(a, "--triage-model") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --triage-model requires a path\n");
                exit_code = 2;
                goto out;
            }
            opt_model = argv[++i];
        } else if (strcmp(a, "--baseline") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --baseline requires a directory\n");
                exit_code = 2;
                goto out;
            }
            opt_baseline = argv[++i];
        } else if (strcmp(a, "--current") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --current requires a directory\n");
                exit_code = 2;
                goto out;
            }
            opt_current = argv[++i];
        } else if (strcmp(a, "--semantic") == 0) {
            opt_semantic = 1;
        } else if (strcmp(a, "--test-cmd") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --test-cmd requires a command\n");
                exit_code = 2;
                goto out;
            }
            opt_test_cmd = argv[++i];
        } else if (strcmp(a, "--timeout") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --timeout requires seconds\n");
                exit_code = 2;
                goto out;
            }
            opt_timeout = atol(argv[++i]);
        } else if (strcmp(a, "--cache") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --cache requires a file path\n");
                exit_code = 2;
                goto out;
            }
            opt_cache = argv[++i];
        } else if (strcmp(a, "--max-mutants") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omnicheck: --max-mutants requires a number\n");
                exit_code = 2;
                goto out;
            }
            opt_max_mutants = (size_t)atol(argv[++i]);
        } else if (strcmp(a, "--fail-on") == 0) {
            if (i + 1 >= argc || !oc_severity_from_str(argv[i + 1], &opt.fail_on)) {
                fprintf(stderr, "omnicheck: --fail-on expects info|warn|error\n");
                exit_code = 2;
                goto out;
            }
            i++;
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "omnicheck: unknown option '%s'\n", a);
            exit_code = 2;
            goto out;
        } else {
            if (!push_path(&paths, &n_paths, &paths_cap, a)) {
                fprintf(stderr, "omnicheck: out of memory recording path '%s'\n", a);
                exit_code = 2;
                goto out;
            }
        }
    }

    if (strcmp(cmd, "packs") == 0) {
        exit_code = cmd_packs(opt.packs_dir, &st);
        goto out;
    }

    /* LSP server (Sprint 4, WBS 3.3): JSON-RPC over stdio until the
     * client exits. Config and packs dir carry over from the CLI. */
    if (strcmp(cmd, "lsp") == 0) {
        oc_lsp_options lopt;
        memset(&lopt, 0, sizeof(lopt));
        lopt.packs_dir = opt.packs_dir;
        lopt.cfg = &opt.cfg;
        exit_code = oc_lsp_run(&lopt, stdin, stdout, &st);
        if (exit_code == 2)
            fail_with_status(&st);
        goto out;
    }

    /* MCP server (Sprint 4, WBS 3.4): newline-delimited JSON-RPC tools
     * server over stdio until the client closes stdin. */
    if (strcmp(cmd, "mcp") == 0) {
        oc_mcp_options mopt;
        memset(&mopt, 0, sizeof(mopt));
        mopt.packs_dir = opt.packs_dir;
        mopt.cfg = &opt.cfg;
        exit_code = oc_mcp_run(&mopt, stdin, stdout, &st);
        if (exit_code == 2)
            fail_with_status(&st);
        goto out;
    }

    /* Breaking-change gate (Sprint 5, WBS 4.6): pure surface diff of two
     * analysis roots; exit 1 on BREAKING makes it a drop-in CI gate. */
    if (strcmp(cmd, "compat") == 0) {
        if (!opt_baseline || !opt_current) {
            fprintf(stderr, "omnicheck: compat requires --baseline DIR and --current DIR\n");
            exit_code = 2;
            goto out;
        }
        exit_code = oc_compat_run(opt_baseline, opt_current, opt.packs_dir, stdout, &st);
        if (exit_code == 2)
            fail_with_status(&st);
        goto out;
    }

    /* T3: introduced/resolved/persistent diff between two reports; exit 1
     * gates on introduced+resolved (the CI gate), exit 2 is a tool error.
     * --semantic adds advisory kernel scores (never affects the gate). */
    if (strcmp(cmd, "diff") == 0) {
        if (!opt_baseline || !opt_current) {
            fprintf(stderr,
                    "omnicheck: diff requires --baseline REPORT.json and "
                    "--current REPORT.json\n");
            exit_code = 2;
            goto out;
        }
        int semantic_changes = 0;
        oc_code drc = oc_reportdiff_run(opt_baseline, opt_current, opt_semantic,
                                        &semantic_changes, stdout, &st);
        if (drc != OC_OK) {
            exit_code = fail_with_status(&st);
            goto out;
        }
        exit_code = semantic_changes ? 1 : 0;
        goto out;
    }

    /* Mutation engine (Sprint 5, WBS 4.5): operator-set mutants run
     * against the suite; exit 1 when survivors exist (gate semantics). */
    if (strcmp(cmd, "mutate") == 0) {
        if (n_paths == 0 && !push_path(&paths, &n_paths, &paths_cap, default_paths[0])) {
            fprintf(stderr, "omnicheck: out of memory recording default path\n");
            exit_code = 2;
            goto out;
        }
        oc_mutate_options mopt;
        memset(&mopt, 0, sizeof(mopt));
        mopt.packs_dir = opt.packs_dir;
        mopt.paths = paths;
        mopt.n_paths = n_paths;
        mopt.test_cmd = opt_test_cmd;
        mopt.timeout_s = opt_timeout;
        mopt.cache_path = opt_cache;
        mopt.max_mutants = opt_max_mutants;
        mopt.coverage = cov_files;
        mopt.n_coverage = n_cov;
        oc_mutate_report rep;
        oc_code mrc = oc_mutate_run(&mopt, &rep, stdout, &st);
        /* destroy FIRST (safe on error paths too: partially collected
         * mutants own a heap array; LSan caught the leak) */
        oc_mutants_destroy(&rep.mutants);
        if (mrc != OC_OK) {
            exit_code = fail_with_status(&st);
            goto out;
        }
        exit_code = rep.survived > 0 ? 1 : 0;
        goto out;
    }

    if (n_paths == 0 && strcmp(cmd, "packs") != 0 && strcmp(cmd, "lsp") != 0 &&
        strcmp(cmd, "mcp") != 0 && strcmp(cmd, "compat") != 0 && strcmp(cmd, "mutate") != 0) {
        if (!push_path(&paths, &n_paths, &paths_cap, default_paths[0])) {
            fprintf(stderr, "omnicheck: out of memory recording default path\n");
            exit_code = 2;
            goto out;
        }
    }

    if (strcmp(cmd, "check") == 0) {
        /* T7: kernel resources without an active triage are inert -- an
         * explicit flag that cannot do its job is a usage error. */
        if ((n_lex > 0 || (opt_model && opt_model[0])) && !opt.want_triage) {
            fprintf(stderr,
                    "omnicheck: --triage-lexicon/--triage-model require triage "
                    "(--triage or --triage-embedded)\n");
            exit_code = 2;
            goto out;
        }
        opt.paths = paths;
        opt.n_paths = n_paths;
        opt.coverage = cov_files;
        opt.n_coverage = n_cov;
        opt.triage_lexicons = lex_files;
        opt.n_triage_lexicons = n_lex;
        opt.triage_model = opt_model;
        exit_code = cmd_check(&opt, &st);
    } else {
        exit_code = cmd_graph(opt.packs_dir, paths, n_paths, cov_files, n_cov, &st);
    }

out:
    free(paths); /* heap array (BUGFIX N-1); NULL when never grown */
    oc_config_destroy(&opt.cfg);
    return exit_code;
}

int main(int argc, char **argv) {
    return oc_cli_main(argc, argv);
}
