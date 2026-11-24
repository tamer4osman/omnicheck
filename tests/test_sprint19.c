/* Sprint 19 (the PHP language adoption): the contract of
 * docs/LANGUAGES.md (PHP section), pinned unit by unit.
 *
 * Symbols: function_definition + method_declaration with a real body
 * (the whole definition is the span); EMPTY methods (interface
 * signatures, abstract) are symbols with complexity exactly 1 through
 * the bodyless slot; anonymous_function/arrow_function/anonymous_class
 * never are (decisions count inward, inclusive). Complexity: the
 * decision table -- if_statement (elseif is its own else_if_clause) +
 * while/do/for/foreach + case_statement (default_statement is a
 * DISTINCT node and counts 0) + match arms (a comma condition list is
 * ONE arm; the match_default_expression arm IS an arm) + catch_clause +
 * conditional_expression (full and short ternary are the SAME node),
 * plus &&/||/and/or inside the shared binary_expression container; ?? is
 * a value operator, not a branch. Imports: the use-declaration channel
 * (plain / aliased / function / const / grouped forms; the alias is
 * local sugar) and the require/include channel with the exact-one-
 * static-string argument guard. Resolver v1-php: composer.json PSR-4/
 * PSR-0 from the ANALYSIS ROOT (longest prefix, backslash->slash,
 * root-anchored), include payloads relative to the requiring file's
 * directory (lexical .. pops), set-semantics edges. Surface: top-level
 * class/interface/trait/enum/function/const_element/enum_case names, NO
 * exportedness filter, the alias never narrows marking, the whole-file
 * claim for requires, NO shebang hint for PHP. Coverage: the Clover XML
 * adapter (line records, count > 0 covered, loud malformed input,
 * dispatched before Cobertura on the shared <coverage> root). Supply:
 * composer.json exact pins (the npm discipline -- ranges and platform
 * requirements record nothing), composer.lock resolved pins, the
 * composer.json licence field, OSV ecosystem packagist.
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
    in.grammar = "php";
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

/* --- unit: symbols, spans, complexity ---------------------------------- */

static void unit_php_symbols(void) {
    static const char src[] =
        "<?php\n"
        "namespace App;\n"
        "\n"
        "interface HasId\n"
        "{\n"
        "    // an interface signature: NO body field -- bodyless symbol, comp 1\n"
        "    public function id(): int;\n"
        "}\n"
        "\n"
        "abstract class Base\n"
        "{\n"
        "    // an abstract method: bodyless symbol, comp 1\n"
        "    abstract protected function tick(): void;\n"
        "\n"
        "    // a plain method: symbol, span = the whole declaration\n"
        "    public function plain(int $a, int $b = 1): int\n"
        "    {\n"
        "        return $a + $b;\n"
        "    }\n"
        "\n"
        "    // an anonymous function inside: NOT a symbol; decisions count inward\n"
        "    public function withClosure(array $xs): int\n"
        "    {\n"
        "        $f = function (int $x) use ($a) {\n"
        "            return $x > 0 ? $x : 0;\n"
        "        };\n"
        "        return $f(1);\n"
        "    }\n"
        "\n"
        "    // a static closure and an arrow fn: never symbols\n"
        "    public function withArrow(array $xs): array\n"
        "    {\n"
        "        return array_map(static fn(int $v) => $v * 2, $xs);\n"
        "    }\n"
        "}\n"
        "\n"
        "function top(): int\n"
        "{\n"
        "    return 1;\n"
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
        parse_one(p, &g, "unit/widget.php", src, &out);

        CHECK(count_symbols_named(&g, "id") == 1);   /* interface signature */
        CHECK(count_symbols_named(&g, "tick") == 1); /* abstract method */
        CHECK(count_symbols_named(&g, "plain") == 1);
        CHECK(count_symbols_named(&g, "withClosure") == 1);
        CHECK(count_symbols_named(&g, "withArrow") == 1);
        CHECK(count_symbols_named(&g, "top") == 1);
        /* anonymous constructs are never symbols */
        CHECK(count_symbols_named(&g, "f") == 0);
        CHECK(count_symbols_named(&g, "v") == 0);
        CHECK(count_symbols_named(&g, "class") == 0);

        /* span contract: the whole declaration extent */
        const oc_node *n = symbol_named(&g, "plain");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->decl_line == 16);
            CHECK(n->def_start_line == 16);
            CHECK(n->def_end_line == 19);
            CHECK(n->complexity == 1);
        }
        /* bodyless symbols: complexity exactly 1, the declaration IS the span */
        n = symbol_named(&g, "id");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->complexity == 1);
            CHECK(n->def_start_line == n->def_end_line);
        }
        n = symbol_named(&g, "tick");
        CHECK(n != NULL);
        if (n)
            CHECK(n->complexity == 1);
        /* the closure's ternary counts INWARD (inclusive) */
        n = symbol_named(&g, "withClosure");
        CHECK(n != NULL);
        if (n)
            CHECK(n->complexity == 2);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the decision table (hand-counts) ---------------------------- */

static void unit_php_decision_table(void) {
    struct {
        const char *name;
        const char *body;
        size_t comp;
    } cases[] = {
        /* if + elseif x2 (each branch by itself; else never) = 3 */
        {"branch_chain",
         "function branch_chain($x) {\n"
         "    if ($x > 0) { return 1;\n"
         "    } elseif ($x == 0) { return 2;\n"
         "    } elseif ($x < 0) { return 3;\n"
         "    } else { return 4; }\n"
         "}\n",
         4},
        /* while / do-while / for / foreach = 4 */
        {"loops",
         "function loops($xs) {\n"
         "    $i = 0;\n"
         "    while ($i < 3) { $i++; }\n"
         "    do { $i++; } while ($i < 9);\n"
         "    for ($j = 0; $j < 2; $j++) {}\n"
         "    foreach ($xs as $k => $v) {}\n"
         "    return $i;\n"
         "}\n",
         5},
        /* switch: one per case_statement; default_statement counts 0 = 3 */
        {"switch_arms",
         "function switch_arms($x) {\n"
         "    switch ($x) {\n"
         "        case 1: return 1;\n"
         "        case 2: return 2;\n"
         "        case 3: return 3;\n"
         "        default: return 0;\n"
         "    }\n"
         "}\n",
         4},
        /* match: one per arm, a comma condition list is ONE arm, the
         * match_default_expression arm IS an arm = 3 */
        {"match_arms",
         "function match_arms($x) {\n"
         "    return match(true) {\n"
         "        $x > 100 => 'big',\n"
         "        $x > 10, $x > 5 => 'mid',\n"
         "        default => 'small',\n"
         "    };\n"
         "}\n",
         4},
        /* try/catch: one per catch_clause; finally counts 0 = 2 */
        {"catches",
         "function catches($x) {\n"
         "    try { return intdiv($x, 2); }\n"
         "    catch (TypeError $e) { return 0; }\n"
         "    catch (Throwable) { return 1; }\n"
         "    finally { $x += 0; }\n"
         "}\n",
         3},
        /* the ternary: full and short forms are the SAME node; ?? is a
         * value operator and counts 0 = 2 */
        {"ternaries",
         "function ternaries($x, $y) {\n"
         "    $a = $x > 0 ? 1 : 2;\n"
         "    $b = $x ?: 3;\n"
         "    $c = $x ?? $y;\n"
         "    return $a + $b + strlen($c ?? '');\n"
         "}\n",
         3},
        /* operators: && || and or inside binary_expression = 4 */
        {"operators",
         "function operators($a, $b, $c, $d) {\n"
         "    $x = $a && $b;\n"
         "    $y = $c || $d;\n"
         "    $z = $a and $b;\n"
         "    $w = $c or $d;\n"
         "    return $x + strlen($y) + strlen($z) + strlen($w);\n"
         "}\n",
         5},
        /* throw is not a branch; goto/exit are not decisions */
        {"throw_not_branch",
         "function throw_not_branch($x) {\n"
         "    if ($x < 0) { throw new RuntimeException('neg'); }\n"
         "    return 1;\n"
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
            char src[1024];
            snprintf(src, sizeof(src), "<?php\n%s", cases[i].body);
            oc_parse_out out;
            memset(&out, 0, sizeof(out));
            parse_one(p, &g, "unit/case.php", src, &out);
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

/* --- unit: parameters and the compat (B3) signature -------------------- */

static void unit_php_params(void) {
    static const char src[] =
        "<?php\n"
        "class C\n"
        "{\n"
        "    public function __construct(private int $id, public string $name = 'x',\n"
        "        protected ?C $next = null) {}\n"
        "    public function m(int $a, bool &$ok, string|int $b = PHP_INT_MAX,\n"
        "        array ...$rest): ?self { return null; }\n"
        "    abstract protected function hook(): void;\n"
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
        parse_one(p, &g, "unit/params.php", src, &out);

        /* names WITHOUT the $ sigil; raw type text; default detection */
        const oc_node *n = symbol_named(&g, "__construct");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 3);
            if (n->n_params == 3) {
                CHECK(STR_EQ(n->params[0].name, "id"));
                CHECK(STR_EQ(n->params[0].type, "int"));
                CHECK(n->params[0].has_default == 0);
                CHECK(STR_EQ(n->params[1].name, "name"));
                CHECK(STR_EQ(n->params[1].type, "string"));
                CHECK(n->params[1].has_default == 1);
                CHECK(STR_EQ(n->params[2].name, "next"));
                CHECK(STR_EQ(n->params[2].type, "?C"));
                CHECK(n->params[2].has_default == 1);
            }
        }
        /* reference modifier, union type, variadic collapse */
        n = symbol_named(&g, "m");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->n_params == 4);
            if (n->n_params == 4) {
                CHECK(STR_EQ(n->params[0].name, "a"));
                CHECK(STR_EQ(n->params[0].type, "int"));
                CHECK(n->params[0].has_default == 0);
                CHECK(STR_EQ(n->params[1].name, "ok"));
                CHECK(STR_EQ(n->params[2].name, "b"));
                CHECK(STR_EQ(n->params[2].type, "string|int"));
                CHECK(n->params[2].has_default == 1);
                /* a variadic parameter collapses to the `...` marker */
                CHECK(STR_EQ(n->params[3].name, "..."));
                CHECK(STR_EQ(n->params[3].type, "array"));
            }
        }
        /* bodyless declarations carry caller-visible signatures too */
        n = symbol_named(&g, "hook");
        CHECK(n != NULL);
        if (n)
            CHECK(n->n_params == 0);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: import records (use + include channels) --------------------- */

static void unit_php_import_records(void) {
    static const char src[] = "<?php\n"
                              "use ArrayObject;\n"
                              "use App\\Store\\Contracts\\Mapper as MapperI;\n"
                              "use function App\\f1;\n"
                              "use const App\\LIMIT;\n"
                              "use App\\Store\\{Product, Order as O};\n"
                              "require 'vendor/autoload.php';\n"
                              "require_once 'init.php';\n"
                              "include 'partial.php';\n"
                              "include_once 'tail.php';\n"
                              "require_once __DIR__ . '/x.php';\n"     /* concatenation: NOTHING */
                              "include $dynamic;\n"                    /* variable: NOTHING */
                              "include 'partial_' . $name . '.php';\n" /* concat: NOTHING */
                              "function f() {}\n";

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
        parse_one(p, &g, "unit/imports.php", src, &out);

        /* the use channel: one record per clause, name as written */
        CHECK(imports_count_spec(&imports, "ArrayObject") == 1);
        CHECK(imports_count_spec(&imports, "App\\Store\\Contracts\\Mapper") == 1);
        CHECK(imports_count_spec(&imports, "App\\f1") == 1);
        CHECK(imports_count_spec(&imports, "App\\LIMIT") == 1);
        CHECK(imports_count_spec(&imports, "App\\Store\\Product") == 1);
        CHECK(imports_count_spec(&imports, "App\\Store\\Order") == 1);
        /* the include channel: one record per static argument, the
         * keyword tag travels in the specifier */
        CHECK(imports_count_spec(&imports, "require vendor/autoload.php") == 1);
        CHECK(imports_count_spec(&imports, "require_once init.php") == 1);
        CHECK(imports_count_spec(&imports, "include partial.php") == 1);
        CHECK(imports_count_spec(&imports, "include_once tail.php") == 1);
        /* the argument guard: dynamic shapes record NOTHING */
        CHECK(imports.n == 10);

        /* binds: the LAST segment of the qualified name (the alias never
         * narrows marking); NULL for the include channel (whole-file) */
        CHECK(STR_EQ(bind_name_for(&binds, "ArrayObject"), "ArrayObject"));
        CHECK(STR_EQ(bind_name_for(&binds, "App\\Store\\Contracts\\Mapper"), "Mapper"));
        CHECK(STR_EQ(bind_name_for(&binds, "App\\f1"), "f1"));
        CHECK(STR_EQ(bind_name_for(&binds, "App\\LIMIT"), "LIMIT"));
        CHECK(STR_EQ(bind_name_for(&binds, "App\\Store\\Product"), "Product"));
        CHECK(STR_EQ(bind_name_for(&binds, "App\\Store\\Order"), "Order"));
        CHECK(bind_name_for(&binds, "require vendor/autoload.php") == NULL);
        CHECK(bind_name_for(&binds, "include_once tail.php") == NULL);
        oc_imports_destroy(&imports);
        oc_surface_binds_destroy(&binds);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the mixed php+html template is inert ------------------------ */

static void unit_php_mixed_template(void) {
    static const char src[] = "<html><body>\n"
                              "<p><?= htmlspecialchars($title) ?></p>\n"
                              "<?php if ($ok) { echo '<b>yes</b>'; } ?>\n"
                              "<?php foreach ($items as $it) { echo '<li>'; } ?>\n"
                              "</body></html>\n";

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
        out.imports = NULL; /* the template records nothing: prose is inert */
        parse_one(p, &g, "unit/template.php", src, &out);
        /* no symbols, no decisions, no imports from template prose */
        size_t syms = 0;
        for (size_t i = 0; i < g.n_nodes; i++)
            if (g.nodes[i].kind == OC_NODE_FUNCTION)
                syms++;
        CHECK(syms == 0);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: surface (exports, hints) ------------------------------------ */

static void unit_php_surface(void) {
    static const char src[] =
        "<?php\n"
        "namespace App\\Deep;\n"
        "\n"
        "class Widget {}\n"
        "interface Spoke {}\n"
        "trait Wrench {}\n"
        "enum Suit: string\n"
        "{\n"
        "    case Hearts = 'H';\n"
        "    case Spades = 'S';\n"
        "}\n"
        "const LIMIT = 9;\n"
        "function top() {}\n"
        "class Inner\n"
        "{\n"
        "    private function hidden() {}\n" /* a method: never module surface */
        "}\n"
        "function nested()\n"
        "{\n"
        "    function inner_fn() {}\n" /* a nested function: not top-level */
        "    return inner_fn();\n"
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
        parse_one(p, &g, "unit/surface.php", src, &out);

        /* NO exportedness filter: every top-level name is surface, the
         * simple declaration name as written (no namespace prefixing) */
        CHECK(exports_count_named(&exports, "Widget") == 1);
        CHECK(exports_count_named(&exports, "Spoke") == 1);
        CHECK(exports_count_named(&exports, "Wrench") == 1);
        CHECK(exports_count_named(&exports, "Suit") == 1);
        CHECK(exports_count_named(&exports, "Hearts") == 1);
        CHECK(exports_count_named(&exports, "Spades") == 1);
        CHECK(exports_count_named(&exports, "LIMIT") == 1);
        CHECK(exports_count_named(&exports, "top") == 1);
        CHECK(exports_count_named(&exports, "Inner") == 1);
        /* members and nested functions never become candidates */
        CHECK(exports_count_named(&exports, "hidden") == 0);
        CHECK(exports_count_named(&exports, "inner_fn") == 0);
        CHECK(exports_count_named(&exports, "App") == 0);
        oc_surface_exports_destroy(&exports);
    }

    /* the shebang hint does not exist for PHP (the contract): a #! line
     * outside the PHP block is template text */
    static const char shebang[] = "#!/usr/bin/env php\n<?php\necho 1;\n";
    oc_graph g2;
    oc_graph_init(&g2, &a);
    oc_parser *p2 = oc_parser_new(&a, &st);
    if (p2) {
        oc_parse_out out2;
        memset(&out2, 0, sizeof(out2));
        oc_hint_vec hints;
        oc_surface_hints_init(&hints);
        out2.hints = &hints;
        parse_one(p2, &g2, "unit/shebang.php", shebang, &out2);
        CHECK(hints.n == 0); /* no shebang hint for PHP */
        oc_surface_hints_destroy(&hints);
        oc_parser_free(p2);
    }
    oc_graph_destroy(&g2);
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: resolver v1-php --------------------------------------------- */

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

static void unit_php_resolver(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    /* corpus B: composer.json at the analysis root carries psr-4
     * Acme\Shop\ -> src/ and psr-0 Acme\Legacy\ -> lib/. The nested
     * composer.json (src/nested/) must never contribute resolver
     * config: its psr-4 mapping points at fake/, which holds nothing. */
    char root[1024];
    snprintf(root, sizeof(root), "%s/tests/fixtures/php_resolver", OC_REPO_ROOT);
    char paths[11][1400];
    static const char *const REL[] = {
        "src/Shop.php",
        "src/Contracts/Mapper.php",
        "src/Product.php",
        "src/Order.php",
        "src/helpers/sum.php",
        "src/Store/Types.php",
        "src/loader.php",
        "src/store/orphan.php",
        "lib/Acme/Legacy/Legacy.php",
        "src/a.php",
        "src/b.php",
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

    /* the use channel: PSR-4 strips the prefix, the remainder is the
     * class path, backslash->slash, .php appended, anchored at the root */
    char from[1400];
    snprintf(from, sizeof(from), "%s/src/Shop.php", root);
    CHECK_STR_EQ(oc_resolve_php_use(cx, from, "Acme\\Shop\\Contracts\\Mapper", &a), paths[1]);
    CHECK_STR_EQ(oc_resolve_php_use(cx, from, "Acme\\Shop\\Store\\Types", &a), paths[5]);
    CHECK_STR_EQ(oc_resolve_php_use(cx, from, "Acme\\Shop\\Product", &a), paths[2]);
    CHECK_STR_EQ(oc_resolve_php_use(cx, from, "Acme\\Shop\\helpers\\sum", &a), paths[4]);
    /* a fully-qualified use (one leading separator) names the same class */
    CHECK_STR_EQ(oc_resolve_php_use(cx, from, "\\Acme\\Shop\\Product", &a), paths[2]);
    /* PSR-0 keeps the FULL name path under the mapping dir */
    char from2[1400];
    snprintf(from2, sizeof(from2), "%s/src/uses_legacy.php", root);
    char legacy_target[1400];
    snprintf(legacy_target, sizeof(legacy_target), "%s/lib/Acme/Legacy/Legacy.php", root);
    CHECK_STR_EQ(oc_resolve_php_use(cx, from2, "Acme\\Legacy\\Legacy", &a), legacy_target);
    /* a use whose leading prefix matches NO composer prefix stays
     * unresolved (external -- the Go module-boundary precedent) */
    CHECK(oc_resolve_php_use(cx, from, "Psr\\Log\\LoggerInterface", &a) == NULL);
    CHECK(oc_resolve_php_use(cx, from, "Doctrine\\ORM\\Query", &a) == NULL);
    /* a matching prefix whose class file is absent: unresolved (never
     * guessed); the nested composer.json's fake/ mapping never fires */
    CHECK(oc_resolve_php_use(cx, from, "Acme\\Shop\\Missing", &a) == NULL);
    /* non-use shapes: never guessed */
    CHECK(oc_resolve_php_use(cx, from, "", &a) == NULL);
    CHECK(oc_resolve_php_use(cx, from, "wat wat", &a) == NULL);

    /* the include channel: relative to the requiring file's directory;
     * lexical .. pops (root-anchored join); guards stay honest */
    char loader[1400];
    snprintf(loader, sizeof(loader), "%s/src/loader.php", root);
    CHECK_STR_EQ(oc_resolve_php_include(cx, loader, "require Shop.php", &a), paths[0]);
    CHECK_STR_EQ(
        oc_resolve_php_include(cx, loader, "include_once store/../Contracts/Mapper.php", &a),
        paths[1]);
    char orphan[1400];
    snprintf(orphan, sizeof(orphan), "%s/src/store/orphan.php", root);
    char mapper[1400];
    snprintf(mapper, sizeof(mapper), "%s/src/Contracts/Mapper.php", root);
    CHECK_STR_EQ(oc_resolve_php_include(cx, orphan, "include ../Contracts/Mapper.php", &a), mapper);
    /* absolute payloads, backslashes and empties: unresolved */
    CHECK(oc_resolve_php_include(cx, loader, "require /abs/path.php", &a) == NULL);
    CHECK(oc_resolve_php_include(cx, loader, "include back\\slash.php", &a) == NULL);
    CHECK(oc_resolve_php_include(cx, loader, "require ", &a) == NULL);
    CHECK(oc_resolve_php_include(cx, loader, "require missing.php", &a) == NULL);
    CHECK(oc_resolve_php_include(cx, loader, "wat", &a) == NULL);

    /* edges: set semantics (the same target named twice collapses) */
    oc_import_vec v;
    oc_imports_init(&v);
    CHECK(oc_imports_push(&v, &a, paths[6] /* loader */, "require Shop.php", OC_LANG_PHP));
    CHECK(oc_imports_push(&v, &a, loader, "require_once Shop.php", OC_LANG_PHP)); /* dup target */
    CHECK(
        oc_imports_push(&v, &a, loader, "include_once store/../Contracts/Mapper.php", OC_LANG_PHP));
    CHECK(oc_imports_push(&v, &a, loader, "Acme\\Shop\\Store\\Types", OC_LANG_PHP));
    CHECK(
        oc_imports_push(&v, &a, loader, "Psr\\Log\\LoggerInterface", OC_LANG_PHP)); /* unresolved */
    CHECK(oc_imports_resolve_ex(&g, &v, &a, roots, 1) == 3);
    CHECK(has_edge(&g, loader, paths[0]));
    CHECK(has_edge(&g, loader, paths[1]));
    CHECK(has_edge(&g, loader, paths[5]));
    oc_imports_destroy(&v);

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);

    /* the longest-prefix rule: A\B\ wins over A\ (a /tmp composer
     * fixture; the probe hits only registered file nodes) */
    oc_arena a2;
    oc_arena_init(&a2);
    oc_graph g2;
    oc_graph_init(&g2, &a2);
    char lp[64] = "/tmp/oc_s19_lp";
    mkdir(lp, 0755);
    char lpc[256];
    snprintf(lpc, sizeof(lpc), "%s/composer.json", lp);
    FILE *cf = fopen(lpc, "wb");
    CHECK(cf != NULL);
    if (cf) {
        /* the FILE bytes must carry TWO backslashes per separator (the
         * JSON escape for one); the C literal escapes each of the four
         * source backslashes */
        const char *cj = "{\"autoload\": {\"psr-4\": {\"A\\\\\": \"a/\","
                         " \"A\\\\B\\\\\": \"ab/\"}}}";
        fwrite(cj, 1, strlen(cj), cf);
        fclose(cf);
    }
    char f_ab[256], f_a[256];
    snprintf(f_ab, sizeof(f_ab), "%s/ab/C.php", lp);
    snprintf(f_a, sizeof(f_a), "%s/a/B/C.php", lp);
    oc_graph_add_file(&g2, f_ab);
    oc_graph_add_file(&g2, f_a);
    const char *roots2[1] = {lp};
    oc_resolve_ctx *cx2 = oc_resolve_ctx_new(&a2, &g2, roots2, 1);
    CHECK(cx2 != NULL);
    if (cx2) {
        char fromlp[256];
        snprintf(fromlp, sizeof(fromlp), "%s/a/B/Use.php", lp);
        /* A\B\C: the DEEPER prefix wins (A\B\ strips to ab/C.php; the
         * base A\ would give a/B/C.php) */
        CHECK_STR_EQ(oc_resolve_php_use(cx2, fromlp, "A\\B\\C", &a2), f_ab);
    }
    oc_graph_destroy(&g2);
    oc_arena_destroy(&a2);
}

/* --- unit: Clover coverage adapter ------------------------------------- */

static void unit_php_clover(void) {
    static const char clover[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<coverage generated=\"1764000000\">\n"
        "  <project timestamp=\"1764000000\" name=\"-acme-shop\">\n"
        "    <file name=\"" OC_REPO_ROOT "/tests/fixtures/php/mathx.php\">\n"
        "      <line num=\"18\" type=\"stmt\" count=\"1\"/>\n"
        "      <line num=\"21\" type=\"stmt\" count=\"2\"/>\n"
        "      <line num=\"25\" type=\"stmt\" count=\"0\"/>\n"
        "    </file>\n"
        "    <file name=\"/nonexistent/ghost.php\">\n"
        "      <line num=\"1\" type=\"stmt\" count=\"5\"/>\n"
        "    </file>\n"
        "  </project>\n"
        "</coverage>\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_coverage cov;
    oc_coverage_init(&cov);
    const char *paths[] = {"/tmp/omnicheck_test_clover.xml"};
    FILE *f = fopen(paths[0], "wb");
    CHECK(f != NULL);
    if (f) {
        fwrite(clover, 1, sizeof(clover) - 1, f);
        fclose(f);
    }
    CHECK(oc_coverage_load(&cov, paths, 1, &a, &st) == OC_OK);
    CHECK(cov.files_loaded == 2);
    CHECK(cov.n_files == 2);

    /* anchors + covered by definition-line window (lines 18..21) */
    size_t anchors = 0, covered = 0;
    int hit = oc_coverage_lookup(&cov, "tests/fixtures/php/mathx.php", 18, 21, &anchors, &covered);
    CHECK(hit == 1);
    CHECK(anchors == 2);
    CHECK(covered == 2); /* both records carry count > 0; line 25 (count 0)
                          * sits outside the window */
    hit = oc_coverage_lookup(&cov, "tests/fixtures/php/mathx.php", 25, 25, &anchors, &covered);
    CHECK(hit == 1);
    CHECK(anchors == 1);
    CHECK(covered == 0); /* count == 0: uncovered anchor */
    /* the ghost path maps to no analyzed file: counted in the ambiguity
     * meta, never guessed (lookup fails for the analyzed path) */
    hit = oc_coverage_lookup(&cov, "tests/fixtures/php/longfn.php", 1, 200, &anchors, &covered);
    CHECK(hit == 0);

    /* malformed <line> fails loud */
    static const char bad[] = "<coverage>\n<file name=\"x.php\">\n"
                              "<line num=\"NaN\" count=\"1\"/>\n</file>\n</coverage>\n";
    FILE *f2 = fopen(paths[0], "wb");
    CHECK(f2 != NULL);
    if (f2) {
        fwrite(bad, 1, sizeof(bad) - 1, f2);
        fclose(f2);
    }
    oc_status_clear(&st);
    oc_coverage cov2;
    oc_coverage_init(&cov2);
    CHECK(oc_coverage_load(&cov2, paths, 1, &a, &st) == OC_ERR_USAGE);
    oc_coverage_destroy(&cov2);
    oc_coverage_destroy(&cov);
    oc_arena_destroy(&a);
}

/* --- unit: composer manifests + packagist OSV -------------------------- */

static void unit_php_manifests(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_manifest_vec mv;
    const char *files[] = {
        OC_REPO_ROOT "/tests/fixtures/php_resolver/composer.json",
        OC_REPO_ROOT "/tests/fixtures/php_resolver/composer.lock",
        OC_REPO_ROOT "/tests/fixtures/php_resolver/src/nested/composer.json",
    };
    CHECK(oc_supply_load_manifests(&mv, files, 3, &a, &st) == OC_OK);
    CHECK(mv.n == 3);

    const oc_manifest *mj = NULL, *ml = NULL, *mn = NULL;
    for (size_t i = 0; i < mv.n; i++) {
        if (mv.items[i].kind == OC_MANIFEST_COMPOSER_JSON && strstr(mv.items[i].path, "nested"))
            mn = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_COMPOSER_JSON)
            mj = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_COMPOSER_LOCK)
            ml = &mv.items[i];
    }
    CHECK(mj != NULL && ml != NULL && mn != NULL);
    if (mj) {
        /* exact pins only; the ^2.0 range records nothing; the php
         * platform requirement records nothing */
        CHECK(mj->n_deps == 2);
        CHECK(mj->eco == OC_ECO_PACKAGIST);
        CHECK(mj->license == NULL); /* no licence field: OC010 material */
        int saw_log = 0, saw_dev = 0;
        for (size_t i = 0; i < mj->n_deps; i++) {
            if (STR_EQ(mj->deps[i].name, "acme/log") && STR_EQ(mj->deps[i].version, "1.2.3"))
                saw_log = 1;
            if (STR_EQ(mj->deps[i].name, "acme/devtool") && STR_EQ(mj->deps[i].version, "0.9.0"))
                saw_dev = 1;
            CHECK(!STR_EQ(mj->deps[i].name, "php"));       /* platform: nothing */
            CHECK(!STR_EQ(mj->deps[i].name, "acme/gear")); /* ^2.0: nothing */
        }
        CHECK(saw_log == 1);
        CHECK(saw_dev == 1);
    }
    if (ml) {
        /* the lock: packages + packages-dev are the RESOLVED pins; the
         * php-64bit platform entry records nothing */
        CHECK(ml->n_deps == 3);
        CHECK(ml->eco == OC_ECO_PACKAGIST);
        int saw_log = 0, saw_gear = 0, saw_dev = 0;
        for (size_t i = 0; i < ml->n_deps; i++) {
            if (STR_EQ(ml->deps[i].name, "acme/log") && STR_EQ(ml->deps[i].version, "1.2.3") &&
                ml->deps[i].pinned)
                saw_log = 1;
            if (STR_EQ(ml->deps[i].name, "acme/gear") && STR_EQ(ml->deps[i].version, "2.4.0"))
                saw_gear = 1;
            if (STR_EQ(ml->deps[i].name, "acme/devtool"))
                saw_dev = 1;
            CHECK(!STR_EQ(ml->deps[i].name, "php-64bit"));
        }
        CHECK(saw_log == 1 && saw_gear == 1 && saw_dev == 1);
    }
    if (mn) {
        CHECK(mn->n_deps == 1); /* acme/decoy-dep 1.0.0 */
        CHECK(mn->license == NULL);
    }

    /* OSV: ecosystem packagist; the lock pin matches, the outside-range
     * pin is silent */
    oc_osv_snapshot snap;
    oc_osv_init(&snap);
    CHECK(oc_osv_load(&snap, OC_REPO_ROOT "/tests/fixtures_sprint19/osv_snapshot_php.json", &a,
                      &st) == OC_OK);
    CHECK(STR_EQ(snap.snapshot_date, "2026-11-22"));
    CHECK(oc_osv_match(&snap, "acme/log", "1.2.3", OC_ECO_PACKAGIST) == 1);
    CHECK(oc_osv_match(&snap, "acme/log", "1.3.0", OC_ECO_PACKAGIST) == 0);
    CHECK(oc_osv_match(&snap, "acme/devtool", "0.9.0", OC_ECO_PACKAGIST) == 0);
    CHECK(oc_osv_match(&snap, "acme/gear", "2.4.0", OC_ECO_PACKAGIST) == 0);
    oc_osv_destroy(&snap);
    oc_manifests_destroy(&mv);
    oc_arena_destroy(&a);
}

/* --- integration: corpus A (structural) and the pack ------------------- */

static void unit_php_corpus_a(void) {
    char buf[65536];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/php", NULL, buf, sizeof(buf));
    /* parse clean, the php pack loaded */
    CHECK(strstr(buf, "\"files_analyzed\":9") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(strstr(buf, "\"php\"") != NULL);
    /* the enabled structural rules fire with the contracted counts */
    CHECK(json_count_rule(buf, "OC001") == 1);
    CHECK(json_count_rule(buf, "OC002") == 1);
    CHECK(json_count_rule(buf, "OC004") == 1);
    CHECK(json_count_rule(buf, "OC006") == 8);
    CHECK(json_count_rule(buf, "OC008") == 2);
    CHECK(json_count_rule(buf, "OC011") == 1);
    /* OC003/OC005 (import-driven) and OC007/OC009/OC010 (inputless)
     * never fire on this corpus */
    CHECK(json_count_rule(buf, "OC003") == 0);
    CHECK(json_count_rule(buf, "OC005") == 0);
    CHECK(json_count_rule(buf, "OC007") == 0);
    CHECK(json_count_rule(buf, "OC009") == 0);
    CHECK(json_count_rule(buf, "OC010") == 0);

    /* the OC006 set must NOT contain the bin script (claimed by the
     * pack's bin glob, not by any shebang hint -- PHP has none) */
    CHECK(strstr(buf, "tool.php") == NULL);
}

/* --- integration: the php pack declares full scope --------------------- */

static void unit_php_pack(void) {
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
        /* the php pack entry carries the full pass scope and the
         * contracted pack data */
        const char *entry = strstr(json, "\"id\":\"php\"");
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
            CHECK(strstr(seg, "\"grammar\":\"php\"") != NULL);
            CHECK(strstr(seg, "index.php") != NULL);
        }
        free(json);
    }
}

int main(void) {
    unit_php_symbols();
    unit_php_decision_table();
    unit_php_params();
    unit_php_import_records();
    unit_php_mixed_template();
    unit_php_surface();
    unit_php_resolver();
    unit_php_clover();
    unit_php_manifests();
    unit_php_corpus_a();
    unit_php_pack();
    TEST_SUMMARY();
}
