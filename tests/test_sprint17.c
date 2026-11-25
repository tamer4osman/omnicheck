/* Sprint 17 (B3 build): the contract of docs/SPRINT-17.md pinned unit
 * by unit.
 *
 * Extraction: ordered (name, type, default) per parameter across all 8
 * grammar families -- Go grouped-name expansion, Ruby v0.23
 * optional_parameter/keyword_parameter shapes, TS annotation-colon
 * trim, splat/variadic/unnamed "..." markers, defaults on the policy
 * languages (JS family, Python, Ruby, C#). Compat v1: the binding
 * policy table end to end on the per-family signature fixture pairs
 * (single-signature semantics with default-awareness; Java/C# arity
 * sets; named-arg rename policy), plus the v0 pairs whose verdicts
 * must stay byte-preserved on the v1 schema.
 */
#include "oc/common.h"
#include "oc/compat.h"
#include "oc/graph.h"
#include "oc/pack.h"
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

static oc_arena arena;

/* --- helpers ----------------------------------------------------------- */

static const oc_node *find_fn(const oc_graph *g, const char *name) {
    for (size_t i = 0; i < g->n_nodes; i++)
        if (g->nodes[i].kind == OC_NODE_FUNCTION && strcmp(g->nodes[i].name, name) == 0)
            return &g->nodes[i];
    return NULL;
}

static size_t count_fn_named(const oc_graph *g, const char *name) {
    size_t n = 0;
    for (size_t i = 0; i < g->n_nodes; i++)
        if (g->nodes[i].kind == OC_NODE_FUNCTION && strcmp(g->nodes[i].name, name) == 0)
            n++;
    return n;
}

/* Parses one file through the real pack set (the same route the compat
 * gate takes), returning the graph on the caller's arena. */
static int parse_one(const char *abs_path, oc_graph *g) {
    oc_status st;
    oc_status_clear(&st);
    oc_pack_set packs;
    oc_packs_init(&packs, &arena);
    if (oc_packs_load_dir(&packs, OC_REPO_ROOT "/packs", &st) != OC_OK)
        return 0;
    const oc_pack *pack = oc_packs_match_path(&packs, abs_path);
    if (!pack) {
        oc_packs_destroy(&packs);
        return 0;
    }
    char *data = NULL;
    size_t len = 0;
    if (oc_read_file(abs_path, &data, &len, &st) != OC_OK) {
        oc_packs_destroy(&packs);
        return 0;
    }
    oc_parser_input in;
    memset(&in, 0, sizeof(in));
    in.path = abs_path;
    in.source = data;
    in.len = len;
    in.grammar = pack->grammar;
    oc_graph_init(g, &arena);
    oc_parser *p = oc_parser_new(&arena, &st);
    oc_parse_out out;
    memset(&out, 0, sizeof(out));
    oc_code rc = oc_parser_parse_file(p, &in, g, &out, &st);
    oc_parser_free(p);
    oc_packs_destroy(&packs);
    free(data);
    return rc == OC_OK;
}

static oc_json *load_json(const char *path) {
    char *data = NULL;
    size_t len = 0;
    oc_status st;
    oc_status_clear(&st);
    if (oc_read_file(path, &data, &len, &st) != OC_OK)
        return NULL;
    oc_json *j = oc_json_parse(&arena, data, len, &st);
    free(data);
    return j;
}

static int run_compat_sig(const char *fam, const char *out_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "cd %s && %s compat --baseline tests/fixtures/compat/sig/%s/v1 "
             "--current tests/fixtures/compat/sig/%s/v2 --packs-dir packs "
             "> %s 2>/dev/null",
             OC_REPO_ROOT, OC_OC_BIN, fam, fam, out_path);
    int st = system(cmd);
    return st == -1 ? -1 : WEXITSTATUS(st); /* CI gate exit code, decoded */
}

static int run_compat_v0(const char *base_dir, const char *cur_dir, const char *out_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "cd %s && %s compat --baseline tests/fixtures/compat/%s "
             "--current tests/fixtures/compat/%s --packs-dir packs "
             "> %s 2>/dev/null",
             OC_REPO_ROOT, OC_OC_BIN, base_dir, cur_dir, out_path);
    int st = system(cmd);
    return st == -1 ? -1 : WEXITSTATUS(st);
}

static size_t count_kind(const oc_json *arr, const char *kind) {
    if (!arr)
        return 0;
    size_t n = 0;
    for (size_t i = 0; i < oc_json_size(arr); i++) {
        const char *k = oc_json_as_str(oc_json_get(arr->items[i], "kind"), "?");
        if (strcmp(k, kind) == 0)
            n++;
    }
    return n;
}

/* --- extraction: per-family pins --------------------------------------- */

static void unit_extract_py(void) {
    oc_graph g;
    CHECK(parse_one(OC_REPO_ROOT "/tests/fixtures/compat/sig/py/v1/pkg_sig.py", &g));
    const oc_node *n = find_fn(&g, "arity_break");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[0].name, "a");
        CHECK_STR_EQ(n->params[1].name, "b");
        CHECK(n->params[0].type == NULL && n->params[1].type == NULL);
        CHECK(n->params[0].has_default == 0);
    }
    n = find_fn(&g, "widened");
    CHECK(n != NULL && n->params != NULL && n->n_params == 3);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[2].name, "c");
        CHECK(n->params[2].has_default == 1);
    }
    n = find_fn(&g, "typed");
    CHECK(n != NULL && n->params != NULL && n->n_params == 1);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[0].name, "x");
        CHECK_STR_EQ(n->params[0].type, "int");
    }
    oc_graph_destroy(&g);
}

static void unit_extract_js(void) {
    oc_graph g;
    CHECK(parse_one(OC_REPO_ROOT "/tests/fixtures/compat/sig/js/v1/mod_sig.js", &g));
    const oc_node *n = find_fn(&g, "arityBreak");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    n = find_fn(&g, "widened");
    CHECK(n != NULL && n->params != NULL && n->n_params == 3);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[2].name, "c");
        CHECK(n->params[2].has_default == 1);
        CHECK(n->params[2].type == NULL); /* javascript grammar: untyped */
    }
    oc_graph_destroy(&g);
}

static void unit_extract_ts(void) {
    oc_graph g;
    CHECK(parse_one(OC_REPO_ROOT "/tests/fixtures/compat/sig/ts/v1/mod_sig.ts", &g));
    const oc_node *n = find_fn(&g, "typed");
    CHECK(n != NULL && n->params != NULL && n->n_params == 1);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[0].type, "number"); /* colon trimmed */
        CHECK(n->params[0].has_default == 0);
    }
    n = find_fn(&g, "optw");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[1].name, "b");
        CHECK(n->params[1].has_default == 1); /* optional_parameter */
        CHECK_STR_EQ(n->params[1].type, "number");
    }
    oc_graph_destroy(&g);
}

static void unit_extract_go(void) {
    oc_graph g;
    CHECK(parse_one(OC_REPO_ROOT "/tests/fixtures/compat/sig/go/v1/sig.go", &g));
    const oc_node *n = find_fn(&g, "ArityBreak");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[0].name, "a"); /* grouped `(a, b int)` */
        CHECK_STR_EQ(n->params[1].name, "b");
        CHECK_STR_EQ(n->params[0].type, "int");
        CHECK_STR_EQ(n->params[1].type, "int");
    }
    n = find_fn(&g, "Variadic");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[1].name, "rest");
        CHECK_STR_EQ(n->params[1].type, "int");
    }
    oc_graph_destroy(&g);
}

static void unit_extract_java(void) {
    oc_graph g;
    CHECK(parse_one(OC_REPO_ROOT "/tests/fixtures/compat/sig/java/v1/Sig.java", &g));
    const oc_node *n = find_fn(&g, "arity");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[0].name, "a");
        CHECK_STR_EQ(n->params[0].type, "int");
    }
    CHECK(count_fn_named(&g, "multi") == 2); /* overloads are distinct nodes */
    oc_graph_destroy(&g);
}

static void unit_extract_csharp(void) {
    oc_graph g;
    CHECK(parse_one(OC_REPO_ROOT "/tests/fixtures/compat/sig/csharp/v1/Sig.cs", &g));
    const oc_node *n = find_fn(&g, "Arity");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    n = find_fn(&g, "Defaulted");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[1].name, "b");
        CHECK(n->params[1].has_default == 1);
        CHECK_STR_EQ(n->params[1].type, "int");
    }
    oc_graph_destroy(&g);
}

static void unit_extract_rust(void) {
    oc_graph g;
    CHECK(parse_one(OC_REPO_ROOT "/tests/fixtures/compat/sig/rust/v1/lib_sig.rs", &g));
    const oc_node *n = find_fn(&g, "arity");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[0].type, "i32");
        CHECK_STR_EQ(n->params[1].type, "i32");
    }
    oc_graph_destroy(&g);
}

static void unit_extract_ruby(void) {
    oc_graph g;
    CHECK(parse_one(OC_REPO_ROOT "/tests/fixtures/compat/sig/ruby/v1/sig.rb", &g));
    const oc_node *n = find_fn(&g, "arity");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    n = find_fn(&g, "widened");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    if (n && n->params) {
        CHECK_STR_EQ(n->params[1].name, "b");
        CHECK(n->params[1].has_default == 1); /* optional_parameter (ruby v0.23) */
    }
    n = find_fn(&g, "splat");
    CHECK(n != NULL && n->params != NULL && n->n_params == 2);
    if (n && n->params)
        CHECK_STR_EQ(n->params[1].name, "..."); /* splat marker keeps arity visible */
    oc_graph_destroy(&g);
}

/* --- compat v1: the binding policy table, end to end -------------------- */

static void unit_policy_sig_pairs(void) {
    struct {
        const char *fam;
        int exit_code;
        size_t breaking;
        size_t info;
    } cases[] = {
        {"py", 1, 4, 1},     /* count+type+rename(named-args)+reordered; default-widened */
        {"js", 1, 2, 2},     /* count+reordered; default-widened + rename informational */
        {"ts", 1, 2, 1},     /* type+reordered; default-widened */
        {"go", 1, 4, 0},     /* count x2 (grouped + variadic), type, reordered */
        {"java", 1, 2, 2},   /* vanished arity + type; added-overload x2 */
        {"csharp", 1, 4, 2}, /* vanished x2 + type + rename(named-args); added-overload x2 */
        {"rust", 1, 3, 0},   /* count, type, reordered */
        {"ruby", 1, 3, 1},   /* count x2 + reordered; default-widened */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char out[128];
        snprintf(out, sizeof(out), "/tmp/oc_s17_sig_%s.json", cases[i].fam);
        int rc = run_compat_sig(cases[i].fam, out);
        CHECK(rc == cases[i].exit_code);
        oc_json *r = load_json(out);
        CHECK(r != NULL);
        CHECK_STR_EQ(oc_json_as_str(oc_json_get(r, "verdict"), "?"),
                     cases[i].exit_code == 1 ? "BREAKING" : "SAFE");
        CHECK_STR_EQ(oc_json_as_str(oc_json_get(r, "schema"), "?"), "omnicheck.compat_report/v1");
        CHECK(oc_json_size(oc_json_get(r, "breaking")) == cases[i].breaking);
        CHECK(oc_json_size(oc_json_get(r, "informational")) == cases[i].info);
        /* the signature kinds each family must show */
        const oc_json *b = oc_json_get(r, "breaking");
        /* ts exercises type/default/reorder only: no count change */
        if (strcmp(cases[i].fam, "ts") != 0)
            CHECK(count_kind(b, "changed-param-count") >= 1);
        if (strcmp(cases[i].fam, "py") == 0 || strcmp(cases[i].fam, "ts") == 0 ||
            strcmp(cases[i].fam, "go") == 0 || strcmp(cases[i].fam, "java") == 0 ||
            strcmp(cases[i].fam, "csharp") == 0 || strcmp(cases[i].fam, "rust") == 0)
            CHECK(count_kind(b, "changed-param-type") == 1);
        if (strcmp(cases[i].fam, "py") == 0 || strcmp(cases[i].fam, "js") == 0 ||
            strcmp(cases[i].fam, "ts") == 0 || strcmp(cases[i].fam, "ruby") == 0)
            CHECK(count_kind(oc_json_get(r, "informational"), "added-param-default") == 1);
        if (strcmp(cases[i].fam, "java") == 0 || strcmp(cases[i].fam, "csharp") == 0)
            CHECK(count_kind(oc_json_get(r, "informational"), "added-overload") == 2);
        if (strcmp(cases[i].fam, "py") == 0 || strcmp(cases[i].fam, "csharp") == 0)
            CHECK(count_kind(b, "param-renamed") == 1); /* named-arg languages break */
        if (strcmp(cases[i].fam, "js") == 0)
            CHECK(count_kind(oc_json_get(r, "informational"), "param-renamed") == 1);
        /* reorder is a single-signature-kind; the arity-set families do
         * not carry a reorder case in their fixtures */
        if (strcmp(cases[i].fam, "java") != 0 && strcmp(cases[i].fam, "csharp") != 0)
            CHECK(count_kind(b, "param-reordered") == 1);
    }
}

static void unit_v0_pairs_on_v1_schema(void) {
    /* the SPRINT-5 fixtures must keep their exact verdicts on the v1
     * schema (no signature changes in those bumps) */
    CHECK(run_compat_v0("v1", "v2", "/tmp/oc_s17_v0_break.json") == 1);
    oc_json *r = load_json("/tmp/oc_s17_v0_break.json");
    CHECK(r != NULL);
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(r, "verdict"), "?"), "BREAKING");
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(r, "schema"), "?"), "omnicheck.compat_report/v1");
    const oc_json *b = oc_json_get(r, "breaking");
    CHECK(oc_json_size(b) == 2);
    CHECK(count_kind(b, "removed-export") == 1);
    CHECK(count_kind(b, "removed-function") == 1);
    CHECK(run_compat_v0("v1", "v2safe", "/tmp/oc_s17_v0_safe.json") == 0);
    oc_json *r2 = load_json("/tmp/oc_s17_v0_safe.json");
    CHECK(r2 != NULL);
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(r2, "verdict"), "?"), "SAFE");
    CHECK(oc_json_size(oc_json_get(r2, "breaking")) == 0);
}

static void unit_detail_strings(void) {
    /* the py pair: typed int -> str carries the comparable type detail */
    oc_json *r = load_json("/tmp/oc_s17_sig_py.json");
    CHECK(r != NULL);
    const oc_json *b = oc_json_get(r, "breaking");
    int found = 0;
    for (size_t i = 0; i < oc_json_size(b); i++) {
        const oc_json *e = b->items[i];
        if (strcmp(oc_json_as_str(oc_json_get(e, "kind"), "?"), "changed-param-type") == 0) {
            found = 1;
            CHECK_STR_EQ(oc_json_as_str(oc_json_get(e, "detail"), "?"), "param 1 type: int -> str");
            CHECK_STR_EQ(oc_json_as_str(oc_json_get(e, "symbol"), "?"), "typed");
        }
    }
    CHECK(found);
}

int main(void) {
    oc_arena_init(&arena);
    unit_extract_py();
    unit_extract_js();
    unit_extract_ts();
    unit_extract_go();
    unit_extract_java();
    unit_extract_csharp();
    unit_extract_rust();
    unit_extract_ruby();
    unit_policy_sig_pairs();
    unit_v0_pairs_on_v1_schema();
    unit_detail_strings();
    TEST_SUMMARY();
}
