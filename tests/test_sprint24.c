/* Sprint 24 (the Scala language adoption, wave-3 continues): the
 * contract of docs/LANGUAGES.md (Scala section), pinned unit by unit.
 *
 * Symbols: function_definition (by construction a def WITH a body);
 * the bodyless function_declaration (trait/abstract members) is NOT a
 * symbol (the C prototype discipline); lambdas are anonymous.
 * Complexity: if_expression (else-if nested), case_clause (EVERY
 * match/catch arm), for/while/do-while, and infix_expression gated to
 * && / || in C. Imports: the declared-fact resolver v1-scala (SHARED
 * with v1-kotlin; package clauses + top-level declaration names as
 * hints; wildcard fan-out; single requires the declared symbol; the
 * member shape is ambiguous and keeps a NULL bind; without hints
 * everything stays unresolved). Surface: named top-level declarations,
 * private excluded, members excluded, enum cases excluded, given and
 * extension containers not surface. Main fact: the object-main shape
 * and the @main annotation shape.
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
    in.grammar = "scala";
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

/* --- unit: symbols, spans, the shape contracts -------------------------- */

static void unit_scala_symbols(void) {
    static const char src[] = "/* free function: symbol, span = the whole declaration */\n"
                              "def compute(n: Int): Int = {\n"
                              "  n\n"
                              "}\n"
                              "\n"
                              "/* method: the name is the declaration's name-field identifier */\n"
                              "class Widget(val id: Int) {\n"
                              "  def scale(v: Int): Int = v * id\n"
                              "}\n"
                              "\n"
                              "/* extension method: the body belongs to its def, the extension\n"
                              " * container itself is never a symbol */\n"
                              "extension (s: String) {\n"
                              "  def shout: String = s.toUpperCase\n"
                              "}\n"
                              "\n"
                              "/* generic def: type parameters are a separate field; the wrapper\n"
                              " * adds no extent */\n"
                              "def max[T <: Comparable[T]](a: T, b: T): T = a\n"
                              "\n"
                              "/* object member: a symbol like any method */\n"
                              "object Registry {\n"
                              "  def lookup(key: String): Int = {\n"
                              "    key.length\n"
                              "  }\n"
                              "}\n"
                              "\n"
                              "/* body-less trait members: NOT symbols (the C prototype\n"
                              " * discipline; the default method IS one) */\n"
                              "trait Repo {\n"
                              "  def fetch(): Int\n"
                              "  def default(): Int = {\n"
                              "    1\n"
                              "  }\n"
                              "}\n"
                              "abstract class Base {\n"
                              "  def run(): Int\n"
                              "}\n"
                              "\n"
                              "/* lambda: anonymous construct, NOT a symbol (decisions count\n"
                              " * inward) */\n"
                              "def lambdaUser(v: Int): Int = {\n"
                              "  val fn = (x: Int) => x * 2\n"
                              "  fn(v)\n"
                              "}\n"
                              "\n"
                              "/* properties are NOT symbols (surface only) */\n"
                              "val greeting = \"hi\"\n"
                              "var counter = 0\n"
                              "\n"
                              "/* type alias: surface only, never a symbol */\n"
                              "type Handler = Int => Unit\n";

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
        parse_one(p, &g, "symbols.scala", src, &out);
        oc_parser_free(p);
    }
    /* the nine symbols: compute, scale, shout, max, lookup, default,
     * lambdaUser; fetch/run are body-less and stay out; fn is a lambda;
     * the extension container is never a symbol */
    CHECK(count_symbols_named(&g, "compute") == 1);
    CHECK(count_symbols_named(&g, "scale") == 1);
    CHECK(count_symbols_named(&g, "shout") == 1);
    CHECK(count_symbols_named(&g, "max") == 1);
    CHECK(count_symbols_named(&g, "lookup") == 1);
    CHECK(count_symbols_named(&g, "default") == 1);
    CHECK(count_symbols_named(&g, "lambdaUser") == 1);
    CHECK(count_symbols_named(&g, "fetch") == 0); /* bodyless trait member */
    CHECK(count_symbols_named(&g, "run") == 0);   /* bodyless abstract member */
    CHECK(count_symbols_named(&g, "fn") == 0);    /* lambda */
    const oc_node *sc = symbol_named(&g, "scale");
    CHECK(sc != NULL);
    if (sc) {
        /* the span is the WHOLE declaration (3 lines: def, body, close) */
        CHECK(sc->def_end_line >= sc->def_start_line);
        CHECK(sc->def_end_line - sc->def_start_line + 1 == 1);
    }
    CHECK(count_symbols_named(&g, "Handler") == 0);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: parameters (the name/type/default fields, the vararg) -------- */

static void unit_scala_params(void) {
    static const char src[] = "def plain(a: Int, b: String): Unit = ()\n"
                              "def defaults(a: Int, b: Int = 2, name: String = \"x\"): Unit = ()\n"
                              "def vararg(items: String*): Unit = ()\n"
                              "def generic[T](a: T, b: T): T = a\n"
                              "def implicitp(implicit ev: Int): Unit = ()\n";

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
        parse_one(p, &g, "params.scala", src, &out);
        oc_parser_free(p);
    }
    const oc_node *pl = symbol_named(&g, "plain");
    CHECK(pl != NULL && pl->n_params == 2);
    if (pl && pl->n_params == 2) {
        CHECK(STR_EQ(pl->params[0].name, "a") && STR_EQ(pl->params[0].type, "Int"));
        CHECK(STR_EQ(pl->params[1].name, "b") && STR_EQ(pl->params[1].type, "String"));
        CHECK(pl->params[0].has_default == 0 && pl->params[1].has_default == 0);
    }
    const oc_node *pd = symbol_named(&g, "defaults");
    CHECK(pd != NULL && pd->n_params == 3);
    if (pd && pd->n_params == 3) {
        CHECK(pd->params[0].has_default == 0);
        CHECK(pd->params[1].has_default == 1 && STR_EQ(pd->params[1].type, "Int"));
        CHECK(pd->params[2].has_default == 1 && STR_EQ(pd->params[2].type, "String"));
    }
    const oc_node *pv = symbol_named(&g, "vararg");
    CHECK(pv != NULL && pv->n_params == 1);
    if (pv && pv->n_params == 1) {
        /* the variadic collapses to the '...' marker, the Kotlin
         * vararg discipline */
        CHECK(STR_EQ(pv->params[0].name, "...") && STR_EQ(pv->params[0].type, "String"));
    }
    const oc_node *pg = symbol_named(&g, "generic");
    CHECK(pg != NULL && pg->n_params == 2);
    const oc_node *pi = symbol_named(&g, "implicitp");
    CHECK(pi != NULL && pi->n_params == 1);
    if (pi && pi->n_params == 1) {
        /* implicit/using modifiers are anonymous tokens: invisible to
         * the extractor, the parameter is still named */
        CHECK(STR_EQ(pi->params[0].name, "ev"));
    }
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

int main(void);

/* --- unit: decisions (the hand-counted inventory) ------------------------ */

static void unit_scala_decisions(void) {
    static const char src[] = "object Roller {\n"
                              "  def rollup(n: Int, items: List[Int]): Int = {\n"
                              "    if (n > 0) {\n"
                              "      return 1\n"
                              "    } else if (n < 0) {\n"
                              "      return -1\n"
                              "    }\n"
                              "    n match {\n"
                              "      case 1 | 2 => return 20\n"
                              "      case k if k > 3 => return 30\n"
                              "      case _ => {}\n"
                              "    }\n"
                              "    for (v <- items) {\n"
                              "      n + v\n"
                              "    }\n"
                              "    var i = n\n"
                              "    while (i > 100) {\n"
                              "      i -= 1\n"
                              "    }\n"
                              "    do {\n"
                              "      i += 1\n"
                              "    } while (i < 3)\n"
                              "    try {\n"
                              "      i = i / n\n"
                              "    } catch {\n"
                              "      case ex: ArithmeticException => i = -1\n"
                              "      case _ => i = 0\n"
                              "    }\n"
                              "    val ok = n > 0 && items.nonEmpty || i == 0\n"
                              "    if (ok) i else 0\n"
                              "  }\n"
                              "  def one: Int = 1\n"
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
        parse_one(p, &g, "rollup.scala", src, &out);
        oc_parser_free(p);
    }
    /* comp(rollup) = 1 + 13: if + else-if (2 if_expression) + three
     * match arms + for + while + do-while + TWO catch arms + && + || +
     * the trailing if */
    const oc_node *r = symbol_named(&g, "rollup");
    CHECK(r != NULL && r->complexity == 14);
    const oc_node *o = symbol_named(&g, "one");
    CHECK(o != NULL && o->complexity == 1);

    /* the non-boolean infix operators are NOT decisions: comp 1 */
    static const char src2[] = "object M {\n"
                               "  def arith(a: Int, b: Int): Int = {\n"
                               "    val x = a + b * 2 - 1\n"
                               "    val y = if (x > 0) x else -x\n"
                               "    y\n"
                               "  }\n"
                               "}\n";
    oc_graph g2;
    oc_graph_init(&g2, &a);
    oc_parser *p2 = oc_parser_new(&a, &st);
    CHECK(p2 != NULL);
    if (p2) {
        oc_parse_out out2;
        memset(&out2, 0, sizeof(out2));
        parse_one(p2, &g2, "arith.scala", src2, &out2);
        oc_parser_free(p2);
    }
    const oc_node *ar = symbol_named(&g2, "arith");
    /* the ternary-shaped if IS one decision; + - * are not */
    CHECK(ar != NULL && ar->complexity == 2);

    /* Scala 3 indented match arms still parse as case_clause */
    static const char src3[] = "object S3 {\n"
                               "  def pick(x: Any): Int =\n"
                               "    x match\n"
                               "      case i: Int => i\n"
                               "      case s: String => s.length\n"
                               "      case _ => 0\n"
                               "}\n";
    oc_graph g3;
    oc_graph_init(&g3, &a);
    oc_parser *p3 = oc_parser_new(&a, &st);
    CHECK(p3 != NULL);
    if (p3) {
        oc_parse_out out3;
        memset(&out3, 0, sizeof(out3));
        parse_one(p3, &g3, "s3match.scala", src3, &out3);
        CHECK(out3.diag.had_error == 0);
        oc_parser_free(p3);
    }
    const oc_node *pk = symbol_named(&g3, "pick");
    CHECK(pk != NULL && pk->complexity == 4); /* 1 + three arms */

    oc_graph_destroy(&g);
    oc_graph_destroy(&g2);
    oc_graph_destroy(&g3);
    oc_arena_destroy(&a);
}

/* --- unit: imports (the six shapes and the bind contract) ---------------- */

static void unit_scala_imports(void) {
    static const char src[] =
        "package app.use\n"
        "\n"
        "import com.example.lib.core\n"                 /* plain 3 segments: NULL bind */
        "import com.example.kit.Helper as H2\n"         /* Scala 3 tail rename: ORIGINAL bound */
        "import com.example.lib.types.*\n"              /* Scala 3 wildcard */
        "import com.example.lib.types2._\n"             /* Scala 2 wildcard, same node */
        "import com.example.lib.types.{TypeA, TypeB}\n" /* selector pair */
        "import com.example.lib.types.{TypeC => TC}\n"  /* Scala 2 selector rename */
        "import com.example.lib.Store.total\n"          /* member: NULL bind */
        "import single.Solo\n"                          /* one segment: bound */
        "import pkg.Type\n"                             /* two segments: bound */
        "\n"
        "def use(): Int = core(1)\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    oc_import_vec imports;
    oc_imports_init(&imports);
    oc_bind_vec binds;
    oc_surface_binds_init(&binds);
    if (p) {
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        out.imports = &imports;
        out.binds = &binds;
        parse_one(p, &g, "imports.scala", src, &out);
        oc_parser_free(p);
    }
    /* the wildcard spellings both normalize to the asterisk marker */
    CHECK(imports_count_spec(&imports, "com.example.lib.types.*") == 1);
    CHECK(imports_count_spec(&imports, "com.example.lib.types2.*") == 1);
    CHECK(imports_count_spec(&imports, "com.example.lib.core") == 1);
    CHECK(imports_count_spec(&imports, "com.example.kit.Helper") == 1);
    CHECK(imports_count_spec(&imports, "com.example.lib.types.TypeA") == 1);
    CHECK(imports_count_spec(&imports, "com.example.lib.types.TypeB") == 1);
    CHECK(imports_count_spec(&imports, "com.example.lib.types.TypeC") == 1);
    CHECK(imports_count_spec(&imports, "com.example.lib.Store.total") == 1);
    CHECK(imports_count_spec(&imports, "single.Solo") == 1);
    CHECK(imports_count_spec(&imports, "pkg.Type") == 1);
    CHECK(imports.n == 10);
    /* the binds: selectors/rename bind the ORIGINAL name (the alias is
     * local surface detail); one/two-segment plain imports bind the
     * last segment; three-or-more segments and wildcard/member keep
     * NULL (the ambiguous member shape, the fail-open whole-file
     * claim) */
    CHECK(bind_name_for(&binds, "com.example.lib.core") == NULL);
    CHECK(STR_EQ(bind_name_for(&binds, "com.example.kit.Helper"), "Helper"));
    CHECK(STR_EQ(bind_name_for(&binds, "com.example.lib.types.TypeA"), "TypeA"));
    CHECK(STR_EQ(bind_name_for(&binds, "com.example.lib.types.TypeB"), "TypeB"));
    CHECK(STR_EQ(bind_name_for(&binds, "com.example.lib.types.TypeC"), "TypeC"));
    CHECK(bind_name_for(&binds, "com.example.lib.Store.total") == NULL);
    CHECK(STR_EQ(bind_name_for(&binds, "single.Solo"), "Solo"));
    CHECK(STR_EQ(bind_name_for(&binds, "pkg.Type"), "Type"));
    CHECK(bind_name_for(&binds, "com.example.lib.types.*") == NULL);
    CHECK(bind_name_for(&binds, "com.example.lib.types2.*") == NULL);
    oc_imports_destroy(&imports);
    oc_surface_binds_destroy(&binds);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: module surface (the top-level and the private filter) -------- */

static void unit_scala_surface(void) {
    static const char src[] = "package app.shapes\n"
                              "\n"
                              "def topFn(n: Int): Int = n\n"
                              "\n"
                              "private def hidden(): Int = 1\n"
                              "\n"
                              "protected def shielded(): Int = 2\n"
                              "\n"
                              "class Top {\n"
                              "  def member(n: Int): Int = n\n"
                              "  object Nested {\n"
                              "    val inner = 3\n"
                              "  }\n"
                              "  class Part(val name: String)\n"
                              "}\n"
                              "\n"
                              "object Singleton\n"
                              "\n"
                              "trait Capable\n"
                              "\n"
                              "enum Color {\n"
                              "  case Red, Green\n"
                              "}\n"
                              "\n"
                              "type Alias = Int\n"
                              "\n"
                              "val topVal = 7\n"
                              "var topVar = 8\n"
                              "val (a, b) = (1, 2)\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    oc_export_vec exports;
    oc_surface_exports_init(&exports);
    oc_hint_vec hints;
    oc_surface_hints_init(&hints);
    if (p) {
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        out.exports = &exports;
        out.hints = &hints;
        parse_one(p, &g, "surface.scala", src, &out);
        oc_parser_free(p);
    }
    /* the top-level named declarations surface: def, class, object,
     * trait, enum, type alias, top-level val/var */
    CHECK(exports_count_named(&exports, "topFn") == 1);
    CHECK(exports_count_named(&exports, "Top") == 1);
    CHECK(exports_count_named(&exports, "Singleton") == 1);
    CHECK(exports_count_named(&exports, "Capable") == 1);
    CHECK(exports_count_named(&exports, "Color") == 1);
    CHECK(exports_count_named(&exports, "Alias") == 1);
    CHECK(exports_count_named(&exports, "topVal") == 1);
    CHECK(exports_count_named(&exports, "topVar") == 1);
    /* private: NOT surface (the C static rule's Kotlin spelling);
     * protected: package/superclass-visible, SURFACE (the Kotlin
     * treatment of internal) */
    CHECK(exports_count_named(&exports, "hidden") == 0);
    CHECK(exports_count_named(&exports, "shielded") == 1);
    /* members, enum CASES, the tuple pattern: not surface */
    CHECK(exports_count_named(&exports, "member") == 0);
    CHECK(exports_count_named(&exports, "Nested") == 0);
    CHECK(exports_count_named(&exports, "Part") == 0);
    CHECK(exports_count_named(&exports, "Red") == 0);
    CHECK(exports_count_named(&exports, "Green") == 0);
    CHECK(exports_count_named(&exports, "a") == 0);
    CHECK(exports_count_named(&exports, "b") == 0);
    /* every export is a top-level-symbol hint (the C# namespace-hint
     * channel): 9 exports -> 9 symbols on the one file's hint item */
    CHECK(hints.n == 1);
    CHECK(hints.items[0].n_symbols == 9);
    oc_surface_exports_destroy(&exports);
    oc_surface_hints_destroy(&hints);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the main fact (the object-main and @main shapes) -------------- */

static void unit_scala_main_fact(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);

    static const char claimed[] = "object App {\n"
                                  "  def main(args: Array[String]): Unit = {\n"
                                  "    ()\n"
                                  "  }\n"
                                  "  def helper(): Unit = ()\n"
                                  "}\n";
    static const char annotation[] = "@main def entry(): Unit = println(\"hi\")\n";
    /* a def named main OUTSIDE an object: no claim (the shape needs
     * the object wrapper) */
    static const char bare[] = "def main(args: Array[String]): Unit = ()\n";
    /* a member named othermain: no claim */
    static const char other[] = "object Other {\n"
                                "  def main2(args: Array[String]): Unit = ()\n"
                                "}\n";

    const char *const cases[] = {claimed, annotation, bare, other};
    const int expected[] = {1, 1, 0, 0};
    for (int i = 0; i < 4; i++) {
        oc_graph g2;
        oc_graph_init(&g2, &a);
        oc_parser *p2 = oc_parser_new(&a, &st);
        CHECK(p2 != NULL);
        oc_hint_vec hints;
        oc_surface_hints_init(&hints);
        if (p2) {
            oc_parse_out out;
            memset(&out, 0, sizeof(out));
            out.hints = &hints;
            char path[32];
            snprintf(path, sizeof(path), "main%d.scala", i);
            parse_one(p2, &g2, path, cases[i], &out);
            oc_parser_free(p2);
        }
        CHECK(hints.n == (size_t)expected[i]);
        if (expected[i])
            CHECK(hints.items[0].has_main == 1);
        oc_surface_hints_destroy(&hints);
        oc_graph_destroy(&g2);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the declared-fact resolver, corpus B inline ------------------- */

typedef struct {
    const char *path;
    const char *source;
} sc_res_file;

static void unit_scala_resolver(void) {
    /* corpus B, inline: the declared-fact contract end to end -- the
     * two-file package union, the wildcard fan-out, the selector pair,
     * the alias, the member shape's BOTH readings, the cycle, the
     * unresolved decoys, the default package. 10 files, 6 contracted
     * edges (the set semantics dedupe the alias and the selector
     * records onto the same file pair). */
    static const sc_res_file FILES[] = {
        {"core.scala", "package com.example.lib\n\n"
                       "class Service {\n  def run(): Int = 1\n}\n\n"
                       "def core(n: Int): Int = n + 1\n"},
        {"extra.scala", "package com.example.lib\n\n"
                        "def beta(n: Int): Int = n * 3\n"},
        {"typesr.scala", "package com.example.lib.types\n\n"
                         "class TypeA {\n  def id: Int = 1\n}\n\n"
                         "class TypeB {\n  def id: Int = 2\n}\n"},
        {"libuser.scala", "package app.use\n\n"
                          "import com.example.lib.core\n"
                          "import com.example.lib.types.*\n"
                          "import com.example.lib.types.{TypeA, TypeB}\n"
                          "import com.example.lib.Service as Svc\n"
                          "import com.example.lib.Store.total\n\n"
                          "def useAll(): Int = {\n"
                          "  core(1) + new TypeA().id + new TypeB().id + new Svc().run() +\n"
                          "    total(1, 2)\n"
                          "}\n"},
        {"store.scala", "package com.example.lib\n\n"
                        "object Store {\n  def total(a: Int, b: Int): Int = a + b\n}\n"},
        {"member.scala", "package com.example.lib.Store\n\n"
                         "def total(a: Int, b: Int): Int = a * b\n"},
        {"cyc_one.scala", "package com.example.cyc\n\nimport com.example.cyc.Two\n\n"
                          "class One {\n  val mate = new Two()\n}\n"},
        {"cyc_two.scala", "package com.example.cyc\n\nimport com.example.cyc.One\n\n"
                          "class Two {\n  val mate = new One()\n}\n"},
        {"decoy.scala", "package com.example.other\n\n"
                        "import java.util.ArrayList\nimport com.example.missing.Ghost\n\n"
                        "class TypeA {\n  def id: Int = 99\n}\n\n"
                        "def decoy(): Int = 0\n"},
        {"nopkg.scala", "def bare(n: Int): Int = n\n"},
    };
    const size_t N_FILES = sizeof(FILES) / sizeof(FILES[0]);
    CHECK(N_FILES == 10);

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_import_vec imports;
    oc_imports_init(&imports);
    oc_hint_vec hints;
    oc_surface_hints_init(&hints);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        oc_export_vec exports;
        oc_surface_exports_init(&exports);
        for (size_t i = 0; i < N_FILES; i++) {
            oc_parser_input in;
            memset(&in, 0, sizeof(in));
            in.path = FILES[i].path;
            in.source = FILES[i].source;
            in.len = strlen(FILES[i].source);
            in.grammar = "scala";
            oc_parse_out out;
            memset(&out, 0, sizeof(out));
            out.imports = &imports;
            out.exports = &exports;
            out.hints = &hints;
            CHECK(oc_parser_parse_file(p, &in, &g, &out, &st) == OC_OK);
            CHECK(out.diag.had_error == 0);
        }
        oc_surface_exports_destroy(&exports);
        oc_parser_free(p);
    }

    /* the edge pass with hints: exactly 6 edges, lexicographic fan-out,
     * set semantics (the alias resolves onto core like the single
     * import; the selectors onto typesr like the wildcard) */
    long n_edges = oc_imports_resolve_ex_hints(&g, &imports, &a, NULL, 0, &hints);
    CHECK(n_edges == 6);

    oc_node_id core = oc_graph_find_file(&g, "core.scala");
    oc_node_id extra = oc_graph_find_file(&g, "extra.scala");
    oc_node_id typesr = oc_graph_find_file(&g, "typesr.scala");
    oc_node_id libuser = oc_graph_find_file(&g, "libuser.scala");
    oc_node_id store = oc_graph_find_file(&g, "store.scala");
    oc_node_id member = oc_graph_find_file(&g, "member.scala");
    oc_node_id cyc_one = oc_graph_find_file(&g, "cyc_one.scala");
    oc_node_id cyc_two = oc_graph_find_file(&g, "cyc_two.scala");
    oc_node_id decoy = oc_graph_find_file(&g, "decoy.scala");
    CHECK(core != OC_NODE_INVALID && extra != OC_NODE_INVALID && typesr != OC_NODE_INVALID &&
          store != OC_NODE_INVALID && member != OC_NODE_INVALID && decoy != OC_NODE_INVALID);

    size_t user_to_core = 0, user_to_typesr = 0, user_to_store = 0, user_to_member = 0,
           user_to_extra = 0, decoy_edges = 0, cycle_edges = 0;
    for (size_t i = 0; i < g.n_edges; i++) {
        if (g.edges[i].kind != OC_EDGE_IMPORTS)
            continue;
        if (g.edges[i].from == libuser && g.edges[i].to == core)
            user_to_core++;
        if (g.edges[i].from == libuser && g.edges[i].to == typesr)
            user_to_typesr++;
        if (g.edges[i].from == libuser && g.edges[i].to == store)
            user_to_store++;
        if (g.edges[i].from == libuser && g.edges[i].to == member)
            user_to_member++;
        if (g.edges[i].from == libuser && g.edges[i].to == extra)
            user_to_extra++;
        if (g.edges[i].from == decoy)
            decoy_edges++;
        if ((g.edges[i].from == cyc_one && g.edges[i].to == cyc_two) ||
            (g.edges[i].from == cyc_two && g.edges[i].to == cyc_one))
            cycle_edges++;
    }
    CHECK(user_to_core == 1);   /* the single + the alias (set semantics) */
    CHECK(user_to_typesr == 1); /* wildcard + selectors (set semantics) */
    CHECK(user_to_store == 1);  /* the member shape's object reading */
    CHECK(user_to_member == 1); /* the member shape's package reading */
    CHECK(user_to_extra == 0);  /* nothing claims beta */
    CHECK(decoy_edges == 0);    /* java.util + com.example.missing stay out */
    CHECK(cycle_edges == 2);    /* OC003 == 1 on this corpus */

    /* the honest degradation: the SAME records without hints resolve
     * NOTHING (no declared facts, no mapping, none guessed) */
    oc_graph g2;
    oc_graph_init(&g2, &a);
    long n2 = oc_imports_resolve_ex(&g2, &imports, &a, NULL, 0);
    CHECK(n2 == 0);
    oc_graph_destroy(&g2);

    oc_graph_destroy(&g);
    oc_imports_destroy(&imports);
    oc_surface_hints_destroy(&hints);
    oc_arena_destroy(&a);
}

/* --- unit: the pack shape ------------------------------------------------ */

static void unit_scala_pack_shape(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_pack_set ps;
    oc_packs_init(&ps, &a);
    oc_status st;
    oc_status_clear(&st);
    char packs_dir[512];
    snprintf(packs_dir, sizeof(packs_dir), "%s/packs", OC_REPO_ROOT);
    CHECK(oc_packs_load_dir(&ps, packs_dir, &st) == OC_OK);
    const oc_pack *sc = NULL;
    for (size_t i = 0; i < ps.n_packs; i++) {
        if (strcmp(ps.packs[i].id, "scala") == 0)
            sc = &ps.packs[i];
    }
    CHECK(sc != NULL);
    if (sc) {
        CHECK(STR_EQ(sc->display_name, "Scala"));
        CHECK(STR_EQ(sc->grammar, "scala"));
        CHECK(sc->n_extensions == 2);
        CHECK(STR_EQ(sc->extensions[0], ".scala"));
        CHECK(STR_EQ(sc->extensions[1], ".sc"));
        CHECK(sc->n_entry_points == 0); /* the main parse fact claims */
        CHECK(sc->n_passes == 11);
        CHECK(STR_EQ(sc->passes[0], "OC001"));
        CHECK(STR_EQ(sc->passes[10], "OC011"));
        /* the pack routes a .scala file and an .sc script */
        const oc_pack *m = oc_packs_match_path(&ps, "src/Main.scala");
        CHECK(m == sc);
        m = oc_packs_match_path(&ps, "scripts/run.sc");
        CHECK(m == sc);
    }
    oc_packs_destroy(&ps);
    oc_arena_destroy(&a);
}

/* --- corpus A: the structural corpus, the contracted counts -------------- */

static void corpus_scala_a(void) {
    char buf[65536];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/scala", "check", buf, sizeof(buf));
    CHECK(strstr(buf, "\"files_analyzed\":13") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(json_count_rule(buf, "OC001") == 1); /* longfn.scala: 63 lines > 60 */
    CHECK(json_count_rule(buf, "OC002") == 2); /* the two TODO markers */
    CHECK(json_count_rule(buf, "OC004") == 1); /* sum26 comp 38 > 15 */
    CHECK(json_count_rule(buf, "OC006") ==
          9); /* 13 files: decls (entry), main_app (entry), widget+tools (imports) claimed */
    CHECK(json_count_rule(buf, "OC008") == 2); /* the dup pair */
    CHECK(json_count_rule(buf, "OC011") == 1); /* the credential val */
    CHECK(json_count_rule(buf, "OC003") == 0);
    CHECK(json_count_rule(buf, "OC005") == 0);
    CHECK(json_count_rule(buf, "OC007") == 0);
    CHECK(json_count_rule(buf, "OC009") == 0);
    CHECK(json_count_rule(buf, "OC010") == 0);
    /* no header_routing block: no C/C++ pack is involved on this corpus */
    CHECK(strstr(buf, "header_routing") == NULL);
}

/* --- corpus A graph: the comp + edge pins -------------------------------- */

static void corpus_scala_graph(void) {
    char buf[131072];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/scala", "graph", buf, sizeof(buf));
    /* the comp pins (hand-counted, the generator asserts the same
     * numbers textually) */
    CHECK(strstr(buf, "\"name\":\"rollup\"") != NULL);
    CHECK(strstr(buf, "\"complexity\":14") != NULL);  /* rollup */
    CHECK(strstr(buf, "\"complexity\":38") != NULL);  /* sum26 */
    CHECK(strstr(buf, "\"name\":\"shout\"") != NULL); /* the extension method */
    CHECK(strstr(buf, "\"name\":\"longRunner\"") != NULL);
    CHECK(strstr(buf, "\"def_end\":") != NULL); /* longRunner carries its span */
    /* the import edges: main_app -> widget (single) + tools (wildcard) */
    size_t imports = 0;
    const char *p = buf;
    while ((p = strstr(p, "\"kind\":\"imports\"")) != NULL) {
        imports++;
        p += 16;
    }
    CHECK(imports == 2);
}

/* --- corpus B: the resolver corpus, the contracted counts ---------------- */

static void corpus_scala_b(void) {
    char buf[65536];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/scala_resolver", "check", buf, sizeof(buf));
    CHECK(strstr(buf, "\"files_analyzed\":10") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(json_count_rule(buf, "OC003") == 1); /* the cyc pair */
    CHECK(json_count_rule(buf, "OC006") == 4);
    /* OC005 empty BY CONSTRUCTION: every export is claimed by the
     * resolver (the honest outcome, the Kotlin corpus B shape) */
    CHECK(json_count_rule(buf, "OC005") == 0);
}

/* --- coverage composition (the Cobertura reader, the OC007 contract) ----- */

static void corpus_scala_coverage(void) {
    char buf[65536];
    char extra[512];
    snprintf(extra, sizeof(extra),
             "check --coverage %s/tests/fixtures_sprint24/cobertura_scala.xml", OC_REPO_ROOT);
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/scala", extra, buf, sizeof(buf));
    /* the composition: 17 functions, 2 measured, ONE OC007 on sum26 --
     * CRAP == comp at 100 percent coverage */
    CHECK(strstr(buf, "\"functions_total\":17") != NULL);
    CHECK(json_count_rule(buf, "OC007") == 1);
    CHECK(json_count_rule(buf, "OC004") == 1);
}

/* --- the honest empty supply tier ---------------------------------------- */

static void corpus_scala_supply(void) {
    char buf[32768];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/scala", "check", buf, sizeof(buf));
    /* no manifest tier exists for Scala: the supply block is ABSENT
     * from the report meta entirely (the SPRINT-20/21/22 honest
     * zero-state: the meta block does not exist with zero manifests) */
    CHECK(strstr(buf, "\"supply\"") == NULL);
    CHECK(strstr(buf, "manifests_scanned") == NULL);
}

/* --- determinism: two runs byte-identical -------------------------------- */

static void corpus_scala_determinism(void) {
    char buf1[65536], buf2[65536];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/scala", "check", buf1, sizeof(buf1));
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/scala", "check", buf2, sizeof(buf2));
    CHECK(strcmp(buf1, buf2) == 0);
}

int main(void) {
    unit_scala_symbols();
    unit_scala_params();
    unit_scala_decisions();
    unit_scala_imports();
    unit_scala_surface();
    unit_scala_main_fact();
    unit_scala_resolver();
    unit_scala_pack_shape();
    corpus_scala_a();
    corpus_scala_graph();
    corpus_scala_b();
    corpus_scala_coverage();
    corpus_scala_supply();
    corpus_scala_determinism();
    TEST_SUMMARY();
    return oc__fails == 0 ? 0 : 1;
}
