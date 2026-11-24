/* Probe tree-sitter-c 0.23.4 shape facts (adoption-time verification, the
 * PHP/Ruby/Rust probe pattern: every contract shape guess is probed against
 * the PINNED grammar with an actual parse before any adoption code exists).
 * The grammar has NO external scanner: tree_sitter_c() + parser.c only. */
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
extern const TSLanguage *tree_sitter_c(void);

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
    ts_parser_set_language(p, tree_sitter_c());
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
    run("definitions: plain, pointer-return, static, fnptr-return",
        "int main(void) { return 0; }\n"
        "char *dup_str(const char *s) { return 0; }\n"
        "static unsigned count(void) { return 0u; }\n"
        "int (*get_step(int n))(int) { return 0; }\n");

    run("declarations: prototype, variables, arrays, K&R",
        "int add(int a, int b);\n"
        "extern unsigned flags;\n"
        "int table[16] = {1, 2};\n"
        "int old(a, b) int a; int b; { return a + b; }\n");

    run("types: struct tag, anonymous struct var, typedef, enum",
        "struct point { int x; int y; };\n"
        "struct { int a; } anon;\n"
        "typedef struct point point_t;\n"
        "typedef int (*handler)(void *, size_t);\n"
        "enum color { RED, GREEN = 2, BLUE };\n");

    run("preproc: include channels, macros",
        "#include <stdio.h>\n"
        "#include \"local.h\"\n"
        "#include PREFIX \"extra.h\"\n"
        "#define MAX 16\n"
        "#define SQR(x) ((x) * (x))\n"
        "#ifdef FAST\n"
        "#endif\n");

    run("params: named, unnamed, variadic, fnptr param",
        "void full(int a, char *b);\n"
        "void abstract(int, char *);\n"
        "void variadic(const char *fmt, ...);\n"
        "void cb(int (*visit)(int item, void *ctx), void *ctx);\n");

    run("decisions: if/else/else-if, loops, switch, ternary, ops",
        "int f(int a, int b) {\n"
        "    if (a > 0) return 1;\n"
        "    else if (a < 0) return -1;\n"
        "    else return 0;\n"
        "}\n"
        "int g(int n) {\n"
        "    while (n--) {}\n"
        "    do {} while (0);\n"
        "    for (int i = 0; i < n && i < 10; i++) {}\n"
        "    switch (n) {\n"
        "    case 1: return 1;\n"
        "    case 2: case 3: return 2;\n"
        "    default: break;\n"
        "    }\n"
        "    int t = n > 0 ? 1 : 0;\n"
        "    return t || n;\n"
        "}\n");

    run("comments: line, block, doc",
        "// line comment\n"
        "/* block\n"
        "   comment */\n"
        "/** doc comment */\n"
        "int x = 1;\n");

    printf("TOTAL has_error count: %u\n", g_errs);
    return 0;
}
