/* Probe tree-sitter-cpp 0.23.4 shape facts (adoption-time verification,
 * the C probe pattern: every contract shape guess is probed against the
 * PINNED grammar with an actual parse before any adoption code exists).
 * The grammar carries an external scanner (src/scanner.c): raw strings,
 * angle-bracket disambiguation. The FULL C grammar is inlined at
 * generation time (grammar.js requires tree-sitter-c/grammar), so
 * parser.c + scanner.c are self-contained. */
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
extern const TSLanguage *tree_sitter_cpp(void);

static uint32_t g_errs = 0;

static void dump(TSNode n, const char *src, int depth) {
    const char *t = ts_node_type(n);
    int is_named = ts_node_is_named(n);
    for (int i = 0; i < depth; i++) fputs("  ", stdout);
    printf("%s%s", t, is_named ? "" : " (anon)");
    if (ts_node_is_error(n)) printf(" ERROR");
    if (!is_named) {
        uint32_t s = ts_node_start_byte(n), e = ts_node_end_byte(n);
        printf(" [%.*s]", (int)(e - s), src + s);
    }
    printf("\n");
    uint32_t nc = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc; i++) {
        const char *fname = ts_node_field_name_for_child(n, i);
        TSNode c = ts_node_child(n, i);
        if (fname && ts_node_is_named(c)) {
            for (int j = 0; j < depth + 1; j++) fputs("  ", stdout);
            printf("field %s:\n", fname);
            dump(c, src, depth + 2);
        } else {
            dump(c, src, depth + 1);
        }
    }
}

static void run(const char *label, const char *src) {
    TSParser *p = ts_parser_new();
    ts_parser_set_language(p, tree_sitter_cpp());
    TSTree *t = ts_parser_parse_string(p, NULL, src, (uint32_t)strlen(src));
    TSNode root = ts_tree_root_node(t);
    printf("===== %s =====\n", label);
    dump(root, src, 0);
    uint32_t had_err = ts_node_has_error(root);
    printf("has_error: %u\n", had_err);
    g_errs += had_err;
    ts_tree_delete(t);
    ts_parser_delete(p);
}

int main(void) {
    run("definitions: free fn, inline method, out-of-class, ctor, dtor, operator",
        "int compute(int n) { return n; }\n"
        "struct Widget {\n"
        "    int scale(int v) { return v; }\n"
        "    Widget();\n"
        "    ~Widget();\n"
        "    bool operator==(const Widget &o) const { return true; }\n"
        "    static int make(void) { return 0; }\n"
        "};\n"
        "int Widget::scale(int v) { return v + 1; }\n"
        "Widget::Widget() {}\n"
        "Widget::~Widget() {}\n");

    run("templates: function template, class template, default args, pack",
        "template <typename T>\n"
        "T pick(T a, T b) { return a; }\n"
        "template <typename T, int N = 4>\n"
        "struct Buffer {\n"
        "    T data[N];\n"
        "    T &at(int i) { return data[i]; }\n"
        "};\n"
        "template <typename... Args>\n"
        "void emit(Args &&...args) { (void)sizeof...(args); }\n");

    run("params: named, abstract, reference, pointer, default value, variadic",
        "void full(int a, char *b) { (void)a; (void)b; }\n"
        "void abstract(int, char *);\n"
        "void refs(int &count, const int &limit, char *buf) { (void)count; (void)limit; (void)buf; }\n"
        "void defaults(int a, int b = 2, const char *name = \"x\") { (void)a; (void)b; (void)name; }\n"
        "void ellipsis(const char *fmt, ...) { (void)fmt; }\n");

    run("decisions: if/else-if, loops, range-for, switch, ternary, catch, ops, C++17 if",
        "int f(int a, int b) {\n"
        "    if (a > 0) return 1;\n"
        "    else if (a < 0) return -1;\n"
        "    else return 0;\n"
        "}\n"
        "int g(int n, int arr[4]) {\n"
        "    while (n--) {}\n"
        "    do {} while (0);\n"
        "    for (int i = 0; i < n && i < 10; i++) {}\n"
        "    for (int v : arr) { (void)v; }\n"
        "    switch (n) {\n"
        "    case 1: return 1;\n"
        "    case 2: case 3: return 2;\n"
        "    default: break;\n"
        "    }\n"
        "    int t = n > 0 ? 1 : 0;\n"
        "    if (int k = n; k > 3) { t = k; }\n"
        "    try { t += n; } catch (const int &e) { t = e; } catch (...) { t = 0; }\n"
        "    return t || n;\n"
        "}\n");

    run("preproc: include channels, macros, guard",
        "#include <string>\n"
        "#include \"local.hpp\"\n"
        "#include PREFIX \"extra.hpp\"\n"
        "#define LIMIT 16\n"
        "#define SQR(x) ((x) * (x))\n"
        "#ifdef FAST\n"
        "#endif\n");

    run("surface: class, namespaces, alias, using, typedef, enum class, guards",
        "namespace app {\n"
        "class Engine {\n"
        "public:\n"
        "    void start(void);\n"
        "    int id = 0;\n"
        "};\n"
        "}\n"
        "namespace {\n"
        "int hidden = 1;\n"
        "}\n"
        "namespace outer::inner {\n"
        "struct Node { int v; };\n"
        "}\n"
        "using BufferInt = Buffer<int>;\n"
        "using app::Engine;\n"
        "using namespace std;\n"
        "typedef unsigned long size_word;\n"
        "enum class Mode { READ, WRITE };\n"
        "#ifndef GUARD_HPP\n"
        "#define GUARD_HPP\n"
        "struct Guarded { int ok; };\n"
        "#endif\n");

    run("extern C and linkage",
        "extern \"C\" {\n"
        "int bridge(int x) { return x; }\n"
        "}\n"
        "extern \"C\" int bridge2(int x) { return x; }\n");

    run("main fact: definition claims, prototype does not",
        "int main() { return 0; }\n"
        "int other_main(void);\n"
        "int main2(int argc, char **argv) { (void)argc; (void)argv; return 0; }\n");

    run("scanner + modern shapes: raw string, lambda, attribute, digit separator",
        "const char *pat = R\"x(raw [\\w]+ text)x\";\n"
        "int make(void) {\n"
        "    auto fn = [](int v) -> int { return v > 0 ? v : -v; };\n"
        "    [[maybe_unused]] int big = 1'000'000;\n"
        "    return fn(big);\n"
        "}\n"
        "auto trailing(int v) -> int { return v; }\n");

    run("concepts/requires and static assert (modern boundary shapes)",
        "template <typename T>\n"
        "concept Numeric = requires(T a, T b) { a + b; };\n"
        "static_assert(sizeof(int) >= 4, \"int too small\");\n"
        "template <Numeric T>\n"
        "T sum2(T a, T b) { return a + b; }\n");

    printf("TOTAL has_error count: %u\n", g_errs);
    return 0;
}
