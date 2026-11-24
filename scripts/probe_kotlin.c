/* Probe tree-sitter-kotlin 0.3.8 shape facts (adoption-time verification,
 * the C probe pattern: every contract shape guess is probed against the
 * PINNED grammar with an actual parse before any adoption code exists).
 * The grammar carries an external scanner (src/scanner.c): automatic
 * semicolons, string interpolation start/end/content, nested multiline
 * comments, the safe-navigation token. LANGUAGE_VERSION 14, accepted by
 * the vendored runtime v0.27.2 (the same version the cpp pin carries). */
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
extern const TSLanguage *tree_sitter_kotlin(void);

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
    ts_parser_set_language(p, tree_sitter_kotlin());
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
    run("declarations: top-level fun/class/object/interface/enum/val/var/typealias",
        "fun topFn(x: Int): Int { return x }\n"
        "class Top {\n}\n"
        "object Singleton {\n    val ready: Boolean\n        get() = true\n}\n"
        "interface Repo {\n    fun fetch(): Int\n    fun default(): Int { return 1 }\n}\n"
        "enum class Color { RED, GREEN }\n"
        "val greeting = \"hi\"\n"
        "var counter = 0\n"
        "typealias Handler = (Int) -> Unit\n");

    run("functions: params, defaults, vararg, return, expression body, extension, generic, suspend",
        "fun plain(a: Int, b: String) {}\n"
        "fun defaults(a: Int, b: Int = 2, name: String = \"x\") {}\n"
        "fun vararg(vararg items: String, tail: Int = 0) {}\n"
        "fun expr(a: Int): Int = a * 2\n"
        "fun String.shout(): String = uppercase()\n"
        "fun <T : Comparable<T>> maxOf2(a: T, b: T): T = if (a >= b) a else b\n"
        "suspend fun load(): Int { return 1 }\n"
        "inline fun hook(block: () -> Unit) { block() }\n"
        "fun abstractish(): Int  /* declaration w/o body in an open context */\n");

    run("classes: primary/secondary ctor, init, companion, nested, modifiers, data/abstract members",
        "open class Widget(val id: Int) {\n"
        "    val cached: Int = id * 2\n"
        "    init { require(id >= 0) }\n"
        "    constructor(): this(0)\n"
        "    fun scale(v: Int): Int = v * id\n"
        "    open fun render(): String = \"w$id\"\n"
        "    companion object Factory {\n        const val KIND = \"w\"\n    }\n"
        "    class Part(val name: String)\n"
        "    object Registry {\n        val n = 0\n    }\n"
        "}\n"
        "abstract class Base {\n    abstract fun run(): Int\n    fun ready(): Int { return 1 }\n}\n"
        "data class Point(val x: Int, val y: Int)\n"
        "sealed class Shape\n"
        "class CtorOnly {\n    constructor(x: Int) { }\n}\n");

    run("decisions: if/else-if, when(+subject, else), for, while, do-while, try/catch/finally, elvis, ops",
        "fun branches(a: Int, b: Int?, items: List<String>): Int {\n"
        "    if (a > 0) return 1 else if (a < 0) return -1\n"
        "    when (a) {\n"
        "        in 3..9 -> return 30\n"
        "        1, 2 -> return 20\n"
        "        else -> {}\n"
        "    }\n"
        "    when {\n"
        "        b != null -> return b\n"
        "        else -> return 0\n"
        "    }\n"
        "    val e = b ?: -1\n"
        "    val ok = a > 0 && b != null || items.isNotEmpty()\n"
        "    for (it in items) { if (it.isEmpty()) continue }\n"
        "    var n = a\n"
        "    while (n > 0) { n-- }\n"
        "    do { n++ } while (n < 3)\n"
        "    try { n = 1 / a } catch (ex: ArithmeticException) { n = -1 } catch (ex: Exception) { n = 0 } finally { }\n"
        "    return n\n"
        "}\n"
        "fun asWhen(x: Any): Int = when (x) { is Int -> x; else -> 0 }\n");

    run("imports + package: single, wildcard, alias, member-of-object, package header",
        "package com.example.app\n"
        "import com.foo.Bar\n"
        "import com.baz.*\n"
        "import com.qux.Kit as Kit2\n"
        "import com.util.Helpers.make\n"
        "import java.util.ArrayList\n");

    run("main fact: top-level main definitions claim; member main does not",
        "fun main() { }\n"
        "fun main(args: Array<String>) { }\n"
        "class App {\n"
        "    fun main() { }\n"
        "}\n"
        "fun main2(x: Int) { }\n");

    run("scanner shapes: templates, raw strings, nested comments, semicolon-free lines, safe-nav",
        "val tmpl = \"a${x + 1}b$c\"\n"
        "val raw = \"\"\"line1\nline2 ${q}\"\"\"\n"
        "val nested = /* outer /* inner */ still outer */ 1\n"
        "val chained = repo?.fetch()?.also { it + 1 }\n"
        "// line comment\n"
        "/** doc */\n");

    run("objects/lambdas/initializers: object expression, companion w/o name, init, trailing lambda",
        "class Host {\n"
        "    companion object {\n        val kind = \"host\"\n    }\n"
        "    private val strategy = object : Strategy {\n        override fun run(): Int { return 1 }\n    }\n"
        "    val late by lazy { compute() }\n"
        "    fun apply(block: () -> Unit) { block() }\n"
        "    fun use() {\n        apply { }\n    }\n"
        "    fun compute(): Int { return 7 }\n"
        "}\n");

    run("surface shapes: interface members, nested fun classes, property accessors, generics with where",
        "interface Sourced {\n"
        "    val name: String\n"
        "    fun source(): String {\n        return name\n    }\n"
        "}\n"
        "class Impl(override val name: String) : Sourced\n"
        "fun makeInner(): Runnable =\n    object : Runnable {\n        override fun run() { }\n    }\n"
        "class Box<T> where T : Comparable<T> {\n    var item: T? = null\n}\n"
        "val lazyProp: String\n    get() = \"computed\"\n");

    run("error recovery boundary: a broken token stream still records what parsed",
        "fun broken(: Int { }\n"
        "fun fine(a: Int): Int = a\n");

    printf("TOTAL has_error count: %u\n", g_errs);
    return 0;
}
