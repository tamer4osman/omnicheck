/* Probe tree-sitter-scala 0.23.4 shape facts (SPRINT-24 adoption-time
 * verification, the C probe pattern: every contract shape guess is probed
 * against the PINNED grammar with an actual parse before any adoption code
 * exists). The grammar carries an external scanner (src/scanner.c):
 * outdent tokens for the significant-indentation shapes. LANGUAGE_VERSION
 * 14, accepted by the vendored runtime v0.27.2 (the same version every
 * other pin carries). */
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
extern const TSLanguage *tree_sitter_scala(void);

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
    ts_parser_set_language(p, tree_sitter_scala());
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
    run("declarations: top-level def/class/object/trait/val/var/typealias (Scala 2 brace style)",
        "def topFn(x: Int): Int = x\n"
        "class Top {\n"
        "}\n"
        "object Singleton {\n"
        "  val ready: Boolean = true\n"
        "}\n"
        "trait Repo {\n"
        "  def fetch(): Int\n"
        "  def default(): Int = 1\n"
        "}\n"
        "val greeting = \"hi\"\n"
        "var counter = 0\n"
        "typealias Handler = Int => Unit\n");

    run("declarations: Scala 3 enum/given/extension + indented body",
        "enum Color:\n"
        "  case Red, Green, Blue\n"
        "enum Planet(mass: Double):\n"
        "  case Earth extends Planet(5.97)\n"
        "given Ordering[Int] = Ordering.fromLessThan(_ < _)\n"
        "given intOrd: Ordering[Int] with\n"
        "  def compare(x: Int, y: Int) = x - y\n"
        "extension (s: String)\n"
        "  def shout: String = s.toUpperCase\n"
        "def scoped(): Unit =\n"
        "  val x = 1\n"
        "  println(x)\n");

    run("functions: params, defaults, vararg, return type, expression body, generic, implicit",
        "def plain(a: Int, b: String): Unit = {}\n"
        "def defaults(a: Int, b: Int = 2, name: String = \"x\") = ()\n"
        "def vararg(items: String*) = ()\n"
        "def expr(a: Int): Int = a * 2\n"
        "def max[T <: Comparable[T]](a: T, b: T): T = if a >= b then a else b\n"
        "def hook(implicit ev: Int) = ()\n"
        "def using(using ctx: Ctx) = ()\n");

    run("classes: ctor params, val members, nested, modifiers, case/sealed/abstract",
        "class Widget(val id: Int) {\n"
        "  val cached: Int = id * 2\n"
        "  def scale(v: Int): Int = v * id\n"
        "  class Part(val name: String)\n"
        "  object Registry { val n = 0 }\n"
        "}\n"
        "abstract class Base {\n"
        "  def run(): Int\n"
        "  def ready(): Int = 1\n"
        "}\n"
        "case class Point(x: Int, y: Int)\n"
        "sealed class Shape\n"
        "case object Unit\n");

    run("decisions: if/else, match cases + guards, for, while, do-while, try/catch/finally, ops",
        "def branches(a: Int, b: Option[Int], items: List[String]): Int = {\n"
        "  if (a > 0) 1 else if (a < 0) -1 else 0\n"
        "  a match {\n"
        "    case 1 | 2 => 20\n"
        "    case n if n > 3 => 30\n"
        "    case _ => 0\n"
        "  }\n"
        "  val e = b.getOrElse(-1)\n"
        "  val ok = a > 0 && b.isDefined || items.nonEmpty\n"
        "  for (it <- items) { if (it.isEmpty) () }\n"
        "  var n = a\n"
        "  while (n > 0) { n -= 1 }\n"
        "  do { n += 1 } while (n < 3)\n"
        "  try { n = 1 / a } catch { case ex: ArithmeticException => n = -1 } finally println(n)\n"
        "  n\n"
        "}\n"
        "def asMatch(x: Any): Int = x match { case i: Int => i; case _ => 0 }\n"
        "def ifThenElse(c: Boolean): Int = if c then 1 else 0\n");

    run("imports + package: single, wildcard Scala2/Scala3, selectors, rename, package clause",
        "package com.example.app\n"
        "import com.foo.Bar\n"
        "import com.baz._\n"
        "import com.baz2.*\n"
        "import com.qux.Kit as Kit2\n"
        "import com.qux.{Kit3, Kit4}\n"
        "import com.util.Helpers.make\n"
        "import java.util.ArrayList\n");

    run("main fact: object with main method, @main annotation, top-level def",
        "object App {\n"
        "  def main(args: Array[String]): Unit = { }\n"
        "  def helper(): Unit = ()\n"
        "}\n"
        "@main def hello(): Unit = println(\"hi\")\n"
        "object Other {\n"
        "  def main(args: Array[String]): Unit = ()\n"
        "}\n");

    run("scanner shapes: interpolated strings, raw strings, nested block comments",
        "val tmpl = s\"a${x + 1}b$c\"\n"
        "val raw = \"\"\"line1\nline2 ${q}\"\"\"\n"
        "val nested = /* outer /* inner */ still outer */ 1\n"
        "// line comment\n"
        "/** scaladoc */\n");

    run("surface shapes: trait members, nested defs, val with block body, generic bounds",
        "trait Sourced {\n"
        "  def name: String\n"
        "  def source(): String = name\n"
        "}\n"
        "class Impl(override val name: String) extends Sourced\n"
        "def makeRunnable(): Runnable =\n"
        "  new Runnable {\n"
        "    def run(): Unit = { () }\n"
        "  }\n"
        "class Box[T: Ordering] {\n"
        "  var item: Option[T] = None\n"
        "}\n"
        "val computed: String = {\n"
        "  val step = 1\n"
        "  s\"step-$step\"\n"
        "}\n");

    run("error recovery boundary: a broken token stream still records what parsed",
        "def broken(: Int = { }\n"
        "def fine(a: Int): Int = a\n");

    printf("TOTAL has_error count: %u\n", g_errs);
    return 0;
}
