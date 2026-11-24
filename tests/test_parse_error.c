/* Parse-error surfacing tests (BUG-1 fix; M1 kill-switch criterion #2):
 * a file that fails to parse must NEVER produce a silent clean run.
 * Unit level: the parser API reports ERROR/MISSING-node diagnostics.
 * E2E level: `oc check` / `oc graph` exit 2, name the files in the
 * report, stay deterministic, and keep clean files analyzable. */
#include "oc/arena.h"
#include "oc/common.h"
#include "oc/graph.h"
#include "oc/json.h"
#include "oc/parser.h"
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

static int files_equal(const char *p1, const char *p2) {
    char *d1 = NULL, *d2 = NULL;
    size_t l1 = 0, l2 = 0;
    if (oc_read_file(p1, &d1, &l1, NULL) != OC_OK)
        return 0;
    if (oc_read_file(p2, &d2, &l2, NULL) != OC_OK) {
        free(d1);
        return 0;
    }
    int eq = l1 == l2 && (l1 == 0 || memcmp(d1, d2, l1) == 0);
    free(d1);
    free(d2);
    return eq;
}

/* -------- unit: parser API on in-memory broken and clean sources ------- */
static void unit_parse_diagnostics(void) {
    static const char broken_js[] = "function broken( {\n  return 1;\n}\n";
    static const char broken_py[] = "def broken(:\n    pass\n";
    static const char clean_js[] = "function fine(a) {\n  return a + 1;\n}\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);

    oc_parser_input in;
    memset(&in, 0, sizeof(in));
    oc_parse_out out;
    memset(&out, 0, sizeof(out)); /* caller-owned init (parser.h contract) */

    in.path = "unit/broken.js";
    in.source = broken_js;
    in.len = strlen(broken_js);
    in.grammar = "javascript";
    CHECK(oc_parser_parse_file(p, &in, &g, &out, &st) == OC_OK);
    CHECK(out.diag.had_error == 1);
    CHECK(out.diag.error_nodes >= 1);
    CHECK(out.diag.first_line == 1);
    CHECK(out.diag.first_col >= 1);

    in.path = "unit/broken.py";
    in.source = broken_py;
    in.len = strlen(broken_py);
    in.grammar = "python";
    CHECK(oc_parser_parse_file(p, &in, &g, &out, &st) == OC_OK);
    CHECK(out.diag.had_error == 1);
    CHECK(out.diag.error_nodes >= 1);
    CHECK(out.diag.first_line == 1);
    CHECK(out.diag.first_col >= 1);

    in.path = "unit/clean.js";
    in.source = clean_js;
    in.len = strlen(clean_js);
    in.grammar = "javascript";
    CHECK(oc_parser_parse_file(p, &in, &g, &out, &st) == OC_OK);
    CHECK(out.diag.had_error == 0);
    CHECK(out.diag.error_nodes == 0);
    CHECK(out.diag.first_line == 0);
    CHECK(out.diag.first_col == 0);

    /* Nullable out-param stays supported. */
    in.path = "unit/broken2.js";
    in.source = broken_js;
    in.len = strlen(broken_js);
    in.grammar = "javascript";
    CHECK(oc_parser_parse_file(p, &in, &g, NULL, &st) == OC_OK);

    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

int main(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char broken_dir[512], clean_dir[512], packs[512];
    snprintf(broken_dir, sizeof(broken_dir), "%s/tests/fixtures_broken", root);
    snprintf(clean_dir, sizeof(clean_dir), "%s/tests/fixtures/js/clean_only", root);
    snprintf(packs, sizeof(packs), "%s/packs", root);

    char cmd[2048];
    oc_arena arena;
    oc_arena_init(&arena);

    unit_parse_diagnostics();

    /* 1) Broken JS + Python: exit 2; report names both files with spans. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s --packs-dir %s > /tmp/oc_pe_out1.json 2>/tmp/oc_pe_err1", bin, broken_dir,
             packs);
    CHECK(run_cmd(cmd) == 2);

    oc_json *report = load_json(&arena, "/tmp/oc_pe_out1.json");
    CHECK(report != NULL);
    const oc_json *meta = oc_json_get(report, "meta");
    CHECK(meta != NULL);
    CHECK(oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1) == 2);
    CHECK(oc_json_as_num(oc_json_get(meta, "files_with_parse_errors"), -1) == 2);
    CHECK(oc_json_as_num(oc_json_get(meta, "parse_error_nodes"), -1) >= 2);

    const oc_json *pe = oc_json_get(report, "parse_errors");
    CHECK(pe != NULL);
    CHECK(oc_json_size(pe) == 2);
    int saw_js = 0, saw_py = 0;
    for (size_t i = 0; i < oc_json_size(pe); i++) {
        const oc_json *e = pe->items[i];
        const char *f = oc_json_as_str(oc_json_get(e, "file"), "");
        if (strstr(f, "broken.js"))
            saw_js = 1;
        if (strstr(f, "broken.py"))
            saw_py = 1;
        CHECK(oc_json_as_num(oc_json_get(e, "error_nodes"), 0) >= 1);
        CHECK(oc_json_as_num(oc_json_get(e, "first_line"), 0) == 1);
        CHECK(oc_json_as_num(oc_json_get(e, "first_column"), 0) >= 1);
    }
    CHECK(saw_js && saw_py);

    /* 2) Determinism: rerun byte-identical. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > /tmp/oc_pe_out2.json 2>/dev/null", bin,
             broken_dir, packs);
    CHECK(run_cmd(cmd) == 2);
    CHECK(files_equal("/tmp/oc_pe_out1.json", "/tmp/oc_pe_out2.json"));

    /* 3) Clean control: exit 0 with zero parse errors (not vacuous). */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > /tmp/oc_pe_clean.json 2>/dev/null",
             bin, clean_dir, packs);
    CHECK(run_cmd(cmd) == 0);
    oc_json *clean_rep = load_json(&arena, "/tmp/oc_pe_clean.json");
    CHECK(clean_rep != NULL);
    CHECK(oc_json_as_num(oc_json_get(oc_json_get(clean_rep, "meta"), "files_with_parse_errors"),
                         -1) == 0);
    CHECK(oc_json_size(oc_json_get(clean_rep, "parse_errors")) == 0);

    /* 4) Mixed run: clean file still analyzed alongside broken ones, exit 2. */
    snprintf(cmd, sizeof(cmd), "%s check %s %s --packs-dir %s > /tmp/oc_pe_mixed.json 2>/dev/null",
             bin, clean_dir, broken_dir, packs);
    CHECK(run_cmd(cmd) == 2);
    oc_json *mixed = load_json(&arena, "/tmp/oc_pe_mixed.json");
    CHECK(mixed != NULL);
    const oc_json *mmeta = oc_json_get(mixed, "meta");
    CHECK(oc_json_as_num(oc_json_get(mmeta, "files_analyzed"), -1) == 3);
    CHECK(oc_json_as_num(oc_json_get(mmeta, "files_with_parse_errors"), -1) == 2);

    /* 5) Precedence: findings at/above threshold AND parse errors -> 2, not 1. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/tests/fixtures/js %s --packs-dir %s --config OC001.max_lines=5 "
             "> /tmp/oc_pe_prec.json 2>/dev/null",
             bin, root, broken_dir, packs);
    CHECK(run_cmd(cmd) == 2);
    oc_json *prec = load_json(&arena, "/tmp/oc_pe_prec.json");
    CHECK(prec != NULL);
    CHECK(oc_json_as_num(oc_json_get(oc_json_get(prec, "meta"), "finding_count"), -1) >= 1);
    CHECK(oc_json_as_num(oc_json_get(oc_json_get(prec, "meta"), "files_with_parse_errors"), -1) ==
          2);

    /* 6) graph subcommand: exit 2 on the broken dir, 0 on the clean dir. */
    snprintf(cmd, sizeof(cmd), "%s graph %s --packs-dir %s > /dev/null 2>&1", bin, broken_dir,
             packs);
    CHECK(run_cmd(cmd) == 2);
    snprintf(cmd, sizeof(cmd), "%s graph %s --packs-dir %s > /dev/null 2>&1", bin, clean_dir,
             packs);
    CHECK(run_cmd(cmd) == 0);

    /* 7) stderr carries an actionable count line. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s >/dev/null 2>/tmp/oc_pe_err2", bin,
             broken_dir, packs);
    CHECK(run_cmd(cmd) == 2);
    char *err = NULL;
    size_t err_len = 0;
    CHECK(oc_read_file("/tmp/oc_pe_err2", &err, &err_len, NULL) == OC_OK);
    CHECK(err != NULL && strstr(err, "2 file(s) did not parse cleanly"));
    free(err);

    oc_arena_destroy(&arena);
    TEST_SUMMARY();
}
