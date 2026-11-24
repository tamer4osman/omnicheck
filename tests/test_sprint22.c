/* Sprint 22 (the Kotlin language adoption, wave-3 opens): the contract
 * of docs/LANGUAGES.md (Kotlin section), pinned unit by unit.
 *
 * Symbols: function_declaration WITH a body (free, method, extension,
 * generic/suspend/inline); body-less declarations are NOT symbols;
 * lambdas and anonymous functions are anonymous; property accessors
 * with a real body ARE symbols (Prop.get / Prop.set, the C# precedent).
 * Complexity: if_expression (else-if nested), when_entry (every arm
 * counts including else -- the Rust match_arm precedent), for, while,
 * do-while, catch_block, elvis_expression, conjunction_expression,
 * disjunction_expression. Imports: the declared-fact resolver
 * v1-kotlin (package clauses + top-level declaration names recorded as
 * hints; wildcard fan-out; single requires the declared symbol; the
 * member shape is ambiguous and keeps a NULL bind; without hints
 * everything stays unresolved). Surface: named top-level declarations,
 * private excluded, members excluded, enum entries excluded. Main fact:
 * a source_file-anchored function DEFINITION named main.
 */
#include "oc/common.h"
#include "oc/coverage.h"
#include "oc/graph.h"
#include "oc/imports.h"
#include "oc/parser.h"
#include "oc/pack.h"
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
    in.grammar = "kotlin";
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

/* --- unit: symbols, spans, the extension/shape contracts --------------- */

static void unit_kt_symbols(void) {
    static const char src[] =
        "/* free function: symbol, span = the whole declaration */\n"
        "fun compute(n: Int): Int {\n"
        "    return n\n"
        "}\n"
        "\n"
        "/* method: the name is the declaration's direct simple_identifier */\n"
        "class Widget(val id: Int) {\n"
        "    fun scale(v: Int): Int = v * id\n"
        "}\n"
        "\n"
        "/* extension: the receiver lives inside the receiver type, so the\n"
        " * declaration's ONLY direct simple_identifier is the name */\n"
        "fun String.shout(): String = uppercase()\n"
        "\n"
        "/* generic + suspend + inline: modifiers and type parameters are\n"
        " * separate children; the wrapper adds no extent */\n"
        "suspend fun <T> pick(a: T, b: T): T = a\n"
        "inline fun hook(block: () -> Unit) {\n"
        "    block()\n"
        "}\n"
        "\n"
        "/* object member: a symbol like any method */\n"
        "object Registry {\n"
        "    fun lookup(key: String): Int {\n"
        "        return key.length\n"
        "    }\n"
        "}\n"
        "\n"
        "/* body-less declarations: NOT symbols (the C prototype\n"
        " * discipline -- Kotlin has no prototypes, every real definition\n"
        " * carries a body) */\n"
        "abstract class Base {\n"
        "    abstract fun run(): Int\n"
        "}\n"
        "interface Repo {\n"
        "    fun fetch(): Int\n"
        "    fun default(): Int {\n"
        "        return 1\n"
        "    }\n"
        "}\n"
        "\n"
        "/* lambda + anonymous function: anonymous constructs, NOT symbols */\n"
        "fun lambdaUser(v: Int): Int {\n"
        "    val fn = { x: Int -> x * 2 }\n"
        "    val g = fun(x: Int): Int {\n"
        "        return x + 1\n"
        "    }\n"
        "    return fn(v) + g(v)\n"
        "}\n"
        "\n"
        "/* properties are NOT symbols (surface only) */\n"
        "val greeting = \"hi\"\n"
        "var counter = 0\n";

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
        parse_one(p, &g, "unit/widget.kt", src, &out);

        CHECK(count_symbols_named(&g, "compute") == 1);
        CHECK(count_symbols_named(&g, "scale") == 1);
        CHECK(count_symbols_named(&g, "shout") == 1); /* the extension name */
        CHECK(count_symbols_named(&g, "pick") == 1);
        CHECK(count_symbols_named(&g, "hook") == 1);
        CHECK(count_symbols_named(&g, "lookup") == 1);
        CHECK(count_symbols_named(&g, "default") == 1); /* the interface DEFAULT method */
        CHECK(count_symbols_named(&g, "lambdaUser") == 1);
        /* body-less declarations, lambdas, anonymous functions and
         * properties are NOT symbols */
        CHECK(count_symbols_named(&g, "run") == 0);
        CHECK(count_symbols_named(&g, "fetch") == 0);
        CHECK(count_symbols_named(&g, "fn") == 0);
        CHECK(count_symbols_named(&g, "g") == 0);
        CHECK(count_symbols_named(&g, "x") == 0);
        CHECK(count_symbols_named(&g, "greeting") == 0);
        CHECK(count_symbols_named(&g, "counter") == 0);
        CHECK(count_symbols_named(&g, "id") == 0);
        CHECK(count_symbols_named(&g, "key") == 0);

        /* span contract: the whole declaration extent */
        const oc_node *n = symbol_named(&g, "compute");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->decl_line == 2);
            CHECK(n->def_start_line == 2);
            CHECK(n->def_end_line == 4);
            CHECK(n->complexity == 1);
        }
        n = symbol_named(&g, "pick");
        CHECK(n != NULL);
        if (n) {
            /* the fexpr is the function_declaration (the suspend/type
             * wrappers are separate children, no extent) */
            CHECK(n->def_end_line >= n->def_start_line);
            CHECK(n->complexity == 1);
        }
        n = symbol_named(&g, "lambdaUser");
        CHECK(n != NULL);
        if (n) {
            /* the lambda + anonymous-function decisions count INWARD:
             * comp(lambdaUser) = 1 (no decisions of its own) */
            CHECK(n->complexity == 1);
        }
        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: property accessors (the C# precedent) ------------------------ */

static void unit_kt_accessors(void) {
    static const char src[] =
        "class Holder {\n"
        "    val cached: Int\n"
        "        get() {\n"
        "            return 1\n"
        "        }\n"
        "    var tracked: Int = 0\n"
        "        set(value) {\n"
        "            field = if (value > 0) value else 0\n"
        "        }\n"
        "    val plain: Int = 3\n"
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
        parse_one(p, &g, "unit/acc.kt", src, &out);

        /* the accessors with a body ARE symbols (the C# accounting-hole
         * precedent), named Prop.get / Prop.set; the initializer-only
         * property is not an accessor */
        CHECK(count_symbols_named(&g, "cached.get") == 1);
        CHECK(count_symbols_named(&g, "tracked.set") == 1);
        CHECK(count_symbols_named(&g, "plain.get") == 0);
        CHECK(count_symbols_named(&g, "cached") == 0);
        const oc_node *n = symbol_named(&g, "cached.get");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->complexity == 1);
            CHECK(n->def_start_line == 3);
            CHECK(n->def_end_line == 5);
        }
        n = symbol_named(&g, "tracked.set");
        CHECK(n != NULL);
        if (n) {
            /* the if_expression inside the setter counts toward the
             * accessor (the accounting-hole closure) */
            CHECK(n->complexity == 2);
        }
        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: parameters (the B3 signature shapes) ------------------------- */

static void unit_kt_params(void) {
    static const char src[] =
        "fun plain(a: Int, b: String) {}\n"
        "fun defaults(a: Int, b: Int = 2) {}\n"
        "fun typed(items: List<String>, cb: (Int) -> Unit) {}\n"
        "fun vararg(vararg items: String, tail: Int = 0) {}\n"
        "fun abstractish(): Int\n";

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
        parse_one(p, &g, "unit/params.kt", src, &out);

        const oc_node *n = symbol_named(&g, "plain");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "a"));
                CHECK(STR_EQ(n->params[0].type, "Int"));
                CHECK(n->params[0].has_default == 0);
                CHECK(STR_EQ(n->params[1].name, "b"));
                CHECK(STR_EQ(n->params[1].type, "String"));
            }
        }
        n = symbol_named(&g, "defaults");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                CHECK(n->params[0].has_default == 0);
                CHECK(n->params[1].has_default == 1); /* the B3 third slot */
            }
        }
        n = symbol_named(&g, "typed");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].type, "List<String>"));
                CHECK(STR_EQ(n->params[1].type, "(Int) -> Unit"));
            }
        }
        n = symbol_named(&g, "vararg");
        CHECK(n != NULL);
        if (n) {
            /* the vararg parameter collapses to the `...` marker (the
             * C++ pack convention); the defaulted tail keeps its flag */
            CHECK(n->n_params == 2);
            if (n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "..."));
                CHECK(n->params[1].has_default == 1);
            }
        }
        /* abstractish: body-less, NOT a symbol at all */
        CHECK(count_symbols_named(&g, "abstractish") == 0);
        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: the decision table (hand-counted complexity) ----------------- */

static void unit_kt_decisions(void) {
    /* comp(rollup) = 1 + 12: if + nested else-if (2 if_expression) + two
     * when arms (every arm counts incl. else -- the Rust match_arm
     * precedent) + for + for-in + while + do-while + one catch + elvis +
     * && + || = 12. The `in`-range when CONDITION is not a decision of
     * its own (only the ENTRY counts). */
    static const char rollup[] =
        "fun rollup(n: Int, items: List<Int>): Int {\n"
        "    if (n > 0) {\n"
        "        return 1\n"
        "    } else if (n < 0) {\n"
        "        return -1\n"
        "    }\n"
        "    when (n) {\n"
        "        in 3..9 -> return 30\n"
        "        else -> {}\n"
        "    }\n"
        "    for (v in items) {\n"
        "        n += v\n"
        "    }\n"
        "    var i = 0\n"
        "    for (k in 0 until 2) {\n"
        "        i++\n"
        "    }\n"
        "    while (n > 100) {\n"
        "        n--\n"
        "    }\n"
        "    do {\n"
        "        n++\n"
        "    } while (n < -100)\n"
        "    var t = 0\n"
        "    try {\n"
        "        t = t + n\n"
        "    } catch (ex: IllegalStateException) {\n"
        "        t = -1\n"
        "    }\n"
        "    val e = n ?: -1\n"
        "    val ok = n > 0 && e != 0\n"
        "    return t + i + e + if (ok) 1 else 0 || n\n"
        "}\n";
    /* comp(small) = 1 + 2: the two when arms */
    static const char small[] =
        "fun pick2(n: Int): Int {\n"
        "    when (n) {\n"
        "        1 -> return 10\n"
        "        else -> return 0\n"
        "    }\n"
        "    return 1\n"
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
        parse_one(p, &g, "unit/decisions.kt", rollup, &out);
        parse_one(p, &g, "unit/small.kt", small, &out);

        const oc_node *n = symbol_named(&g, "rollup");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->complexity == 14); /* 1 + 13 decisions (incl. the if-ok) */
        }
        n = symbol_named(&g, "pick2");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->complexity == 3); /* 1 + the two when arms */
        }
        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: the import-channel shapes ------------------------------------ */

static void unit_kt_imports(void) {
    static const char src[] =
        "package com.example.app\n"
        "\n"
        "import com.example.widgets.Widget\n"
        "import com.example.tools.*\n"
        "import com.example.kit.Helper as H2\n"
        "import com.example.kit.Helper.tag\n"
        "import java.util.ArrayList\n"
        "\n"
        "fun user(): Int {\n"
        "    return 1\n"
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
        out.imports = (oc_import_vec *)malloc(sizeof(oc_import_vec));
        out.binds = (oc_bind_vec *)malloc(sizeof(oc_bind_vec));
        CHECK(out.imports != NULL && out.binds != NULL);
        if (out.imports && out.binds) {
            oc_imports_init(out.imports);
            oc_surface_binds_init(out.binds);
            parse_one(p, &g, "unit/imports.kt", src, &out);

            /* the records: the path AS WRITTEN; the wildcard carries the
             * Java asterisk marker; the alias keeps the ORIGINAL path */
            CHECK(imports_count_spec(out.imports, "com.example.widgets.Widget") == 1);
            CHECK(imports_count_spec(out.imports, "com.example.tools.*") == 1);
            CHECK(imports_count_spec(out.imports, "com.example.kit.Helper") == 1);
            CHECK(imports_count_spec(out.imports, "com.example.kit.Helper.tag") == 1);
            CHECK(imports_count_spec(out.imports, "java.util.ArrayList") == 1);
            CHECK(out.imports->n == 5);
            for (size_t i = 0; i < out.imports->n; i++)
                CHECK(out.imports->items[i].family == OC_LANG_KOTLIN);

            /* the binds: the ALIAS form binds the ORIGINAL last segment
             * (Kotlin cannot alias a member -- only a declaration); a
             * plain one/two-segment import binds the last segment (the
             * unambiguous single-type record, the Java discipline); the
             * three-plus-segment plain form keeps the NULL bind (the
             * ambiguous member shape, whole-file claim); the wildcard
             * binds NULL (fail-open) */
            CHECK(bind_name_for(out.binds, "com.example.widgets.Widget") == NULL);
            CHECK(bind_name_for(out.binds, "com.example.tools.*") == NULL);
            CHECK(STR_EQ(bind_name_for(out.binds, "com.example.kit.Helper"), "Helper"));
            CHECK(bind_name_for(out.binds, "com.example.kit.Helper.tag") == NULL);
            CHECK(bind_name_for(out.binds, "java.util.ArrayList") == NULL);
            CHECK(out.binds->n == 5);

            oc_imports_destroy(out.imports);
            oc_surface_binds_destroy(out.binds);
            free(out.imports);
            free(out.binds);
        }
        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: module surface (top-level + the private filter) -------------- */

static void unit_kt_surface(void) {
    static const char src[] =
        "package app.shapes\n"
        "\n"
        "fun topLevel(): Int {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "private fun hidden(): Int {\n"
        "    return 2\n"
        "}\n"
        "\n"
        "internal fun moduleVisible(): Int {\n"
        "    return 3\n"
        "}\n"
        "\n"
        "class Holder {\n"
        "    fun member(): Int {\n"
        "        return 4\n"
        "    }\n"
        "    class Nested {\n"
        "        fun nestedMember(): Int {\n"
        "            return 5\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
        "object Singleton {\n"
        "    fun objectMember(): Int {\n"
        "        return 6\n"
        "    }\n"
        "}\n"
        "\n"
        "enum class Color {\n"
        "    RED,\n"
        "    GREEN\n"
        "}\n"
        "\n"
        "typealias Handler = (Int) -> Unit\n"
        "\n"
        "val prop = 7\n";

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
        out.exports = (oc_export_vec *)malloc(sizeof(oc_export_vec));
        out.hints = (oc_hint_vec *)malloc(sizeof(oc_hint_vec));
        CHECK(out.exports != NULL && out.hints != NULL);
        if (out.exports && out.hints) {
            oc_surface_exports_init(out.exports);
            oc_surface_hints_init(out.hints);
            parse_one(p, &g, "unit/surface.kt", src, &out);

            /* top-level named declarations are surface; private is
             * file-private (the C static rule's Kotlin spelling);
             * members and enum entries are not (the type-nesting
             * precedent); properties and type aliases surface */
            CHECK(exports_count_named(out.exports, "topLevel") == 1);
            CHECK(exports_count_named(out.exports, "moduleVisible") == 1);
            CHECK(exports_count_named(out.exports, "Holder") == 1);
            CHECK(exports_count_named(out.exports, "Singleton") == 1);
            CHECK(exports_count_named(out.exports, "Color") == 1);
            CHECK(exports_count_named(out.exports, "Handler") == 1);
            CHECK(exports_count_named(out.exports, "prop") == 1);
            CHECK(exports_count_named(out.exports, "hidden") == 0); /* private */
            CHECK(exports_count_named(out.exports, "member") == 0);
            CHECK(exports_count_named(out.exports, "nestedMember") == 0);
            CHECK(exports_count_named(out.exports, "objectMember") == 0);
            CHECK(exports_count_named(out.exports, "RED") == 0);
            CHECK(exports_count_named(out.exports, "GREEN") == 0);
            CHECK(out.exports->n == 7);

            /* the top-level-symbol hint channel (the resolver's
             * declared facts) mirrors the exports exactly */
            CHECK(out.hints->n == 1);
            if (out.hints->n == 1) {
                CHECK(STR_EQ(out.hints->items[0].path, "unit/surface.kt"));
                CHECK(out.hints->items[0].n_symbols == 7);
                CHECK(STR_EQ(out.hints->items[0].package_name, "app.shapes"));
            }
            oc_surface_exports_destroy(out.exports);
            oc_surface_hints_destroy(out.hints);
            free(out.exports);
            free(out.hints);
        }
        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: the main fact + the package hint ------------------------------ */

static void unit_kt_main_fact(void) {
    static const char src[] =
        "package app.entry\n"
        "\n"
        "fun main() {\n"
        "}\n"
        "fun main(args: Array<String>) {\n"
        "}\n"
        "class App {\n"
        "    fun main() {\n"
        "    }\n"
        "}\n"
        "fun main2(x: Int) {\n"
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
        out.hints = (oc_hint_vec *)malloc(sizeof(oc_hint_vec));
        CHECK(out.hints != NULL);
        if (out.hints) {
            oc_surface_hints_init(out.hints);
            parse_one(p, &g, "unit/main.kt", src, &out);

            /* the parse fact fires ONCE for the file (one hint per
             * file); both entry spellings match; the member `fun main`
             * inside class App claims nothing (the source_file anchor) */
            CHECK(out.hints->n == 1);
            if (out.hints->n == 1) {
                CHECK(out.hints->items[0].has_main == 1);
                CHECK(STR_EQ(out.hints->items[0].package_name, "app.entry"));
            }
            oc_surface_hints_destroy(out.hints);
            free(out.hints);
        }
        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: resolver v1-kotlin, the declared facts end to end ------------ */

typedef struct {
    const char *path;
    const char *source;
} kt_res_file;

static void unit_kt_resolver(void) {
    /* corpus B, inline: the declared-fact contract end to end -- the
     * two-file package union, the wildcard fan-out, the alias, the
     * member shape, the cycle, the unresolved decoys, the default
     * package. 11 files, 9 contracted edges. */
    static const kt_res_file FILES[] = {
        {"core.kt",
         "package app.core\n\n"
         "class Engine {\n    fun start(): Int {\n        return 1\n    }\n}\n\n"
         "fun alpha(): Int {\n    return 2\n}\n"},
        {"extra.kt",
         "package app.core\n\n"
         "fun beta(): Int {\n    return 3\n}\n"},
        {"lib.kt",
         "package app.lib\n\nimport app.core.Engine\n\n"
         "fun gamma(): Int {\n    return Engine().start()\n}\n"},
        {"wide.kt",
         "package app.lib\n\nimport app.core.*\n\n"
         "val useAll = alpha() + beta()\n"},
        {"alias.kt",
         "package app.util\n\nimport app.core.Engine as Motor\nimport app.lib.gamma\n\n"
         "fun eps(): Int {\n    return Motor().start() + gamma()\n}\n"},
        {"member.kt",
         "package app.util\n\nimport app.core.Engine.start\n\n"
         "fun memb(): Int {\n    return 1\n}\n"},
        {"cyc_one.kt",
         "package app.cyc\n\nimport app.cyc.Two\n\n"
         "class One {\n    val pair = Two()\n}\n"},
        {"cyc_two.kt",
         "package app.cyc\n\nimport app.cyc.One\n\n"
         "class Two {\n    val mate = One()\n}\n"},
        {"decoy.kt",
         "package app.decoy\n\nimport java.util.ArrayList\nimport app.missing.Ghost\n\n"
         "fun decoy(): Int {\n    return 0\n}\n"},
        {"nopkg.kt",
         "fun standalone(): Int {\n    return 9\n}\n"},
        {"user.kt",
         "package app.user\n\nimport standalone\n\n"
         "fun userFn(): Int {\n    return standalone()\n}\n"},
    };
    const size_t N_FILES = sizeof(FILES) / sizeof(FILES[0]);
    CHECK(N_FILES == 11);

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
            in.grammar = "kotlin";
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

    /* the edge pass with hints: exactly 9 edges, lexicographic fan-out,
     * set semantics */
    long n_edges = oc_imports_resolve_ex_hints(&g, &imports, &a, NULL, 0, &hints);
    CHECK(n_edges == 9);

    /* spot-pins: the wildcard fans out over BOTH package files; the
     * cycle closes; the decoys resolve nothing */
    oc_node_id core = oc_graph_find_file(&g, "core.kt");
    oc_node_id extra = oc_graph_find_file(&g, "extra.kt");
    oc_node_id wide = oc_graph_find_file(&g, "wide.kt");
    oc_node_id lib = oc_graph_find_file(&g, "lib.kt");
    oc_node_id alias = oc_graph_find_file(&g, "alias.kt");
    oc_node_id member = oc_graph_find_file(&g, "member.kt");
    oc_node_id cyc_one = oc_graph_find_file(&g, "cyc_one.kt");
    oc_node_id cyc_two = oc_graph_find_file(&g, "cyc_two.kt");
    oc_node_id decoy = oc_graph_find_file(&g, "decoy.kt");
    oc_node_id nopkg = oc_graph_find_file(&g, "nopkg.kt");
    oc_node_id user = oc_graph_find_file(&g, "user.kt");
    CHECK(core != OC_NODE_INVALID && extra != OC_NODE_INVALID && wide != OC_NODE_INVALID);
    CHECK(decoy != OC_NODE_INVALID);

    size_t wide_to_core = 0, wide_to_extra = 0, decoy_edges = 0, member_to_core = 0;
    size_t alias_to_core = 0, alias_to_lib = 0, user_to_nopkg = 0, cycle_edges = 0;
    for (size_t i = 0; i < g.n_edges; i++) {
        if (g.edges[i].kind != OC_EDGE_IMPORTS)
            continue;
        if (g.edges[i].from == wide && g.edges[i].to == core)
            wide_to_core++;
        if (g.edges[i].from == wide && g.edges[i].to == extra)
            wide_to_extra++;
        if (g.edges[i].from == decoy)
            decoy_edges++;
        if (g.edges[i].from == member && g.edges[i].to == core)
            member_to_core++;
        if (g.edges[i].from == alias && g.edges[i].to == core)
            alias_to_core++;
        if (g.edges[i].from == alias && g.edges[i].to == lib)
            alias_to_lib++;
        if (g.edges[i].from == user && g.edges[i].to == nopkg)
            user_to_nopkg++;
        if ((g.edges[i].from == cyc_one && g.edges[i].to == cyc_two) ||
            (g.edges[i].from == cyc_two && g.edges[i].to == cyc_one))
            cycle_edges++;
    }
    CHECK(wide_to_core == 1);
    CHECK(wide_to_extra == 1);
    CHECK(decoy_edges == 0); /* java.util + app.missing stay unresolved */
    CHECK(member_to_core == 1); /* the member shape's object reading */
    CHECK(alias_to_core == 1);
    CHECK(alias_to_lib == 1);
    CHECK(user_to_nopkg == 1); /* the default-package claim */
    CHECK(cycle_edges == 2);   /* OC003 == 1 on this corpus */
    CHECK(lib != OC_NODE_INVALID && alias != OC_NODE_INVALID && member != OC_NODE_INVALID);

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

/* --- unit: the declared-fact set semantics ------------------------------- */

static void unit_kt_resolver_set(void) {
    /* two files declaring the SAME name in one package: the single-type
     * import fans out over both (the honest over-claim direction); a
     * duplicate record collapses (the imports relation is a set) */
    static const char a_src[] =
        "package dup\n\nfun twin(): Int {\n    return 1\n}\n";
    static const char b_src[] =
        "package dup\n\nfun twin(): Int {\n    return 2\n}\n";
    static const char user_src[] =
        "package app\n\nimport dup.twin\nimport dup.twin\n\n"
        "fun u(): Int {\n    return twin()\n}\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_import_vec imports;
    oc_imports_init(&imports);
    oc_hint_vec hints;
    oc_surface_hints_init(&hints);
    oc_export_vec exports;
    oc_surface_exports_init(&exports);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        const char *paths[3] = {"a.kt", "b.kt", "user.kt"};
        const char *srcs[3] = {a_src, b_src, user_src};
        for (size_t i = 0; i < 3; i++) {
            oc_parser_input in;
            memset(&in, 0, sizeof(in));
            in.path = paths[i];
            in.source = srcs[i];
            in.len = strlen(srcs[i]);
            in.grammar = "kotlin";
            oc_parse_out out;
            memset(&out, 0, sizeof(out));
            out.imports = &imports;
            out.exports = &exports;
            out.hints = &hints;
            CHECK(oc_parser_parse_file(p, &in, &g, &out, &st) == OC_OK);
        }
        oc_parser_free(p);
    }
    oc_surface_exports_destroy(&exports);
    /* the duplicate record collapses at the parse (the set semantics:
     * the same specifier twice pushes twice, the EDGE dedupes) */
    CHECK(imports_count_spec(&imports, "dup.twin") == 2);
    long n = oc_imports_resolve_ex_hints(&g, &imports, &a, NULL, 0, &hints);
    CHECK(n == 2); /* user -> a.kt AND user -> b.kt (the declared union) */
    oc_graph_destroy(&g);
    oc_imports_destroy(&imports);
    oc_surface_hints_destroy(&hints);
    oc_arena_destroy(&a);
}

/* --- corpus A: the structural corpus, the contracted counts -------------- */

static void corpus_kotlin_a(void) {
    char buf[65536];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/kotlin", "check", buf, sizeof(buf));
    CHECK(strstr(buf, "\"files_analyzed\":14") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(json_count_rule(buf, "OC001") == 1); /* longfn.kt: 63 lines > 60 */
    CHECK(json_count_rule(buf, "OC002") == 2); /* the two TODO markers */
    CHECK(json_count_rule(buf, "OC004") == 1); /* sum26 comp 35 > 15 */
    CHECK(json_count_rule(buf, "OC006") == 10);
    CHECK(json_count_rule(buf, "OC008") == 2); /* the dup pair */
    CHECK(json_count_rule(buf, "OC011") == 1); /* the credential assignment */
    CHECK(json_count_rule(buf, "OC003") == 0);
    CHECK(json_count_rule(buf, "OC005") == 0);
    CHECK(json_count_rule(buf, "OC007") == 0);
    CHECK(json_count_rule(buf, "OC009") == 0);
    CHECK(json_count_rule(buf, "OC010") == 0);
    /* no header_routing block: no C/C++ pack is involved on this corpus */
    CHECK(strstr(buf, "header_routing") == NULL);
}

/* --- corpus A graph: the comp + accessor + edge pins --------------------- */

static void corpus_kotlin_graph(void) {
    char buf[131072];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/kotlin", "graph", buf, sizeof(buf));
    /* the comp pins (hand-counted, the generator asserts the same
     * numbers textually) */
    CHECK(strstr(buf, "\"name\":\"rollup\"") != NULL);
    CHECK(strstr(buf, "\"complexity\":14") != NULL); /* rollup */
    CHECK(strstr(buf, "\"complexity\":35") != NULL); /* sum26 */
    /* the accessor symbols (the C# precedent, the sibling-owner shape) */
    CHECK(strstr(buf, "\"name\":\"cached.get\"") != NULL);
    CHECK(strstr(buf, "\"name\":\"tracked.set\"") != NULL);
    /* the three import edges (single, wildcard, alias) */
    size_t imports = 0;
    const char *p = buf;
    while ((p = strstr(p, "\"kind\":\"imports\"")) != NULL) {
        imports++;
        p += 16;
    }
    CHECK(imports == 3);
}

/* --- corpus B: the resolver corpus, the contracted counts ---------------- */

static void corpus_kotlin_b(void) {
    char buf[65536];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/kotlin_resolver", "check", buf, sizeof(buf));
    CHECK(strstr(buf, "\"files_analyzed\":11") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(json_count_rule(buf, "OC003") == 1); /* the cyc pair */
    CHECK(json_count_rule(buf, "OC006") == 5);
    /* OC005 empty BY CONSTRUCTION: every export is claimed by the
     * resolver (the honest outcome, the Go whole-package analogue) */
    CHECK(json_count_rule(buf, "OC005") == 0);
}

/* --- coverage composition (the Cobertura reader, the OC007 contract) ----- */

static void corpus_kotlin_coverage(void) {
    char buf[65536];
    char extra[512];
    snprintf(extra, sizeof(extra),
             "check --coverage %s/tests/fixtures_sprint22/cobertura_kotlin.xml", OC_REPO_ROOT);
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/kotlin", extra, buf, sizeof(buf));
    /* the composition: 18 functions, 2 measured, ONE OC007 on sum26 --
     * CRAP == comp at 100 percent coverage */
    CHECK(strstr(buf, "\"functions_total\":18") != NULL);
    CHECK(strstr(buf, "\"functions_measured\":2") != NULL);
    CHECK(json_count_rule(buf, "OC007") == 1);
    CHECK(strstr(buf, "CRAP=35") != NULL);
    CHECK(json_count_rule(buf, "OC004") == 1);
}

/* --- the honest empty supply tier ---------------------------------------- */

static void corpus_kotlin_supply(void) {
    char buf[32768];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/kotlin", "check", buf, sizeof(buf));
    /* no manifest tier exists for Kotlin: the supply block is ABSENT
     * from the report meta entirely (the SPRINT-20/21 honest
     * zero-state: the meta block does not exist with zero manifests) */
    CHECK(strstr(buf, "\"supply\"") == NULL);
    CHECK(strstr(buf, "manifests_scanned") == NULL);
}

/* --- determinism: two runs byte-identical -------------------------------- */

static void corpus_kotlin_determinism(void) {
    char buf1[65536], buf2[65536];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/kotlin", "check", buf1, sizeof(buf1));
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/kotlin", "check", buf2, sizeof(buf2));
    CHECK(strcmp(buf1, buf2) == 0);
}

/* --- the pack shape ------------------------------------------------------- */

static void unit_kt_pack_shape(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_pack_set ps;
    oc_packs_init(&ps, &a);
    oc_status st;
    oc_status_clear(&st);
    char packs_dir[512];
    snprintf(packs_dir, sizeof(packs_dir), "%s/packs", OC_REPO_ROOT);
    CHECK(oc_packs_load_dir(&ps, packs_dir, &st) == OC_OK);
    const oc_pack *kt = NULL;
    for (size_t i = 0; i < ps.n_packs; i++) {
        if (strcmp(ps.packs[i].id, "kotlin") == 0)
            kt = &ps.packs[i];
    }
    CHECK(kt != NULL);
    if (kt) {
        CHECK(STR_EQ(kt->display_name, "Kotlin"));
        CHECK(STR_EQ(kt->grammar, "kotlin"));
        CHECK(kt->n_extensions == 3);
        CHECK(STR_EQ(kt->extensions[0], ".kt"));
        CHECK(STR_EQ(kt->extensions[1], ".kts"));
        CHECK(STR_EQ(kt->extensions[2], ".ktm"));
        CHECK(kt->n_entry_points == 0); /* the main parse fact claims */
        CHECK(kt->n_passes == 11);
        CHECK(STR_EQ(kt->passes[0], "OC001"));
        CHECK(STR_EQ(kt->passes[10], "OC011"));
        /* the pack routes a .kt file */
        const oc_pack *m = oc_packs_match_path(&ps, "src/Main.kt");
        CHECK(m == kt);
        m = oc_packs_match_path(&ps, "scripts/build.gradle.kts");
        CHECK(m == kt);
    }
    oc_packs_destroy(&ps);
    oc_arena_destroy(&a);
}

int main(void) {
    unit_kt_symbols();
    unit_kt_accessors();
    unit_kt_params();
    unit_kt_decisions();
    unit_kt_imports();
    unit_kt_surface();
    unit_kt_main_fact();
    unit_kt_resolver();
    unit_kt_resolver_set();
    unit_kt_pack_shape();
    corpus_kotlin_a();
    corpus_kotlin_graph();
    corpus_kotlin_b();
    corpus_kotlin_coverage();
    corpus_kotlin_supply();
    corpus_kotlin_determinism();
    TEST_SUMMARY();
    return oc__fails == 0 ? 0 : 1;
}
