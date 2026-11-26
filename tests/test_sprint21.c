/* Sprint 21 (the C++ language adoption, wave-2 close-out): the contract
 * of docs/LANGUAGES.md (C++ section), pinned unit by unit.
 *
 * Symbols: function DEFINITIONS with a body anywhere -- free functions,
 * inline and out-of-class methods, constructors, destructors (the
 * as-written ~X spelling), operator overloads (operator==), templated
 * functions; prototypes and member declarations are NOT symbols; lambdas
 * are anonymous. Complexity: C's decision table + for_range_loop +
 * catch_clause; the case_statement value-field guard (the default arm
 * counts 0). Imports: the C include channel shared (resolver v1-cpp).
 * Surface: named-namespace bodies and template wrappers are top level,
 * anonymous namespaces are internal linkage (never surface), class
 * members are not module surface, alias_declaration is surface. Main
 * fact: a free-standing function DEFINITION named main. .h routing: the
 * content sniff + project-root default, counted in the report meta.
 * Supply: NO manifest tier -- nothing is discovered, nothing is
 * fabricated.
 */
#include "oc/common.h"
#include "oc/coverage.h"
#include "oc/graph.h"
#include "oc/imports.h"
#include "oc/pack.h"
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
    in.grammar = "cpp";
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

/* --- unit: symbols, spans, the cpp declarator-chain shapes -------------- */

static void unit_cpp_symbols(void) {
    static const char src[] = "/* free function: symbol, span = the whole definition */\n"
                              "int compute(int n)\n"
                              "{\n"
                              "    return n;\n"
                              "}\n"
                              "\n"
                              "/* inline method: the name is a field_identifier */\n"
                              "struct Widget {\n"
                              "    int scale(int v) { return v; }\n"
                              "    Widget();          /* member declaration: NOT a symbol */\n"
                              "    ~Widget();         /* member declaration: NOT a symbol */\n"
                              "    bool operator==(const Widget &o) const { return true; }\n"
                              "    static int make(void) { return 0; }\n"
                              "};\n"
                              "\n"
                              "/* out-of-class definitions: qualified_identifier name field */\n"
                              "int Widget::scale(int v) { return v + 1; }\n"
                              "Widget::Widget() {}\n"
                              "Widget::~Widget() {}\n"
                              "\n"
                              "/* templated function: the template_declaration is the wrapper,\n"
                              " * the inner function_definition is the symbol */\n"
                              "template <typename T>\n"
                              "T pick(T a, T b) { return a; }\n"
                              "\n"
                              "/* lambda: anonymous, NOT a symbol */\n"
                              "int lambda_user(int v) {\n"
                              "    auto fn = [](int x) { return x * 2; };\n"
                              "    return fn(v);\n"
                              "}\n"
                              "\n"
                              "/* prototypes are NOT symbols */\n"
                              "int add3(int a, int b, int c);\n";

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
        parse_one(p, &g, "unit/widget.cpp", src, &out);

        CHECK(count_symbols_named(&g, "compute") == 1);
        CHECK(count_symbols_named(&g, "scale") == 2);      /* inline + out-of-class */
        CHECK(count_symbols_named(&g, "Widget") == 1);     /* the ctor (as written) */
        CHECK(count_symbols_named(&g, "~Widget") == 1);    /* the dtor spelling */
        CHECK(count_symbols_named(&g, "operator==") == 1); /* the as-written spelling */
        CHECK(count_symbols_named(&g, "make") == 1);
        CHECK(count_symbols_named(&g, "pick") == 1);
        CHECK(count_symbols_named(&g, "lambda_user") == 1);
        /* prototypes, member declarations and the lambda are NOT symbols */
        CHECK(count_symbols_named(&g, "add3") == 0);
        CHECK(count_symbols_named(&g, "fn") == 0);
        CHECK(count_symbols_named(&g, "o") == 0);

        /* span contract: the whole definition extent */
        const oc_node *n = symbol_named(&g, "compute");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->decl_line == 2);
            CHECK(n->def_start_line == 2);
            CHECK(n->def_end_line == 5);
            CHECK(n->complexity == 1);
        }
        n = symbol_named(&g, "pick");
        CHECK(n != NULL);
        if (n) {
            /* the fexpr is the INNER function_definition: the
             * template_declaration wrapper (the line above) adds no
             * extent -- decl_line is the definition's own line */
            CHECK(n->decl_line == 24);
            CHECK(n->def_end_line == 24);
        }
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the decision table (hand-counts, the cpp kinds) -------------- */

static void unit_cpp_decision_table(void) {
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
        /* while / do-while / for / the C++11 range-for (its OWN node
         * type) = 4 */
        {"loops",
         "int loops(int x) {\n"
         "    int i = 0;\n"
         "    int arr[2] = {1, 2};\n"
         "    while (i < 3) { i++; }\n"
         "    do { i++; } while (i < 9);\n"
         "    for (int j = 0; j < 2; j++) {}\n"
         "    for (int v : arr) { i += v; }\n"
         "    return i;\n"
         "}\n",
         5},
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
        /* ONE decision per catch_clause (two handlers = 2); try is not
         * a decision (the PHP discipline) */
        {"catches",
         "int catches(int x) {\n"
         "    try { x = x + 1; }\n"
         "    catch (const int &e) { x = e; }\n"
         "    catch (...) { x = 0; }\n"
         "    return x;\n"
         "}\n",
         3},
        /* the C++17 if-with-initializer is ONE if_statement */
        {"if_init",
         "int if_init(int x) {\n"
         "    if (int k = x; k > 3) { return k; }\n"
         "    return 0;\n"
         "}\n",
         2},
        /* preprocessor conditionals are NOT decisions, but the RUNTIME
         * statements they guard still are: if + while = 2 */
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
        /* a lambda's decisions count INWARD (anonymous constructs are
         * not symbols; the walker sees the whole enclosing body) */
        {"lambda_inward",
         "int lambda_inward(int x) {\n"
         "    auto fn = [x](int v) -> int { return v > 0 ? v : -v; };\n"
         "    return fn(x);\n"
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
            parse_one(p, &g, "unit/case.cpp", cases[i].body, &out);
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

static void unit_cpp_params(void) {
    static const char src[] = "/* named, reference, default-value, abstract, pack */\n"
                              "void full(int a, char *b) { }\n"
                              "void refs(int &count, const int &limit, char *buf) { }\n"
                              "void defaults(int a, int b = 2) { }\n"
                              "void abstract(int, char *) { }\n"
                              "template <typename... Args>\n"
                              "void emit(Args &&...args) { (void)sizeof...(args); }\n"
                              "/* the fnptr-return definition: the INNER list is the\n"
                              " * function's own (the honest shape, shared with C) */\n"
                              "int (*get_step(int n))(int) { return 0; }\n";

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
        parse_one(p, &g, "unit/params.cpp", src, &out);

        const oc_node *n = symbol_named(&g, "full");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "a"));
                CHECK(STR_EQ(n->params[0].type, "int"));
                CHECK(STR_EQ(n->params[1].name, "b"));
                /* the as-written type slice: the pointer stays declarator
                 * syntax (the documented C boundary, shared) */
                CHECK(STR_EQ(n->params[1].type, "char"));
                CHECK(n->params[0].has_default == 0);
            }
        }
        n = symbol_named(&g, "refs");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 3);
            if (n->n_params == 3) {
                /* reference declarators descend like pointers */
                CHECK(STR_EQ(n->params[0].name, "count"));
                CHECK(STR_EQ(n->params[0].type, "int"));
                CHECK(STR_EQ(n->params[1].name, "limit"));
                CHECK(STR_EQ(n->params[1].type, "const int"));
                CHECK(STR_EQ(n->params[2].name, "buf"));
            }
        }
        n = symbol_named(&g, "defaults");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                /* the C++ default-argument shape: the DEFAULT flag is
                 * recorded (the B3 signature's third slot) */
                CHECK(STR_EQ(n->params[0].name, "a"));
                CHECK(n->params[0].has_default == 0);
                CHECK(STR_EQ(n->params[1].name, "b"));
                CHECK(STR_EQ(n->params[1].type, "int"));
                CHECK(n->params[1].has_default == 1);
            }
        }
        n = symbol_named(&g, "abstract");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "..."));
                CHECK(STR_EQ(n->params[0].type, "int"));
                CHECK(STR_EQ(n->params[1].name, "..."));
                CHECK(STR_EQ(n->params[1].type, "char"));
            }
        }
        n = symbol_named(&g, "emit");
        CHECK(n != NULL);
        if (n) {
            /* the parameter PACK collapses to the `...` marker (the
             * PHP convention; arity stays caller-visible) */
            CHECK(n->n_params == 1);
            if (n->n_params == 1)
                CHECK(STR_EQ(n->params[0].name, "..."));
        }
        n = symbol_named(&g, "get_step");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 1);
            if (n->n_params == 1) {
                CHECK(STR_EQ(n->params[0].name, "n"));
                CHECK(STR_EQ(n->params[0].type, "int"));
            }
        }
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: import records (the shared include channel) ------------------ */

static void unit_cpp_import_records(void) {
    static const char src[] = "#include \"util/log.hpp\"\n"
                              "#include <vector>\n"
                              "#include HEADER_PATH\n" /* macro path: NOTHING */
                              "#include \"missing.hpp\"\n"
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
        parse_one(p, &g, "unit/imports.cpp", src, &out);

        CHECK(imports_count_spec(&imports, "include util/log.hpp") == 1);
        CHECK(imports_count_spec(&imports, "system vector") == 1);
        CHECK(imports.n == 3);
        /* the using family records NOTHING in v1 (no honest file
         * mapping without a manifest) */
        CHECK(bind_name_for(&binds, "include util/log.hpp") == NULL);
        CHECK(bind_name_for(&binds, "system vector") == NULL);
        oc_imports_destroy(&imports);
        oc_surface_binds_destroy(&binds);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: surface (exports, exportedness, the boundaries) -------------- */

static void unit_cpp_surface(void) {
    static const char src[] = "/* the class tag with a body: surface */\n"
                              "class Engine {\n"
                              "public:\n"
                              "    int start(int v);\n"
                              "    int id = 0;\n"
                              "};\n"
                              "\n"
                              "/* struct/union/enum class: surface */\n"
                              "struct point { int x; int y; };\n"
                              "union raw { unsigned u; void *p; };\n"
                              "enum class Mode { READ, WRITE };\n"
                              "\n"
                              "/* typedefs and the alias_declaration: surface */\n"
                              "typedef unsigned long word_t;\n"
                              "using Width = int;\n"
                              "\n"
                              "/* macros: the name field is surface */\n"
                              "#define MAX_TAGS 16\n"
                              "#define TAG(x) ((x) * 2)\n"
                              "\n"
                              "/* multi-declarator line: EACH declarator is surface */\n"
                              "int obj_a, obj_b = 2, obj_c[4];\n"
                              "\n"
                              "/* static narrows: internal linkage, never surface */\n"
                              "static int internal_state = 3;\n"
                              "static int hidden_fn(void) { return 0; }\n"
                              "\n"
                              "/* the tag defined INSIDE a declaration with a variable:\n"
                              " * both the tag and the declarator are surface */\n"
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
        parse_one(p, &g, "unit/surface.cpp", src, &out);

        CHECK(exports_count_named(&exports, "Engine") == 1);
        CHECK(exports_count_named(&exports, "point") == 1);
        CHECK(exports_count_named(&exports, "raw") == 1);
        CHECK(exports_count_named(&exports, "Mode") == 1);
        CHECK(exports_count_named(&exports, "word_t") == 1);
        CHECK(exports_count_named(&exports, "Width") == 1);
        CHECK(exports_count_named(&exports, "MAX_TAGS") == 1);
        CHECK(exports_count_named(&exports, "TAG") == 1);
        CHECK(exports_count_named(&exports, "obj_a") == 1);
        CHECK(exports_count_named(&exports, "obj_b") == 1);
        CHECK(exports_count_named(&exports, "obj_c") == 1);
        CHECK(exports_count_named(&exports, "paired") == 1);
        CHECK(exports_count_named(&exports, "paired_var") == 1);
        /* static = internal linkage: not module surface */
        CHECK(exports_count_named(&exports, "internal_state") == 0);
        CHECK(exports_count_named(&exports, "hidden_fn") == 0);
        /* class members are NOT module surface (the class is the
         * surface unit -- the C# type-nesting precedent) */
        CHECK(exports_count_named(&exports, "start") == 0);
        CHECK(exports_count_named(&exports, "id") == 0);
        /* the enumerator boundary: enum-class constants are NOT
         * surface v1 (the documented C boundary, shared) */
        CHECK(exports_count_named(&exports, "READ") == 0);
        CHECK(exports_count_named(&exports, "WRITE") == 0);
        oc_surface_exports_destroy(&exports);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: namespaces, templates, extern "C" and the climb -------------- */

static void unit_cpp_toplevel_climb(void) {
    static const char src[] = "#ifndef WIDGET_HPP\n"
                              "#define WIDGET_HPP\n"
                              "\n"
                              "/* named namespace body: the members ARE surface */\n"
                              "namespace app {\n"
                              "struct guarded { int a; };\n"
                              "int app_fn(int n);\n"
                              "}\n"
                              "\n"
                              "/* ANONYMOUS namespace: internal linkage, never surface */\n"
                              "namespace {\n"
                              "int hidden_state = 1;\n"
                              "int hidden_fn2(void) { return 2; }\n"
                              "}\n"
                              "\n"
                              "/* template_declaration is a wrapper: the class template\n"
                              " * and the function template are surface */\n"
                              "template <typename T>\n"
                              "struct Buffer { T data[4]; };\n"
                              "template <typename T>\n"
                              "T pick2(T a) { return a; }\n"
                              "\n"
                              "extern \"C\" int exported_fn(int n);\n"
                              "extern \"C\" { struct inner2 { int b; }; int also_fn(int n); }\n"
                              "\n"
                              "#endif\n"
                              "/* inside a function body: NOT top level, never surface */\n"
                              "int wrapper2(void) {\n"
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
        parse_one(p, &g, "unit/guarded.hpp", src, &out);

        /* every named-namespace member is surface through the guard */
        CHECK(exports_count_named(&exports, "guarded") == 1);
        CHECK(exports_count_named(&exports, "app_fn") == 1);
        /* the anonymous namespace is internal linkage */
        CHECK(exports_count_named(&exports, "hidden_state") == 0);
        CHECK(exports_count_named(&exports, "hidden_fn2") == 0);
        /* template wrappers climb to translation_unit */
        CHECK(exports_count_named(&exports, "Buffer") == 1);
        CHECK(exports_count_named(&exports, "pick2") == 1);
        /* extern "C", both forms */
        CHECK(exports_count_named(&exports, "exported_fn") == 1);
        CHECK(exports_count_named(&exports, "inner2") == 1);
        CHECK(exports_count_named(&exports, "also_fn") == 1);
        CHECK(exports_count_named(&exports, "WIDGET_HPP") == 1); /* the #define */
        /* a function-body local never is */
        CHECK(exports_count_named(&exports, "local_var") == 0);
        CHECK(exports_count_named(&exports, "wrapper2") == 1);
        oc_surface_exports_destroy(&exports);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the main fact (the entry-point parse fact) ------------------- */

static void unit_cpp_main_fact(void) {
    oc_arena a;
    oc_arena_init(&a);

    static const char with_main[] = "#include \"x.hpp\"\n"
                                    "\n"
                                    "int main() {\n"
                                    "    return 0;\n"
                                    "}\n";
    static const char with_argc[] = "int main(int argc, char **argv) {\n"
                                    "    return 0;\n"
                                    "}\n";
    static const char member_main[] = "struct Holder {\n"
                                      "    int main() { return 0; }\n"
                                      "};\n";
    static const char qualified_main[] = "struct Q { int main(); };\n"
                                         "int Q::main() { return 0; }\n";
    static const char prototype_only[] = "int main();\n"
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
        {with_main, 1},      {with_argc, 1},
        {member_main, 0},    /* a member function is not THE main */
        {qualified_main, 0}, /* an out-of-class definition is not free-standing */
        {prototype_only, 0}, {no_main, 0},
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
            parse_one(p, &g, "unit/main.cpp", cases[i].src, &out);
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

/* --- unit: resolver v1-cpp (the shared include implementation) ---------- */

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

static void unit_cpp_resolver(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    /* the SPRINT-20 resolver fixture tree, resolved with OC_LANG_CPP
     * records: v1-cpp shares the v1-c implementation verbatim (the C++
     * preprocessor include model). */
    char root[1024];
    snprintf(root, sizeof(root), "%s/tests/fixtures/cpp_resolver", OC_REPO_ROOT);
    char paths[10][1400];
    static const char *const REL[] = {
        "src/main.cpp", "src/util/log.hpp",     "src/util/log.cpp",      "third/p.hpp",
        "third/p.cpp",  "include/outer.hpp",    "include/sub/inner.hpp", "cyc/a.hpp",
        "cyc/b.hpp",    "cyc/cycle_driver.cpp",
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
    snprintf(from, sizeof(from), "%s/src/main.cpp", root);
    char log_h[1400];
    snprintf(log_h, sizeof(log_h), "%s/src/util/log.hpp", root);
    char p_h[1400];
    snprintf(p_h, sizeof(p_h), "%s/third/p.hpp", root);
    /* the quoted channel: relative to the INCLUDING file's directory */
    CHECK_STR_EQ(oc_resolve_c_include(cx, from, "include util/log.hpp", &a), log_h);
    /* the lexical .. pops across src/ */
    CHECK_STR_EQ(oc_resolve_c_include(cx, from, "include ../third/p.hpp", &a), p_h);
    /* the system channel NEVER resolves (recorded-and-unresolved) */
    CHECK(oc_resolve_c_include(cx, from, "system vector", &a) == NULL);
    /* guards repeated at the resolver (defense in depth) */
    CHECK(oc_resolve_c_include(cx, from, "include /abs/path.hpp", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "include back\\slash.hpp", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "include ", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "include missing.hpp", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "wat", &a) == NULL);
    CHECK(oc_resolve_c_include(cx, from, "", &a) == NULL);

    /* the nested include anchors at the INCLUDING FILE's directory */
    char outer[1400];
    snprintf(outer, sizeof(outer), "%s/include/outer.hpp", root);
    char inner_h[1400];
    snprintf(inner_h, sizeof(inner_h), "%s/include/sub/inner.hpp", root);
    CHECK_STR_EQ(oc_resolve_c_include(cx, outer, "include sub/inner.hpp", &a), inner_h);

    /* edges: set semantics (a header included twice collapses to one
     * edge) and the cycle (a.hpp <-> b.hpp) */
    oc_import_vec v;
    oc_imports_init(&v);
    CHECK(oc_imports_push(&v, &a, from, "include util/log.hpp", OC_LANG_CPP));
    CHECK(oc_imports_push(&v, &a, from, "include util/log.hpp", OC_LANG_CPP)); /* dup */
    CHECK(oc_imports_push(&v, &a, from, "system vector", OC_LANG_CPP));        /* unresolved */
    char log_c[1400];
    snprintf(log_c, sizeof(log_c), "%s/src/util/log.cpp", root);
    CHECK(oc_imports_push(&v, &a, log_c, "include log.hpp", OC_LANG_CPP));
    char a_h[1400], b_h[1400];
    snprintf(a_h, sizeof(a_h), "%s/cyc/a.hpp", root);
    snprintf(b_h, sizeof(b_h), "%s/cyc/b.hpp", root);
    CHECK(oc_imports_push(&v, &a, a_h, "include b.hpp", OC_LANG_CPP));
    CHECK(oc_imports_push(&v, &a, b_h, "include a.hpp", OC_LANG_CPP));
    /* 4 distinct edges: main->log.hpp (set collapse), log.cpp->log.hpp,
     * a.hpp->b.hpp, b.hpp->a.hpp */
    CHECK(oc_imports_resolve_ex(&g, &v, &a, roots, 1) == 4);
    CHECK(has_edge(&g, from, log_h));
    CHECK(has_edge(&g, log_c, log_h));
    CHECK(has_edge(&g, a_h, b_h));
    CHECK(has_edge(&g, b_h, a_h));
    oc_imports_destroy(&v);

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the sniff (parse facts only) --------------------------------- */

static void unit_cpp_sniff(void) {
    /* C++-distinctive: namespace + class, clean under cpp */
    CHECK(oc_parser_sniff_header("namespace app {\nclass W { int f(void); };\n}\n",
                                 strlen("namespace app {\nclass W { int f(void); };\n}\n")) ==
          OC_SNIFF_CPP_POSITIVE);
    /* a C++ template is distinctive too */
    CHECK(oc_parser_sniff_header("template <typename T>\nstruct B { T t; };\n",
                                 strlen("template <typename T>\nstruct B { T t; };\n")) ==
          OC_SNIFF_CPP_POSITIVE);
    /* C-compatible content, no distinctive node: inconclusive (the
     * project-root default decides) */
    CHECK(oc_parser_sniff_header(
              "struct point { int x; int y; };\nint add(int a, int b);\n",
              strlen("struct point { int x; int y; };\nint add(int a, int b);\n")) ==
          OC_SNIFF_INCONCLUSIVE);
    /* positively C: the K&R old-style definition -- real C code,
     * structurally impossible in C++ (the cpp grammar ERRORS on it
     * while the C grammar parses it clean; keyword identifiers like
     * `int class = 3;` do NOT work -- tree-sitter's error recovery
     * parses them clean under cpp, probed at adoption) */
    CHECK(oc_parser_sniff_header("int old(a, b) int a; int b;\n{ return a + b; }\n",
                                 strlen("int old(a, b) int a; int b;\n{ return a + b; }\n")) ==
          OC_SNIFF_C_POSITIVE);
    /* empty content: inconclusive, never a fabricated verdict */
    CHECK(oc_parser_sniff_header("", 0) == OC_SNIFF_INCONCLUSIVE);
}

/* --- unit: the .h routing treatment (pack.c) ---------------------------- */

static void unit_cpp_routing(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_pack_set ps;
    oc_packs_init(&ps, &a);
    CHECK(oc_packs_load_dir(&ps, OC_REPO_ROOT "/packs", &st) == OC_OK);
    const oc_pack *c_pack = oc_packs_find(&ps, "c");
    const oc_pack *cpp_pack = oc_packs_find(&ps, "cpp");
    CHECK(c_pack != NULL && cpp_pack != NULL);

    const char *cpp_project[] = {OC_REPO_ROOT "/tests/fixtures/cpp/rollup.cpp",
                                 OC_REPO_ROOT "/tests/fixtures/cpp/local_util.h"};
    const char *c_project[] = {OC_REPO_ROOT "/tests/fixtures/c/mathx.c",
                               OC_REPO_ROOT "/tests/fixtures/c/local_util.h"};

    oc_route_reason reason = OC_ROUTE_EXT;
    /* unambiguous extensions stay plain extension matches */
    CHECK(oc_packs_route_source(&ps, "x/dir/file.c", "int f(void) { return 0; }\n", 26, cpp_project,
                                2, &a, &reason) == c_pack);
    CHECK(reason == OC_ROUTE_EXT);
    CHECK(oc_packs_route_source(&ps, "x/dir/file.cpp", "int f(void) { return 0; }\n", 26, c_project,
                                2, &a, &reason) == cpp_pack);
    CHECK(reason == OC_ROUTE_EXT);
    CHECK(oc_packs_route_source(&ps, "x/dir/file.hpp", "class W { };\n", 13, c_project, 2, &a,
                                &reason) == cpp_pack);
    CHECK(reason == OC_ROUTE_EXT);

    /* .h with C++-distinctive content: the SNIFF claims cpp */
    const char *cppish = "namespace app {\nclass W { int f(void); };\n}\n";
    CHECK(oc_packs_route_source(&ps, "x/dir/widget.h", cppish, strlen(cppish), c_project, 2, &a,
                                &reason) == cpp_pack);
    CHECK(reason == OC_ROUTE_H_SNIFF_CPP);

    /* .h positively C (the K&R definition errors under cpp, parses
     * clean under C): the SNIFF claims c */
    const char *c_only = "int old(a, b) int a; int b;\n{ return a + b; }\n";
    CHECK(oc_packs_route_source(&ps, "x/dir/old_names.h", c_only, strlen(c_only), cpp_project, 2,
                                &a, &reason) == c_pack);
    CHECK(reason == OC_ROUTE_H_SNIFF_C);

    /* C-compatible .h in a cpp project: the ROOT DEFAULT claims cpp */
    const char *plain = "struct point { int x; int y; };\nint add(int a, int b);\n";
    CHECK(oc_packs_route_source(&ps, "x/dir/plain.h", plain, strlen(plain), cpp_project, 2, &a,
                                &reason) == cpp_pack);
    CHECK(reason == OC_ROUTE_H_DEFAULT_CPP);

    /* C-compatible .h in a C-only project: the ROOT DEFAULT claims c */
    CHECK(oc_packs_route_source(&ps, "x/dir/plain.h", plain, strlen(plain), c_project, 2, &a,
                                &reason) == c_pack);
    CHECK(reason == OC_ROUTE_H_DEFAULT_C);

    oc_packs_destroy(&ps);
    oc_arena_destroy(&a);
}

/* --- unit: gcovr Cobertura through the existing reader ------------------ */

static void unit_cpp_cobertura(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_coverage cov;
    oc_coverage_init(&cov);
    const char *paths[] = {OC_REPO_ROOT "/tests/fixtures_sprint21/gcovr_cobertura_cpp.xml"};
    CHECK(oc_coverage_load(&cov, paths, 1, &a, &st) == OC_OK);
    CHECK(cov.n_files == 2); /* rollup.cpp + decls.cpp */

    size_t anchors = 0, covered = 0;
    int hit = oc_coverage_lookup(&cov, "tests/fixtures/cpp/rollup.cpp", 50, 52, &anchors, &covered);
    CHECK(hit == 1);
    CHECK(anchors == 3);
    CHECK(covered == 3);
    hit = oc_coverage_lookup(&cov, "tests/fixtures/cpp/decls.cpp", 15, 17, &anchors, &covered);
    CHECK(hit == 1);
    CHECK(anchors == 3);
    CHECK(covered == 2); /* the closing line carries hits="0" */
    /* a path no class element carries: not found, never guessed */
    hit = oc_coverage_lookup(&cov, "tests/fixtures/cpp/mathx.cpp", 1, 100, &anchors, &covered);
    CHECK(hit == 0);
    oc_coverage_destroy(&cov);
    oc_arena_destroy(&a);
}

/* --- unit: the honest supply tier (nothing discovered, nothing
 * fabricated) ------------------------------------------------------------ */

static void unit_cpp_supply_honest(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_manifest_vec mv;
    const char *files[] = {
        OC_REPO_ROOT "/tests/fixtures/cpp/rollup.cpp",
        OC_REPO_ROOT "/tests/fixtures/cpp/decls.cpp",
        OC_REPO_ROOT "/tests/fixtures/cpp/local_util.h",
    };
    CHECK(oc_supply_load_manifests(&mv, files, 3, &a, &st) == OC_OK);
    /* C++ has NO canonical manifest reader in v1 (no fabricated
     * pkg-config/vcpkg/conan tier): manifests_scanned stays 0 and the
     * report meta says so */
    CHECK(mv.n == 0);
    oc_manifests_destroy(&mv);
    oc_arena_destroy(&a);
}

/* --- integration: corpus A (structural), routing and the pack ---------- */

static void unit_cpp_corpus_a(void) {
    char buf[65536];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/cpp", NULL, buf, sizeof(buf));
    /* parse clean, both C-family packs loaded */
    CHECK(strstr(buf, "\"files_analyzed\":14") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(strstr(buf, "\"cpp\"") != NULL);
    CHECK(strstr(buf, "\"c\"") != NULL);
    /* the routing block: types.h SNIFFED to cpp, local_util.h by the
     * ROOT DEFAULT to cpp (the corpus carries .cpp files) */
    CHECK(strstr(buf, "\"header_routing\":") != NULL);
    CHECK(strstr(buf, "\"sniffed_cpp\":1") != NULL);
    CHECK(strstr(buf, "\"sniffed_c\":0") != NULL);
    CHECK(strstr(buf, "\"default_cpp\":1") != NULL);
    CHECK(strstr(buf, "\"default_c\":0") != NULL);
    /* the enabled structural rules fire with the contracted counts */
    CHECK(json_count_rule(buf, "OC001") == 1); /* longfn 63 lines */
    CHECK(json_count_rule(buf, "OC002") == 1); /* todo.cpp marker */
    CHECK(json_count_rule(buf, "OC004") == 1); /* sum26 comp 35 */
    CHECK(json_count_rule(buf, "OC006") == 12);
    CHECK(json_count_rule(buf, "OC008") == 2); /* the dup pair */
    CHECK(json_count_rule(buf, "OC011") == 1); /* cfg/secret.hpp */
    /* OC003/OC005 (import-driven: the one resolved include marks its
     * target used) and OC007/OC009/OC010 (inputless -- the honest C++
     * supply tier) never fire on this corpus */
    CHECK(json_count_rule(buf, "OC003") == 0);
    CHECK(json_count_rule(buf, "OC005") == 0);
    CHECK(json_count_rule(buf, "OC007") == 0);
    CHECK(json_count_rule(buf, "OC009") == 0);
    CHECK(json_count_rule(buf, "OC010") == 0);

    /* the OC006 set must NOT contain main_app.cpp (claimed by the main
     * parse fact) nor local_util.h (included twice) */
    CHECK(strstr(buf, "main_app.cpp\"") == NULL);
    CHECK(strstr(buf, "local_util.h\"") == NULL);
}

/* --- integration: the cpp pack declares full scope ---------------------- */

static void unit_cpp_pack(void) {
    char buf[32768];
    snprintf(buf, sizeof(buf), "cd %s && %s packs --packs-dir %s/packs 2>/dev/null", OC_REPO_ROOT,
             OC_OC_BIN, OC_REPO_ROOT);
    FILE *fp = popen(buf, "r");
    char *json = malloc(1 << 19);
    size_t n = 0;
    CHECK(fp != NULL);
    if (fp && json) {
        n = fread(json, 1, (1 << 19) - 1, fp);
        json[n] = '\0';
        pclose(fp);
    }
    CHECK(json != NULL);
    if (json) {
        const char *entry = strstr(json, "\"id\":\"cpp\"");
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
            CHECK(strstr(seg, "\"grammar\":\"cpp\"") != NULL);
            CHECK(strstr(seg, "\".cpp\"") != NULL);
            CHECK(strstr(seg, "\".h\"") != NULL);
            CHECK(strstr(seg, "\"entry_points\":[]") != NULL);
        }
        free(json);
    }
}

int main(void) {
    unit_cpp_symbols();
    unit_cpp_decision_table();
    unit_cpp_params();
    unit_cpp_import_records();
    unit_cpp_surface();
    unit_cpp_toplevel_climb();
    unit_cpp_main_fact();
    unit_cpp_resolver();
    unit_cpp_sniff();
    unit_cpp_routing();
    unit_cpp_cobertura();
    unit_cpp_supply_honest();
    unit_cpp_corpus_a();
    unit_cpp_pack();
    TEST_SUMMARY();
}
