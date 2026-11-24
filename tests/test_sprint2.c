/* Sprint 2 (WBS 3.x) tests: OBS-2 unified definition spans, OBS-1
 * counted discovery skips, resolver v2 (tsconfig paths aliases), module
 * surface passes (OC005 unused-export, OC006 unreferenced-module), and
 * the flagship CRAP-ms composition (OC007 over Istanbul / coverage.py).
 * E2E against the real oc binary; units cover JSONC, entry globs and
 * coverage composition. */
#include "oc/arena.h"
#include "oc/common.h"
#include "oc/coverage.h"
#include "oc/json.h"
#include "oc/surface.h"
#include "testkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the built oc binary"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1)
        return -1;
    if (WIFEXITED(rc))
        return WEXITSTATUS(rc);
    return -1;
}

static oc_json *load_json(oc_arena *a, const char *path) {
    char *data = NULL;
    size_t len = 0;
    if (oc_read_file(path, &data, &len, NULL) != OC_OK)
        return NULL;
    oc_json *v = oc_json_parse(a, data, len, NULL);
    free(data);
    return v;
}

static size_t count_findings(const oc_json *report, const char *rule) {
    const oc_json *findings = oc_json_get(report, "findings");
    size_t n = oc_json_size(findings), hits = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(oc_json_as_str(oc_json_get(findings->items[i], "rule"), ""), rule) == 0)
            hits++;
    }
    return hits;
}

static size_t count_edges(const oc_json *graph, const char *kind) {
    const oc_json *edges = oc_json_get(graph, "edges");
    size_t n = oc_json_size(edges), hits = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(oc_json_as_str(oc_json_get(edges->items[i], "kind"), ""), kind) == 0)
            hits++;
    }
    return hits;
}

/* Finds a function node in a graph dump; returns its node object. */
static const oc_json *graph_func(const oc_json *graph, const char *name) {
    const oc_json *nodes = oc_json_get(graph, "nodes");
    for (size_t i = 0; i < oc_json_size(nodes); i++) {
        const oc_json *n = nodes->items[i];
        if (strcmp(oc_json_as_str(oc_json_get(n, "kind"), ""), "function") == 0 &&
            strcmp(oc_json_as_str(oc_json_get(n, "name"), ""), name) == 0)
            return n;
    }
    return NULL;
}

/* ------------------------------------------------------------ units -- */

static void unit_jsonc(void) {
    oc_arena a;
    oc_arena_init(&a);
    static const char tsconfig[] = "{\n"
                                   "  // line comment\n"
                                   "  /* block\n"
                                   "     comment */\n"
                                   "  \"compilerOptions\": {\n"
                                   "    \"baseUrl\": \".\",  // same-line comment\n"
                                   "    \"paths\": {\n"
                                   "      \"@lib/*\": [\"src/lib/*\"],\n"
                                   "      \"tricky\": [\"a,]b\"],\n"
                                   "    },\n"
                                   "  },\n"
                                   "}\n";
    oc_json *root = oc_json_parse_lenient(&a, tsconfig, strlen(tsconfig), NULL);
    CHECK(root != NULL);
    const oc_json *co = oc_json_get(root, "compilerOptions");
    CHECK(co != NULL);
    CHECK(oc_json_get(co, "baseUrl") != NULL);
    const oc_json *paths = oc_json_get(co, "paths");
    CHECK(paths != NULL);
    const oc_json *tricky = oc_json_get(paths, "tricky");
    CHECK(tricky != NULL && oc_json_size(tricky) == 1);
    CHECK_STR_EQ(oc_json_as_str(tricky ? tricky->items[0] : NULL, ""), "a,]b");
    CHECK(oc_json_size(oc_json_get(paths, "@lib/*")) == 1);
    /* Strict parser must REJECT the same text (comments are JSONC-only). */
    CHECK(oc_json_parse(&a, tsconfig, strlen(tsconfig), NULL) == NULL);
    oc_arena_destroy(&a);
}

static void unit_globs(void) {
    CHECK(oc_glob_match_entry("**/index.ts", "a/b/index.ts"));
    CHECK(oc_glob_match_entry("**/index.ts", "index.ts"));
    CHECK(!oc_glob_match_entry("**/index.ts", "a/index.js"));
    CHECK(!oc_glob_match_entry("**/index.ts", "a/b/indexx.ts"));
    CHECK(oc_glob_match_entry("main.py", "main.py"));
    CHECK(oc_glob_match_entry("main.py", "src/main.py"));
    CHECK(!oc_glob_match_entry("main.py", "src/mainx.py"));
    CHECK(oc_glob_match_entry("**/__init__.py", "pkg/sub/__init__.py"));
    CHECK(oc_glob_match_entry("**/__main__.py", "pkg/__main__.py"));
    CHECK(!oc_glob_match_entry("**/__main__.py", "pkg/main.py"));
    CHECK(oc_glob_match_entry("**/lib/*.js", "lib/a.js"));
    CHECK(!oc_glob_match_entry("**/lib/*.js", "lib/sub/a.js"));
}

/* Istanbul + coverage.py composition, suffix matching, ambiguity. */
static void unit_coverage(void) {
    oc_arena a;
    oc_arena_init(&a);

    FILE *f1 = fopen("/tmp/oc_s2_ist.json", "w");
    CHECK(f1 != NULL);
    fprintf(f1, "{\"/repo/lib/hot.js\": {\"path\": \"/repo/lib/hot.js\", "
                "\"statementMap\": {\"0\": {\"start\": {\"line\": 2}}, "
                "\"1\": {\"start\": {\"line\": 3}}, "
                "\"2\": {\"start\": {\"line\": 4}}}, "
                "\"s\": {\"0\": 1, \"1\": 0, \"2\": 1}}, "
                "\"/repo/other/lib/x.js\": {\"path\": \"/repo/other/lib/x.js\", "
                "\"statementMap\": {\"0\": {\"start\": {\"line\": 1}}}, \"s\": {\"0\": 1}}, "
                "\"/repo/second/lib/x.js\": {\"path\": \"/repo/second/lib/x.js\", "
                "\"statementMap\": {\"0\": {\"start\": {\"line\": 1}}}, \"s\": {\"0\": 1}}}");
    fclose(f1);

    oc_coverage cov;
    oc_coverage_init(&cov);
    const char *files1[1] = {"/tmp/oc_s2_ist.json"};
    CHECK(oc_coverage_load(&cov, files1, 1, &a, NULL) == OC_OK);
    CHECK(cov.files_loaded == 3);

    size_t anchors = 0, covered = 0;
    /* Exact-path match. */
    CHECK(oc_coverage_lookup(&cov, "/repo/lib/hot.js", 1, 10, &anchors, &covered) == 1);
    CHECK(anchors == 3 && covered == 2);
    /* Span-restricted: anchor on line 2 only. */
    CHECK(oc_coverage_lookup(&cov, "/repo/lib/hot.js", 2, 2, &anchors, &covered) == 1);
    CHECK(anchors == 1 && covered == 1);
    /* Unique suffix match onto the analyzed-relative path. */
    CHECK(oc_coverage_lookup(&cov, "lib/hot.js", 1, 10, &anchors, &covered) == 1);
    CHECK(anchors == 3 && covered == 2);
    /* Ambiguous suffix matches nothing (two candidates). */
    CHECK(oc_coverage_lookup(&cov, "lib/x.js", 1, 10, &anchors, &covered) == 0);
    /* Absent file: no match. */
    CHECK(oc_coverage_lookup(&cov, "lib/absent.js", 1, 10, &anchors, &covered) == 0);
    oc_coverage_destroy(&cov);

    /* coverage.py format. */
    FILE *f2 = fopen("/tmp/oc_s2_py.json", "w");
    CHECK(f2 != NULL);
    fprintf(f2, "{\"files\": {\"/repo/pkg/mod.py\": {\"executed_lines\": [1, 2, 5], "
                "\"missing_lines\": [4]}}, \"totals\": {\"percent_covered\": 75.0}}");
    fclose(f2);
    oc_coverage cov2;
    oc_coverage_init(&cov2);
    const char *files2[1] = {"/tmp/oc_s2_py.json"};
    CHECK(oc_coverage_load(&cov2, files2, 1, &a, NULL) == OC_OK);
    CHECK(cov2.files_loaded == 1);
    /* anchors = executed + missing union; covered = executed only. */
    CHECK(oc_coverage_lookup(&cov2, "/repo/pkg/mod.py", 1, 10, &anchors, &covered) == 1);
    CHECK(anchors == 4 && covered == 3);
    oc_coverage_destroy(&cov2);

    oc_arena_destroy(&a);
}

/* --------------------------------------------------------------- main -- */

int main(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char fixtures[512], packs[512];
    snprintf(fixtures, sizeof(fixtures), "%s/tests/fixtures_sprint2", root);
    snprintf(packs, sizeof(packs), "%s/packs", root);

    char cmd[4096];
    char covjs[1024];
    snprintf(covjs, sizeof(covjs), "/tmp/oc_s2_istanbul_run.json");
    oc_arena arena;
    oc_arena_init(&arena);

    unit_jsonc();
    unit_globs();
    unit_coverage();

    /* Runtime-built Istanbul fixture for the CRAP-ms e2e: absolute paths
     * inside the coverage report exercise "/"-boundary suffix matching. */
    {
        FILE *f = fopen(covjs, "w");
        CHECK(f != NULL);
        fprintf(f,
                "{\"%s/tests/fixtures_sprint2/cov_js/hot.js\": {"
                "\"path\": \"%s/tests/fixtures_sprint2/cov_js/hot.js\", "
                "\"statementMap\": {"
                "\"0\": {\"start\": {\"line\": 2}}, "
                "\"1\": {\"start\": {\"line\": 3}}, "
                "\"2\": {\"start\": {\"line\": 4}}, "
                "\"3\": {\"start\": {\"line\": 6}}, "
                "\"4\": {\"start\": {\"line\": 10}}, "
                "\"5\": {\"start\": {\"line\": 11}}}, "
                "\"s\": {\"0\": 0, \"1\": 0, \"2\": 0, \"3\": 0, \"4\": 1, \"5\": 1}}}",
                root, root);
        fclose(f);
    }

    /* 1) OBS-2: the canonical span is the DEFINITION EXTENT for every
     *    language. Python def line must be inside [def_start..def_end]. */
    snprintf(cmd, sizeof(cmd),
             "%s graph %s/cov_js --packs-dir %s > /tmp/oc_s2_span.json 2>/dev/null", bin, fixtures,
             packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *span = load_json(&arena, "/tmp/oc_s2_span.json");
    CHECK(span != NULL);
    const oc_json *cold = graph_func(span, "cold");
    const oc_json *warm = graph_func(span, "warm");
    CHECK(cold != NULL && warm != NULL);
    if (cold) {
        size_t ds = (size_t)oc_json_as_num(oc_json_get(cold, "def_start"), -1);
        size_t de = (size_t)oc_json_as_num(oc_json_get(cold, "def_end"), -1);
        size_t dl = (size_t)oc_json_as_num(oc_json_get(cold, "decl_line"), -1);
        CHECK(ds == 1 && de == 7 && dl == 1); /* def line .. closing brace */
    }
    if (warm) {
        size_t ds = (size_t)oc_json_as_num(oc_json_get(warm, "def_start"), -1);
        CHECK(ds == 9); /* def/blank structure: warm starts at line 9 */
    }

    /* 2) Module surface JS: OC005 flags unusedFn only; OC006 flags the
     *    wild + orphan files; barrels/shebang/tests stay silent. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/surface_js --packs-dir %s > /tmp/oc_s2_sjs.json 2>/dev/null", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    oc_json *sjs = load_json(&arena, "/tmp/oc_s2_sjs.json");
    CHECK(sjs != NULL);
    CHECK(count_findings(sjs, "OC005") == 1);
    CHECK(count_findings(sjs, "OC006") == 2);
    {
        const oc_json *fs = oc_json_get(sjs, "findings");
        int saw_unusedfn = 0;
        for (size_t i = 0; i < oc_json_size(fs); i++) {
            const oc_json *f = fs->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(f, "rule"), ""), "OC005") == 0) {
                CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(f, "symbol"), ""), "unusedFn");
                saw_unusedfn = 1;
            }
        }
        CHECK(saw_unusedfn);
    }

    /* 3) Module surface Python: __all__-less top-level surface, underscore
     *    private convention, package __init__ as entry point. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/surface_py --packs-dir %s > /tmp/oc_s2_spy.json 2>/dev/null", bin,
             fixtures, packs);
    CHECK(run_cmd(cmd) == 1);
    oc_json *spy = load_json(&arena, "/tmp/oc_s2_spy.json");
    CHECK(spy != NULL);
    CHECK(count_findings(spy, "OC005") == 1);
    CHECK(count_findings(spy, "OC006") == 1);
    {
        const oc_json *fs = oc_json_get(spy, "findings");
        for (size_t i = 0; i < oc_json_size(fs); i++) {
            const oc_json *f = fs->items[i];
            const char *sym = oc_json_as_str(oc_json_get(f, "symbol"), "");
            if (strcmp(oc_json_as_str(oc_json_get(f, "rule"), ""), "OC005") == 0)
                CHECK_STR_EQ(sym, "unused_py");
            if (strcmp(oc_json_as_str(oc_json_get(f, "rule"), ""), "OC006") == 0)
                CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(f, "file"), ""), "orphan.py");
        }
    }

    /* 4) Resolver v2: the @lib/util alias resolves through tsconfig.json. */
    snprintf(cmd, sizeof(cmd),
             "%s graph %s/alias --packs-dir %s > /tmp/oc_s2_alias.json 2>/dev/null", bin, fixtures,
             packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *alias = load_json(&arena, "/tmp/oc_s2_alias.json");
    CHECK(alias != NULL);
    CHECK(count_edges(alias, "imports") == 1);

    /* 5) OBS-1: dot-entries and ignored dirs are counted, not silent. */
    snprintf(cmd, sizeof(cmd), "%s check %s/dots --packs-dir %s > /tmp/oc_s2_dots.json 2>/dev/null",
             bin, fixtures, packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *dots = load_json(&arena, "/tmp/oc_s2_dots.json");
    CHECK(dots != NULL);
    {
        const oc_json *meta = oc_json_get(dots, "meta");
        CHECK(oc_json_as_num(oc_json_get(meta, "files_skipped_dot_entries"), -1) == 2);
        CHECK(oc_json_as_num(oc_json_get(meta, "files_skipped_ignored_dirs"), -1) == 1);
        CHECK(oc_json_as_num(oc_json_get(meta, "files_skipped_unknown_type"), -1) == 1);
        CHECK(oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1) == 0);
    }

    /* 6) CRAP-ms e2e (Istanbul): cold 0% -> CRAP=6 > 5; warm 100% silent. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/cov_js --packs-dir %s --coverage %s --config OC007.max_crap=5 "
             "> /tmp/oc_s2_crap.json 2>/dev/null",
             bin, fixtures, packs, covjs);
    CHECK(run_cmd(cmd) == 1);
    oc_json *crap = load_json(&arena, "/tmp/oc_s2_crap.json");
    CHECK(crap != NULL);
    CHECK(count_findings(crap, "OC007") == 1);
    {
        const oc_json *meta = oc_json_get(crap, "meta");
        const oc_json *cv = oc_json_get(meta, "coverage");
        CHECK(oc_json_as_num(oc_json_get(cv, "files_loaded"), -1) == 1);
        CHECK(oc_json_as_num(oc_json_get(cv, "files_matched"), -1) == 1);
        CHECK(oc_json_as_num(oc_json_get(cv, "functions_total"), -1) == 2);
        CHECK(oc_json_as_num(oc_json_get(cv, "functions_measured"), -1) == 2);
        const oc_json *fs = oc_json_get(crap, "findings");
        int saw_cold = 0;
        for (size_t i = 0; i < oc_json_size(fs); i++) {
            const oc_json *f = fs->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(f, "rule"), ""), "OC007") == 0) {
                CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(f, "message"), ""), "CRAP=6");
                CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(f, "message"), ""), "cold");
                saw_cold = 1;
            }
        }
        CHECK(saw_cold);
    }

    /* 7) CRAP-ms on graph output: coverage fields appear per function. */
    snprintf(cmd, sizeof(cmd),
             "%s graph %s/cov_js --packs-dir %s --coverage %s > /tmp/oc_s2_gcov.json "
             "2>/dev/null",
             bin, fixtures, packs, covjs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *gcov = load_json(&arena, "/tmp/oc_s2_gcov.json");
    CHECK(gcov != NULL);
    {
        const oc_json *c2 = graph_func(gcov, "cold");
        const oc_json *w2 = graph_func(gcov, "warm");
        CHECK(c2 && w2);
        if (c2) {
            CHECK(oc_json_as_num(oc_json_get(c2, "coverage_pct"), -1) == 0);
            CHECK(oc_json_as_num(oc_json_get(c2, "crap"), -1) == 6);
            CHECK(oc_json_as_num(oc_json_get(c2, "coverage_anchors"), -1) == 4);
        }
        if (w2) {
            CHECK(oc_json_as_num(oc_json_get(w2, "coverage_pct"), -1) == 100);
            CHECK(oc_json_as_num(oc_json_get(w2, "crap"), -1) == 1);
        }
    }

    /* 8) CRAP-ms e2e (coverage.py). */
    {
        FILE *f = fopen("/tmp/oc_s2_pycov_run.json", "w");
        CHECK(f != NULL);
        fprintf(f,
                "{\"files\": {\"%s/tests/fixtures_sprint2/cov_py/mod.py\": {"
                "\"executed_lines\": [1, 2], \"missing_lines\": [4, 5]}}}",
                root);
        fclose(f);
        snprintf(cmd, sizeof(cmd),
                 "%s check %s/cov_py --packs-dir %s --coverage /tmp/oc_s2_pycov_run.json "
                 "--config OC007.max_crap=1 > /tmp/oc_s2_pycrap.json 2>/dev/null",
                 bin, fixtures, packs);
        CHECK(run_cmd(cmd) == 1);
        oc_json *pc = load_json(&arena, "/tmp/oc_s2_pycrap.json");
        CHECK(pc != NULL);
        CHECK(count_findings(pc, "OC007") == 1);
        {
            const oc_json *meta = oc_json_get(pc, "meta");
            const oc_json *cv = oc_json_get(meta, "coverage");
            CHECK(oc_json_as_num(oc_json_get(cv, "functions_measured"), -1) == 2);
            const oc_json *fs = oc_json_get(pc, "findings");
            for (size_t i = 0; i < oc_json_size(fs); i++) {
                const oc_json *f2 = fs->items[i];
                if (strcmp(oc_json_as_str(oc_json_get(f2, "rule"), ""), "OC007") == 0)
                    CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(f2, "message"), ""),
                                       "coverage 0%");
            }
        }
    }

    /* 9) No --coverage: no OC007 findings, no coverage fields in graph. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/cov_js --packs-dir %s > /tmp/oc_s2_nocov.json 2>/dev/null", bin, fixtures,
             packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *nocov = load_json(&arena, "/tmp/oc_s2_nocov.json");
    CHECK(nocov != NULL);
    CHECK(count_findings(nocov, "OC007") == 0);
    {
        const oc_json *meta = oc_json_get(nocov, "meta");
        CHECK(oc_json_as_num(oc_json_get(oc_json_get(meta, "coverage"), "files_loaded"), -1) == 0);
    }

    /* 10) Determinism: the composed run is byte-identical on rerun. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/cov_js --packs-dir %s --coverage %s --config OC007.max_crap=5 "
             "> /tmp/oc_s2_crap2.json 2>/dev/null",
             bin, fixtures, packs, covjs);
    CHECK(run_cmd(cmd) == 1);
    {
        char *d1 = NULL, *d2 = NULL;
        size_t l1 = 0, l2 = 0;
        CHECK(oc_read_file("/tmp/oc_s2_crap.json", &d1, &l1, NULL) == OC_OK);
        CHECK(oc_read_file("/tmp/oc_s2_crap2.json", &d2, &l2, NULL) == OC_OK);
        CHECK(l1 == l2 && (l1 == 0 || memcmp(d1, d2, l1) == 0));
        free(d1);
        free(d2);
    }

    oc_arena_destroy(&arena);
    TEST_SUMMARY();
}
