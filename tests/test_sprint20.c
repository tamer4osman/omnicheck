/* Sprint 20 (the C language adoption): the contract of
 * docs/LANGUAGES.md (C section), pinned unit by unit.
 *
 * Symbols: function DEFINITIONS with a body (the whole definition is the
 * span); the grammar has no name field, so the symbol name is the
 * identifier at the bottom of the declarator chain (plain, pointer
 * returns, function-pointer returns, K&R). Prototypes are NOT symbols
 * (the honest double-count rationale). Complexity: the decision table --
 * if_statement (else-if is a NESTED if_statement) + while/do/for +
 * case_statement WITH a value (a default arm is the same node type
 * without it and counts 0) + the ternary, plus && and ||; preprocessor
 * conditionals are NOT decisions. Imports: the include channel -- quoted
 * resolves relative to the including file's directory, angle system
 * includes never resolve, a macro path records nothing. Resolver v1-c.
 * Surface: top-level definitions with C's internal-linkage filter
 * (static narrows), tags/typedefs/macros/multi-declarators, the
 * enumerator boundary. Main fact: a function DEFINITION named main.
 * Coverage: the existing Cobertura reader accepts gcovr's Cobertura XML.
 * Supply: NO C manifest tier -- nothing is discovered, nothing is
 * fabricated.
 */
#include "oc/common.h"
#include "oc/coverage.h"
#include "oc/graph.h"
#include "oc/imports.h"
#include "oc/parser.h"
#include "oc/supply.h"
#include "oc/surface.h"
#include "testkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STR_EQ(a, b) ((a) != NULL && (b) != NULL && strcmp((a), (b)) == 0)

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the built oc binary"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif

static size_t count_symbols_named(const oc_graph *g, const char *name) {
    size_t n = 0;
    for (size_t i = 0; i < g->n_nodes; i++)
        if (g->nodes[i].kind == OC_NODE_FUNCTION && strcmp(g->nodes[i].name, name) == 0)
            n++;
    return n;
}

static const oc_node *symbol_named(const oc_graph *g, const char *name) {
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].kind == OC_NODE_FUNCTION && strcmp(g->nodes[i].name, name) == 0)
            return &g->nodes[i];
    }
    return NULL;
}

static void parse_one(oc_parser *p, oc_graph *g, const char *path, const char *src,
                      oc_parse_out *out) {
    oc_parser_input in;
    memset(&in, 0, sizeof(in));
    in.path = path;
    in.source = src;
    in.len = strlen(src);
    in.grammar = "c";
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_parser_parse_file(p, &in, g, out, &st) == OC_OK);
    CHECK(out->diag.had_error == 0);
}

static size_t imports_count_spec(const oc_import_vec *v, const char *spec) {
    size_t n = 0;
    for (size_t i = 0; i < v->n; i++)
        if (strcmp(v->items[i].specifier, spec) == 0)
            n++;
    return n;
}

static const char *bind_name_for(const oc_bind_vec *v, const char *spec) {
    for (size_t i = 0; i < v->n; i++)
        if (strcmp(v->items[i].specifier, spec) == 0)
            return v->items[i].name;
    return NULL;
}

static size_t exports_count_named(const oc_export_vec *v, const char *name) {
    size_t n = 0;
    for (size_t i = 0; i < v->n; i++)
        if (strcmp(v->items[i].name, name) == 0)
            n++;
    return n;
}

static void run_oc_json(const char *root, const char *fixture, const char *extra, char *buf,
                        size_t cap) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd %s && %s %s %s --packs-dir %s/packs --format json 2>/dev/null",
             root, OC_OC_BIN, extra ? extra : "check", fixture, root);
    FILE *fp = popen(cmd, "r");
    CHECK(fp != NULL);
    if (fp) {
        size_t n = fread(buf, 1, cap - 1, fp);
        buf[n] = '\0';
        pclose(fp);
    }
}

static size_t json_count_rule(const char *json, const char *rule) {
    size_t n = 0;
    const char *p = json;
    size_t rl = strlen(rule);
    while ((p = strstr(p, "\"rule\":")) != NULL) {
        p += 7;
        while (*p == ' ' || *p == '"')
            p++;
        if (strncmp(p, rule, rl) == 0)
            n++;
    }
    return n;
}

/* --- unit: symbols, spans, prototypes-not-symbols ----------------------- */

static void unit_c_symbols(void) {
    static const char src[] =
        "/* plain definition: symbol, span = the whole definition */\n"
        "int plain(int a, int b)\n"
        "{\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "/* pointer return: the name sits under a pointer_declarator */\n"
        "char *dup_str(const char *s)\n"
        "{\n"
        "    return 0;\n"
        "}\n"
        "\n"
        "/* static helper: a symbol (OC001 material), never module surface */\n"
        "static unsigned hidden(void)\n"
        "{\n"
        "    return 0u;\n"
        "}\n"
        "\n"
        "/* function-pointer return: TWO parameter_lists, one name */\n"
        "int (*get_step(int n))(int)\n"
        "{\n"
        "    return 0;\n"
        "}\n"
        "\n"
        "/* K&R old-style definition: a symbol through the same query */\n"
        "int old(a, b) int a; int b;\n"
        "{\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "/* prototypes are NOT symbols: the definition lives elsewhere */\n"
        "int add3(int a, int b, int c);\n"
        "void cb(int (*visit)(int item, void *ctx));\n"
        "extern unsigned flags;\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        parse_one(p, &g, "unit/widget.c", src, &out);

        CHECK(count_symbols_named(&g, "plain") == 1);
        CHECK(count_symbols_named(&g, "dup_str") == 1);
        CHECK(count_symbols_named(&g, "hidden") == 1);
        CHECK(count_symbols_named(&g, "get_step") == 1);
        CHECK(count_symbols_named(&g, "old") == 1);
        /* prototypes and objects are never symbols */
        CHECK(count_symbols_named(&g, "add3") == 0);
        CHECK(count_symbols_named(&g, "visit") == 0);
        CHECK(count_symbols_named(&g, "flags") == 0);
        CHECK(count_symbols_named(&g, "cb") == 0);

        /* span contract: the whole definition extent */
        const oc_node *n = symbol_named(&g, "plain");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->decl_line == 2);
            CHECK(n->def_start_line == 2);
            CHECK(n->def_end_line == 5);
            CHECK(n->complexity == 1);
        }
        n = symbol_named(&g, "get_step");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->decl_line == 20);
            CHECK(n->def_end_line == 23);
        }
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the decision table (hand-counts) ---------------------------- */

static void unit_c_decision_table(void) {
    struct {
        const char *name;
        const char *body;
        size_t comp;
    } cases[] = {
        /* if + else-if x2 (each branch by itself; a bare else never) = 3 */
        {"branch_chain",
         "int branch_chain(int x) {\n"
         "    if (x > 0) { return 1;\n"
         "    } else if (x == 0) { return 2;\n"
         "    } else if (x < 0) { return 3;\n"
         "    } else { return 4; }\n"
         "}\n",
         4},
        /* while / do-while / for = 3 (the do-while's while is part of the
         * do_statement node, not a second decision) */
        {"loops",
         "int loops(int x) {\n"
         "    int i = 0;\n"
         "    while (i < 3) { i++; }\n"
         "    do { i++; } while (i < 9);\n"
         "    for (int j = 0; j < 2; j++) {}\n"
         "    return i;\n"
         "}\n",
         4},
        /* switch: one per case_statement WITH a value; a default arm is
         * the same node type WITHOUT the value and counts 0 = 2 */
        {"switch_arms",
         "int switch_arms(int x) {\n"
         "    switch (x) {\n"
         "    case 1:\n"
         "        return 1;\n"
         "    case 2:\n"
         "        return 2;\n"
         "    default:\n"
         "        return 0;\n"
         "    }\n"
         "}\n",
         3},
        /* fall-through labels: case 1/2/3 are separate nodes = 3 */
        {"fall_through",
         "int fall_through(int x) {\n"
         "    switch (x) {\n"
         "    case 1:\n"
         "    case 2:\n"
         "    case 3:\n"
         "        return 3;\n"
         "    }\n"
         "    return 0;\n"
         "}\n",
         4},
        /* the ternary = 1 */
        {"ternaries",
         "int ternaries(int x) {\n"
         "    int a = x > 0 ? 1 : 2;\n"
         "    return a;\n"
         "}\n",
         2},
        /* operators: && and || inside binary_expression = 2 */
        {"operators",
         "int operators(int a, int b) {\n"
         "    int x = a && b;\n"
         "    int y = a || b;\n"
         "    return x + y;\n"
         "}\n",
         3},
        /* preprocessor conditionals are NOT decisions, but the RUNTIME
         * statements they guard still are (the walker sees the whole
         * body subtree through the preproc wrappers): if + while = 2 */
        {"preproc_not_decision",
         "int preproc_not_decision(int x) {\n"
         "#ifdef FAST\n"
         "    if (x > 0) { return 1; }\n"
         "#else\n"
         "    while (x > 0) { x--; }\n"
         "#endif\n"
         "    return x;\n"
         "}\n",
         3},
        /* goto is not a branch */
        {"goto_not_branch",
         "int goto_not_branch(int x) {\n"
         "    if (x < 0) goto out;\n"
         "    x++;\n"
         "out:\n"
         "    return x;\n"
         "}\n",
         2},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        oc_arena a;
        oc_arena_init(&a);
        oc_graph g;
        oc_graph_init(&g, &a);
        oc_status st;
        oc_status_clear(&st);
        oc_parser *p = oc_parser_new(&a, &st);
        if (p) {
            oc_parse_out out;
            memset(&out, 0, sizeof(out));
            parse_one(p, &g, "unit/case.c", cases[i].body, &out);
            const oc_node *n = symbol_named(&g, cases[i].name);
            CHECK(n != NULL);
            if (n)
                CHECK(n->complexity == cases[i].comp);
            oc_parser_free(p);
        }
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
    }
}

/* --- unit: parameters and the compat (B3) signature --------------------- */

static void unit_c_params(void) {
    static const char src[] = "/* named, pointer, unnamed (abstract), variadic, fnptr param */\n"
                              "void full(int a, char *b);\n"
                              "void abstract(int, char *);\n"
                              "void variadic(const char *fmt, ...);\n"
                              "void cb(int (*visit)(int item, void *ctx), void *ctx);\n"
                              "/* the fnptr-return definition: the OUTER parameter list is the\n"
                              " * caller-visible signature (the honest shape) */\n"
                              "int (*get_step(int n))(int)\n"
                              "{\n"
                              "    return 0;\n"
                              "}\n"
                              "/* K&R old-style: bare identifier names, types declared after */\n"
                              "int old(a, b) int a; int b;\n"
                              "{\n"
                              "    return a + b;\n"
                              "}\n"
                              "int get_step_proxy(int n);\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        parse_one(p, &g, "unit/params.c", src, &out);

        const oc_node *n = symbol_named(&g, "get_step");
        CHECK(n != NULL);
        if (n) {
            /* the INNER function_declarator (adjacent to the identifier)
             * carries the function's own list (int n); the OUTER one is
             * the RETURNED function pointer's type (abstract (int)) */
            CHECK(n->n_params == 1);
            if (n->n_params == 1) {
                CHECK(STR_EQ(n->params[0].name, "n"));
                CHECK(STR_EQ(n->params[0].type, "int"));
            }
        }
        n = symbol_named(&g, "old");
        CHECK(n != NULL);
        if (n) {
            /* K&R: bare names, the type honestly unknown */
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "a"));
                CHECK(n->params[0].type == NULL);
                CHECK(STR_EQ(n->params[1].name, "b"));
            }
        }
        oc_parser_free(p);
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);

        /* the declaration shapes: unnamed and variadic collapse to the
         * `...` marker (declarations are not symbols -- this parse only
         * checks the import-free path); reparse with a definition proxy
         * that carries the same parameter_list shapes */
        oc_arena a2;
        oc_arena_init(&a2);
        oc_graph g2;
        oc_graph_init(&g2, &a2);
        oc_parser *p2 = oc_parser_new(&a2, &st);
        CHECK(p2 != NULL);
        static const char src2[] = "void full(int a, char *b) { }\n"
                                   "void abstract(int, char *) { }\n"
                                   "void variadic(const char *fmt, ...) { }\n"
                                   "void cb(int (*visit)(int item, void *ctx), void *ctx) { }\n";
        oc_parse_out out2;
        memset(&out2, 0, sizeof(out2));
        parse_one(p2, &g2, "unit/params2.c", src2, &out2);
        n = symbol_named(&g2, "full");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "a"));
                CHECK(STR_EQ(n->params[0].type, "int"));
                CHECK(STR_EQ(n->params[1].name, "b"));
                /* the as-written type slice: qualifier and base type, the
                 * pointer stays declarator syntax (the documented
                 * boundary) */
                CHECK(STR_EQ(n->params[1].type, "char"));
            }
        }
        n = symbol_named(&g2, "abstract");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                /* unnamed (abstract) parameters: the `...` marker with
                 * the as-written type slice (the abstract pointer is
                 * declarator syntax, so the type reads "char" like its
                 * named sibling) */
                CHECK(STR_EQ(n->params[0].name, "..."));
                CHECK(STR_EQ(n->params[0].type, "int"));
                CHECK(STR_EQ(n->params[1].name, "..."));
                CHECK(STR_EQ(n->params[1].type, "char"));
            }
        }
        n = symbol_named(&g2, "variadic");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "fmt"));
                CHECK(STR_EQ(n->params[0].type, "const char"));
                CHECK(STR_EQ(n->params[1].name, "..."));
                CHECK(n->params[1].type == NULL);
            }
        }
        n = symbol_named(&g2, "cb");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                /* the fnptr param name sits at the declarator-chain bottom */
                CHECK(STR_EQ(n->params[0].name, "visit"));
                CHECK(STR_EQ(n->params[1].name, "ctx"));
            }
        }
        oc_parser_free(p2);
        oc_graph_destroy(&g2);
        oc_arena_destroy(&a2);
        return;
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: import records (the include channel) ------------------------ */

static void unit_c_import_records(void) {
    static const char src[] = "#include \"util/log.h\"\n"
                              "#include <stdio.h>\n"
                              "#include HEADER_PATH\n" /* macro path: NOTHING */
                              "#include \"missing.h\"\n"
                              "#include \"\"\n" /* empty: NOTHING */
                              "int f(void) { return 0; }\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        oc_import_vec imports;
        oc_imports_init(&imports);
        oc_bind_vec binds;
        oc_surface_binds_init(&binds);
        out.imports = &imports;
        out.binds = &binds;
        parse_one(p, &g, "unit/imports.c", src, &out);

        /* the quoted channel: one record, the `include` tag in the
         * specifier (the Rust `mod ` precedent) */
        CHECK(imports_count_spec(&imports, "include util/log.h") == 1);
        /* the angle channel: the `system` tag, never resolved later */
        CHECK(imports_count_spec(&imports, "system stdio.h") == 1);
        /* the macro path and the empty payload record NOTHING */
        CHECK(imports.n == 3);

        /* binds: NULL for the whole-file claim (the Ruby require
         * discipline) */
        CHECK(bind_name_for(&binds, "include util/log.h") == NULL);
        CHECK(bind_name_for(&binds, "system stdio.h") == NULL);
        oc_imports_destroy(&imports);
        oc_surface_binds_destroy(&binds);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: surface (exports, exportedness, the enumerator boundary) ---- */

static void unit_c_surface(void) {
    static const char src[] = "/* direct tag definitions: surface */\n"
                              "struct point { int x; int y; };\n"
                              "union raw { unsigned u; void *p; };\n"
                              "enum color { RED, GREEN = 2, BLUE };\n"
                              "\n"
                              "/* typedefs: the declarator-chain name is surface */\n"
                              "typedef struct point point_t;\n"
                              "typedef int (*handler)(void *, size_t);\n"
                              "\n"
                              "/* macros: the name field is surface */\n"
                              "#define MAX_TAGS 16\n"
                              "#define TAG(x) ((x) * 2)\n"
                              "\n"
                              "/* anonymous struct variable: only the declarator is surface */\n"
                              "struct { int a; } anon_var;\n"
                              "\n"
                              "/* a bare tag USE records nothing (no body) */\n"
                              "struct point pt;\n"
                              "\n"
                              "/* multi-declarator line: EACH declarator is surface */\n"
                              "int obj_a, obj_b = 2, obj_c[4];\n"
                              "\n"
                              "/* static narrows: internal linkage, never surface */\n"
                              "static int internal_state = 3;\n"
                              "static int hidden_fn(void) { return 0; }\n"
                              "\n"
                              "/* extern does NOT narrow */\n"
                              "extern int visible_flag;\n"
                              "\n"
                              "/* the tag defined INSIDE a declaration with a variable: both\n"
                              " * the tag and the declarator are surface */\n"
                              "struct paired { int q; } paired_var;\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        oc_export_vec exports;
        oc_surface_exports_init(&exports);
        out.exports = &exports;
        parse_one(p, &g, "unit/surface.c", src, &out);

        CHECK(exports_count_named(&exports, "point") == 1);
        CHECK(exports_count_named(&exports, "raw") == 1);
        CHECK(exports_count_named(&exports, "color") == 1);
        CHECK(exports_count_named(&exports, "point_t") == 1);
        CHECK(exports_count_named(&exports, "handler") == 1);
        CHECK(exports_count_named(&exports, "MAX_TAGS") == 1);
        CHECK(exports_count_named(&exports, "TAG") == 1);
        CHECK(exports_count_named(&exports, "anon_var") == 1);
        CHECK(exports_count_named(&exports, "pt") == 1);
        CHECK(exports_count_named(&exports, "obj_a") == 1);
        CHECK(exports_count_named(&exports, "obj_b") == 1);
        CHECK(exports_count_named(&exports, "obj_c") == 1);
        CHECK(exports_count_named(&exports, "visible_flag") == 1);
        CHECK(exports_count_named(&exports, "paired") == 1);
        CHECK(exports_count_named(&exports, "paired_var") == 1);
        /* static = internal linkage: not module surface */
        CHECK(exports_count_named(&exports, "internal_state") == 0);
        CHECK(exports_count_named(&exports, "hidden_fn") == 0);
        /* the enumerator boundary: enum constants are NOT surface v1 */
        CHECK(exports_count_named(&exports, "RED") == 0);
        CHECK(exports_count_named(&exports, "GREEN") == 0);
        CHECK(exports_count_named(&exports, "BLUE") == 0);
        oc_surface_exports_destroy(&exports);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: include guards + extern "C" are transparent to top-level ---- */

static void unit_c_toplevel_climb(void) {
    static const char src[] = "#ifndef WIDGET_H\n"
                              "#define WIDGET_H\n"
                              "\n"
                              "struct guarded { int a; };\n"
                              "#ifdef FAST\n"
                              "int fast_path(int n);\n"
                              "#else\n"
                              "int slow_path(int n);\n"
                              "#endif\n"
                              "\n"
                              "extern \"C\" int exported_fn(int n);\n"
                              "extern \"C\" { struct inner { int b; }; int also_fn(int n); }\n"
                              "\n"
                              "#endif\n"
                              "/* inside a function body: NOT top level, never surface */\n"
                              "int wrapper(void) {\n"
                              "    int local_var = 1;\n"
                              "    return local_var;\n"
                              "}\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        oc_export_vec exports;
        oc_surface_exports_init(&exports);
        out.exports = &exports;
        parse_one(p, &g, "unit/guarded.h", src, &out);

        /* every conventional-header declaration is surface through the
         * guard, the #ifdef branches and the extern "C" blocks */
        CHECK(exports_count_named(&exports, "guarded") == 1);
        CHECK(exports_count_named(&exports, "fast_path") == 1);
        CHECK(exports_count_named(&exports, "slow_path") == 1);
        CHECK(exports_count_named(&exports, "exported_fn") == 1);
        CHECK(exports_count_named(&exports, "inner") == 1);
        CHECK(exports_count_named(&exports, "also_fn") == 1);
        CHECK(exports_count_named(&exports, "WIDGET_H") == 1); /* the #define */
        /* a function-body local never is */
        CHECK(exports_count_named(&exports, "local_var") == 0);
        CHECK(exports_count_named(&exports, "wrapper") == 1);
        oc_surface_exports_destroy(&exports);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the main fact (entry-point parse fact) ---------------------- */

static void unit_c_main_fact(void) {
    oc_arena a;
    oc_arena_init(&a);

    static const char with_main[] = "#include \"x.h\"\n"
                                    "\n"
                                    "int main(void) {\n"
                                    "    return 0;\n"
                                    "}\n";
    static const char with_argc[] = "int main(int argc, char **argv) {\n"
                                    "    return 0;\n"
                                    "}\n";
    static const char prototype_only[] = "int main(void);\n"
                                         "int other(void) {\n"
                                         "    return 1;\n"
                                         "}\n";
    static const char no_main[] = "int helper(void) {\n"
                                  "    return 1;\n"
                                  "}\n";

    struct {
        const char *src;
        int expect_main;
    } cases[] = {
        {with_main, 1},
        {with_argc, 1},
        {prototype_only, 0},
        {no_main, 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        oc_graph g;
        oc_graph_init(&g, &a);
        oc_status st;
        oc_status_clear(&st);
        oc_parser *p = oc_parser_new(&a, &st);
        CHECK(p != NULL);
        if (p) {
            oc_parse_out out;
            memset(&out, 0, sizeof(out));
            oc_hint_vec hints;
            oc_surface_hints_init(&hints);
            out.hints = &hints;
            parse_one(p, &g, "unit/main.c", cases[i].src, &out);
            if (cases[i].expect_main) {
                CHECK(hints.n == 1);
                if (hints.n == 1)
                    CHECK(hints.items[0].has_main == 1);
            } else {
                CHECK(hints.n == 0);
            }
            oc_surface_hints_destroy(&hints);
            oc_parser_free(p);
        }
        oc_graph_destroy(&g);
    }
    oc_arena_destroy(&a);
}

/* --- unit: resolver v1-c ----------------------------------------------- */

static int has_edge(const oc_graph *g, const char *from, const char *to) {
    oc_node_id f = OC_NODE_INVALID, t = OC_NODE_INVALID;
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].kind != OC_NODE_FILE)
            continue;
        if (strcmp(g->nodes[i].path, from) == 0)
            f = (oc_node_id)i;
        if (strcmp(g->nodes[i].path, to) == 0)
            t = (oc_node_id)i;
    }
    if (f == OC_NODE_INVALID || t == OC_NODE_INVALID)
        return 0;
    for (size_t i = 0; i < g->n_edges; i++)
        if (g->edges[i].kind == OC_EDGE_IMPORTS && g->edges[i].from == f && g->edges[i].to == t)
            return 1;
    return 0;
}

static void unit_c_resolver(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    /* corpus B: the include-resolution fixture tree. No manifest is ever
     * read (none exists for C) -- resolution is purely the analyzed
     * file set + the including file's directory. */
    char root[1024];
    snprintf(root, sizeof(root), "%s/tests/fixtures/c_resolver", OC_REPO_ROOT);
    char paths[9][1400];
    static const char *const REL[] = {
        "src/main.c",      "src/util/log.h",      "src/util/log.c", "third/p.h", "third/p.c",
        "include/outer.h", "include/sub/inner.h", "cyc/a.h",        "cyc/b.h",
    };
    for (size_t i = 0; i < sizeof(REL) / sizeof(REL[0]); i++) {
        snprintf(paths[i], sizeof(paths[i]), "%s/%s", root, REL[i]);
        oc_graph_add_file(&g, paths[i]);
    }
    const char *roots[1] = {root};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }

    char from[1400];
    snprintf(from, sizeof(from), "%s/src/main.c", root);
    /* the quoted channel: relative to the INCLUDING file's directory */
    char log_h[1400];
    snprintf(log_h, sizeof(log_h), "%s/src/util/log.h", root);
    char p_h[1400];
    snprintf(p_h, sizeof(p_h), "%s/third/p.h", root);
    CHECK_STR_EQ(oc_resolve_c_include(cx, from, "include util/log.h", &a), log_h);
    /* the lexical .. pops across src/ (the PHP include-channel rule) */
    CHECK_STR_EQ(oc_resolve_c_include(cx, from, "include ../third/p.h", &a), p_h);
    /* the system channel NEVER resolves (recorded-and-unresolved) */
    CHECK(oc_resolve_c_include(cx, from, "system stdio.h", &a) == NULL);
    /* guards repeated at the resolver (defense in depth) */
    CHECK(oc_resolve_c_include(cx, from, "include /abs/path.h", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "include back\\slash.h", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "include ", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "include missing.h", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "wat", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "", &a) == NULL);

    /* the nested include anchors at the INCLUDING FILE's directory:
     * outer.h's "sub/inner.h" resolves against include/, NOT against
     * the .c file's directory (the C preprocessor rule) */
    char outer[1400];
    snprintf(outer, sizeof(outer), "%s/include/outer.h", root);
    char inner_h[1400];
    snprintf(inner_h, sizeof(inner_h), "%s/include/sub/inner.h", root);
    CHECK_STR_EQ(oc_resolve_c_include(cx, outer, "include sub/inner.h", &a), inner_h);

    /* the same-directory include from a nested dir: log.c -> log.h */
    char log_c[1400];
    snprintf(log_c, sizeof(log_c), "%s/src/util/log.c", root);
    CHECK_STR_EQ(oc_resolve_c_include(cx, log_c, "include log.h", &a), log_h);

    /* edges: set semantics (a header included twice collapses to one
     * edge) and the cycle (a.h <-> b.h) */
    oc_import_vec v;
    oc_imports_init(&v);
    CHECK(oc_imports_push(&v, &a, from, "include util/log.h", OC_LANG_C));
    CHECK(oc_imports_push(&v, &a, from, "include util/log.h", OC_LANG_C)); /* dup */
    CHECK(oc_imports_push(&v, &a, from, "system stdio.h", OC_LANG_C));     /* unresolved */
    CHECK(oc_imports_push(&v, &a, log_c, "include log.h", OC_LANG_C));
    char a_h[1400], b_h[1400];
    snprintf(a_h, sizeof(a_h), "%s/cyc/a.h", root);
    snprintf(b_h, sizeof(b_h), "%s/cyc/b.h", root);
    CHECK(oc_imports_push(&v, &a, a_h, "include b.h", OC_LANG_C));
    CHECK(oc_imports_push(&v, &a, b_h, "include a.h", OC_LANG_C));
    /* 3 distinct edges: main->log.h (set collapse), log.c->log.h,
     * a.h->b.h, b.h->a.h = 4 */
    CHECK(oc_imports_resolve_ex(&g, &v, &a, roots, 1) == 4);
    CHECK(has_edge(&g, from, log_h));
    CHECK(has_edge(&g, log_c, log_h));
    CHECK(has_edge(&g, a_h, b_h));
    CHECK(has_edge(&g, b_h, a_h));
    oc_imports_destroy(&v);

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: gcovr Cobertura coverage through the existing reader -------- */

static void unit_c_cobertura(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_coverage cov;
    oc_coverage_init(&cov);
    const char *paths[] = {OC_REPO_ROOT "/tests/fixtures_sprint20/gcovr_cobertura.xml"};
    CHECK(oc_coverage_load(&cov, paths, 1, &a, &st) == OC_OK);
    CHECK(cov.n_files == 2); /* rollup.c (two class elements) + decls.c */

    /* anchors + covered by definition-line window (gcovr records) */
    size_t anchors = 0, covered = 0;
    int hit = oc_coverage_lookup(&cov, "tests/fixtures/c/rollup.c", 37, 38, &anchors, &covered);
    CHECK(hit == 1);
    CHECK(anchors == 2);
    CHECK(covered == 2);
    hit = oc_coverage_lookup(&cov, "tests/fixtures/c/decls.c", 9, 11, &anchors, &covered);
    CHECK(hit == 1);
    CHECK(anchors == 3);
    CHECK(covered == 2); /* line 11 carries hits="0" */
    /* a path that no class element carries: not found, never guessed */
    hit = oc_coverage_lookup(&cov, "tests/fixtures/c/mathx.c", 1, 100, &anchors, &covered);
    CHECK(hit == 0);
    oc_coverage_destroy(&cov);
    oc_arena_destroy(&a);
}

/* --- unit: the honest supply tier (nothing discovered, nothing
 * fabricated) ----------------------------------------------------------- */

static void unit_c_supply_honest(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_manifest_vec mv;
    const char *files[] = {
        OC_REPO_ROOT "/tests/fixtures/c/rollup.c",
        OC_REPO_ROOT "/tests/fixtures/c/types.c",
        OC_REPO_ROOT "/tests/fixtures/c/local_util.h",
    };
    CHECK(oc_supply_load_manifests(&mv, files, 3, &a, &st) == OC_OK);
    /* C has NO canonical manifest: nothing is discovered for a C tree --
     * no fabricated pkg-config/vendored tier, manifests_scanned stays 0
     * and the report meta says so */
    CHECK(mv.n == 0);
    oc_manifests_destroy(&mv);
    oc_arena_destroy(&a);
}

/* --- integration: corpus A (structural) and the pack ------------------- */

static void unit_c_corpus_a(void) {
    char buf[65536];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/c", NULL, buf, sizeof(buf));
    /* parse clean, the c pack loaded */
    CHECK(strstr(buf, "\"files_analyzed\":13") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(strstr(buf, "\"c\"") != NULL);
    /* the enabled structural rules fire with the contracted counts */
    CHECK(json_count_rule(buf, "OC001") == 1); /* longfn 63 lines */
    CHECK(json_count_rule(buf, "OC002") == 1); /* todo.c marker */
    CHECK(json_count_rule(buf, "OC004") == 1); /* sum26 comp 35 */
    CHECK(json_count_rule(buf, "OC006") == 11);
    CHECK(json_count_rule(buf, "OC008") == 2); /* the dup pair */
    CHECK(json_count_rule(buf, "OC011") == 1); /* cfg/secret.h */
    /* OC003/OC005 (import-driven: the one resolved include marks its
     * target used) and OC007/OC009/OC010 (inputless -- the honest C
     * supply tier) never fire on this corpus */
    CHECK(json_count_rule(buf, "OC003") == 0);
    CHECK(json_count_rule(buf, "OC005") == 0);
    CHECK(json_count_rule(buf, "OC007") == 0);
    CHECK(json_count_rule(buf, "OC009") == 0);
    CHECK(json_count_rule(buf, "OC010") == 0);

    /* the OC006 set must NOT contain main_app.c (claimed by the main
     * parse fact) nor local_util.h (included twice) */
    CHECK(strstr(buf, "main_app.c") == NULL);
    CHECK(strstr(buf, "local_util.h") == NULL);
}

/* --- integration: the c pack declares full scope ----------------------- */

static void unit_c_pack(void) {
    char buf[32768];
    snprintf(buf, sizeof(buf), "cd %s && %s packs --packs-dir %s/packs 2>/dev/null", OC_REPO_ROOT,
             OC_OC_BIN, OC_REPO_ROOT);
    FILE *fp = popen(buf, "r");
    char *json = malloc(1 << 18);
    size_t n = 0;
    CHECK(fp != NULL);
    if (fp && json) {
        n = fread(json, 1, (1 << 18) - 1, fp);
        json[n] = '\0';
        pclose(fp);
    }
    CHECK(json != NULL);
    if (json) {
        /* the c pack entry carries the full pass scope and the
         * contracted pack data */
        const char *entry = strstr(json, "\"id\":\"c\"");
        CHECK(entry != NULL);
        if (entry) {
            const char *end = strstr(entry + 6, "\"id\":\"");
            char seg[4096];
            size_t len = (size_t)(end - entry);
            if (len >= sizeof(seg))
                len = sizeof(seg) - 1;
            memcpy(seg, entry, len);
            seg[len] = '\0';
            for (int r = 1; r <= 11; r++) {
                char rule[8];
                snprintf(rule, sizeof(rule), "OC0%02d", r);
                CHECK(strstr(seg, rule) != NULL);
            }
            CHECK(strstr(seg, "\"grammar\":\"c\"") != NULL);
            CHECK(strstr(seg, "\".c\"") != NULL);
            CHECK(strstr(seg, "\".h\"") != NULL);
        }
        free(json);
    }
}

int main(void) {
    unit_c_symbols();
    unit_c_decision_table();
    unit_c_params();
    unit_c_import_records();
    unit_c_surface();
    unit_c_toplevel_climb();
    unit_c_main_fact();
    unit_c_resolver();
    unit_c_cobertura();
    unit_c_supply_honest();
    unit_c_corpus_a();
    unit_c_pack();
    TEST_SUMMARY();
}
