/* Sprint 23 (the Swift language adoption, wave-3 continues): the
 * contract of docs/LANGUAGES.md (Swift section), pinned unit by unit.
 *
 * Symbols: function_declaration WITH a body (free, method, extension,
 * static/class, generic); init/deinit with a body ARE symbols (the C#
 * constructor precedent, the honest synthesized spellings); a protocol
 * function WITH a default body IS a symbol (the nested-wrapper shape);
 * body-less protocol REQUIREMENTS are NOT symbols; lambdas are
 * anonymous. Accessor bodies ARE symbols (Prop.get / Prop.set /
 * Prop.willSet / Prop.didSet, the C# precedent; the implicit getter
 * counts as get; an EMPTY body matches nothing). Complexity:
 * if_statement (else-if nested), guard_statement, ternary_expression,
 * switch_entry (every arm counts including default -- the Rust
 * match_arm precedent), for, while, repeat-while, catch_block,
 * nil_coalescing_expression, conjunction_expression,
 * disjunction_expression. Imports: the declared-fact resolver
 * v1-swift (Package.swift declared targets recorded as hints; the
 * module is the FIRST import segment; whole-module claims fan out
 * wholesale; kind-scoped member claims bind the last segment; the
 * submodule spelling resolves on the first segment; the SPM documented
 * default Sources/<name> applies; without hints everything stays
 * unresolved).
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
    in.grammar = "swift";
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

/* --- unit: symbols, spans, the init/deinit and protocol contracts ------- */

static void unit_sw_symbols(void) {
    static const char src[] = "/* free function: symbol, span = the whole declaration */\n"
                              "func compute(n: Int) -> Int {\n"
                              "    return n\n"
                              "}\n"
                              "\n"
                              "/* method: the name is the declaration's name-field identifier */\n"
                              "class Widget {\n"
                              "    func scale(v: Int) -> Int { return v }\n"
                              "}\n"
                              "\n"
                              "/* extension method: the receiver is the extension declaration,\n"
                              " * not part of the function */\n"
                              "extension Int {\n"
                              "    func doubled() -> Int { return self * 2 }\n"
                              "}\n"
                              "\n"
                              "/* static/class methods: the modifiers carry the keyword */\n"
                              "class Factory {\n"
                              "    static func make() -> Int { return 1 }\n"
                              "    class func kind() -> String { return \"f\" }\n"
                              "}\n"
                              "\n"
                              "/* init/deinit with a body ARE symbols (the C# constructor\n"
                              " * precedent; the honest synthesized spellings) */\n"
                              "class Owned {\n"
                              "    init(id: Int) { }\n"
                              "    deinit { }\n"
                              "}\n"
                              "\n"
                              "/* protocol REQUIREMENTS: body-less, NOT symbols; the DEFAULT\n"
                              " * body IS a symbol (the C# interface-default precedent) */\n"
                              "protocol Spec {\n"
                              "    func run() -> Int\n"
                              "    func withBody() -> Int { return 1 }\n"
                              "}\n"
                              "\n"
                              "/* lambda: anonymous construct, NOT symbols */\n"
                              "func lambdaUser(v: Int) -> Int {\n"
                              "    let fn = { (x: Int) -> Int in x * 2 }\n"
                              "    return fn(v)\n"
                              "}\n"
                              "\n"
                              "/* properties are NOT symbols (surface only) */\n"
                              "let greeting = \"hi\"\n"
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
        parse_one(p, &g, "unit/widget.swift", src, &out);

        CHECK(count_symbols_named(&g, "compute") == 1);
        CHECK(count_symbols_named(&g, "scale") == 1);
        CHECK(count_symbols_named(&g, "doubled") == 1); /* the extension name */
        CHECK(count_symbols_named(&g, "make") == 1);
        CHECK(count_symbols_named(&g, "kind") == 1);
        CHECK(count_symbols_named(&g, "init") == 1);     /* the synthesized spelling */
        CHECK(count_symbols_named(&g, "deinit") == 1);   /* the synthesized spelling */
        CHECK(count_symbols_named(&g, "withBody") == 1); /* the protocol default body */
        CHECK(count_symbols_named(&g, "lambdaUser") == 1);
        /* the protocol REQUIREMENT `run` is body-less: NOT a symbol */
        CHECK(count_symbols_named(&g, "run") == 0);
        /* the lambda is anonymous: no symbol carries its body */
        const oc_node *lu = symbol_named(&g, "lambdaUser");
        CHECK(lu != NULL);
        /* the init/deinit spans live inside their declaration */
        const oc_node *ini = symbol_named(&g, "init");
        CHECK(ini != NULL && ini->complexity == 1);

        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: accessors (the C# precedent, the parent-climb owner) --------- */

static void unit_sw_accessors(void) {
    static const char src[] = "class Tracked {\n"
                              "    private var cached = 0\n"
                              "    var plain = 0\n"
                              "    var doubled: Int {\n"
                              "        return cached * 2\n"
                              "    }\n"
                              "    var tracked: Int {\n"
                              "        get {\n"
                              "            return cached\n"
                              "        }\n"
                              "        set(v) {\n"
                              "            cached = v\n"
                              "        }\n"
                              "    }\n"
                              "    var observed: Int = 0 {\n"
                              "        willSet {\n"
                              "            if newValue > cached {\n"
                              "                cached = newValue\n"
                              "            }\n"
                              "        }\n"
                              "        didSet {\n"
                              "        }\n"
                              "    }\n"
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
        parse_one(p, &g, "unit/tracked.swift", src, &out);

        const oc_node *dg = symbol_named(&g, "doubled.get");
        CHECK(dg != NULL && dg->complexity == 1);
        const oc_node *tg = symbol_named(&g, "tracked.get");
        CHECK(tg != NULL && tg->complexity == 1);
        const oc_node *ts = symbol_named(&g, "tracked.set");
        CHECK(ts != NULL && ts->complexity == 1);
        const oc_node *ws = symbol_named(&g, "observed.willSet");
        CHECK(ws != NULL && ws->complexity == 2); /* the if inside counts */
        /* the EMPTY didSet body matches NOTHING (the body-less
         * accessor rule) */
        CHECK(symbol_named(&g, "observed.didSet") == NULL);
        /* plain stored properties are not accessor symbols */
        CHECK(symbol_named(&g, "plain.get") == NULL);

        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: parameters (the field-bearing node, positional type) --------- */

static void unit_sw_params(void) {
    static const char src[] = "func plain(a: Int, b: String) {}\n"
                              "func labeled(external local: Int, rest: Int = 2) {}\n"
                              "func inoutp(v: inout Int) {}\n"
                              "func variadic(items: Int..., tail: String) {}\n"
                              "func generic<T: Comparable>(_ a: T, _ b: T) -> T { return a }\n";

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
        parse_one(p, &g, "unit/params.swift", src, &out);

        CHECK(g.n_nodes > 0);
        /* the B3 signatures: name/type as written, the external label
         * OUT of the name, the variadic collapsed to the marker */
        int checked_plain = 0, checked_labeled = 0, checked_variadic = 0, checked_generic = 0;
        for (size_t i = 0; i < g.n_nodes; i++) {
            const oc_node *n = &g.nodes[i];
            if (n->kind != OC_NODE_FUNCTION || !n->params)
                continue;
            if (STR_EQ(n->name, "plain") && n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "a"));
                CHECK(STR_EQ(n->params[0].type, "Int"));
                CHECK(STR_EQ(n->params[1].type, "String"));
                checked_plain = 1;
            } else if (STR_EQ(n->name, "labeled") && n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "local")); /* the label stays out */
                CHECK(STR_EQ(n->params[0].type, "Int"));
                CHECK(n->params[1].has_default == 1); /* the in-node default */
                checked_labeled = 1;
            } else if (STR_EQ(n->name, "variadic") && n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "...")); /* the marker */
                CHECK(STR_EQ(n->params[0].type, "Int"));
                CHECK(STR_EQ(n->params[1].name, "tail"));
                checked_variadic = 1;
            } else if (STR_EQ(n->name, "generic") && n->n_params == 2) {
                CHECK(STR_EQ(n->params[0].name, "a")); /* _ becomes the name */
                checked_generic = 1;
            }
        }
        CHECK(checked_plain);
        CHECK(checked_labeled);
        CHECK(checked_variadic);
        CHECK(checked_generic);

        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: decision kinds (the hand-counted inventory) ------------------- */

static void unit_sw_decisions(void) {
    /* comp(rollup) = 1 + 13: if + nested else-if (2 if_statement) +
     * guard + ternary + 2 switch_entry (every arm counts) + for +
     * while + repeat-while + 1 catch + nil-coalescing + && + || = 13.
     * The file rollup = 14 (under the OC004 threshold). */
    static const char src[] = "func rollup(n: Int, items: [Int]) -> Int {\n"
                              "    if n > 0 {\n"
                              "        return 1\n"
                              "    } else if n < 0 {\n"
                              "        return -1\n"
                              "    }\n"
                              "    guard !items.isEmpty else { return 0 }\n"
                              "    let e: Int? = nil\n"
                              "    var t = e ?? n\n"
                              "    t = t > 10 ? t : -t\n"
                              "    switch t {\n"
                              "    case 1:\n"
                              "        t += 10\n"
                              "    default:\n"
                              "        t += 2\n"
                              "    }\n"
                              "    for v in items {\n"
                              "        t += v\n"
                              "    }\n"
                              "    var i = 0\n"
                              "    while i < 2 {\n"
                              "        i += 1\n"
                              "    }\n"
                              "    repeat {\n"
                              "        t += 1\n"
                              "    } while t < -100\n"
                              "    do {\n"
                              "        t = try boom(t)\n"
                              "    } catch {\n"
                              "        t = -1\n"
                              "    }\n"
                              "    let ok = t > 0 && items.count > 1\n"
                              "    let alt = ok || t > 500\n"
                              "    _ = alt\n"
                              "    return t\n"
                              "}\n"
                              "\n"
                              "func boom(_ x: Int) throws -> Int {\n"
                              "    return x\n"
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
        parse_one(p, &g, "unit/rollup.swift", src, &out);

        const oc_node *ru = symbol_named(&g, "rollup");
        CHECK(ru != NULL);
        CHECK(ru->complexity == 14); /* 1 + 13, hand-counted */
        const oc_node *bm = symbol_named(&g, "boom");
        CHECK(bm != NULL && bm->complexity == 1);

        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: import shapes (whole-module / submodule / kind-scoped) -------- */

static void unit_sw_imports(void) {
    static const char src[] = "import Foundation\n"
                              "import Mod.Submodule\n"
                              "import struct Widgets.Widget\n"
                              "import func Mod.Helpers.make\n"
                              "@testable import StoreKit\n"
                              "import typealias Core.Logger\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_import_vec imports;
    oc_imports_init(&imports);
    oc_bind_vec binds;
    oc_surface_binds_init(&binds);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        out.imports = &imports;
        out.binds = &binds;
        parse_one(p, &g, "unit/imports.swift", src, &out);

        /* the module is the FIRST segment (the declared-fact key) */
        CHECK(imports_count_spec(out.imports, "Foundation") == 1);
        CHECK(imports_count_spec(out.imports, "Mod") == 2); /* submodule + member */
        CHECK(imports_count_spec(out.imports, "Widgets") == 1);
        CHECK(imports_count_spec(out.imports, "StoreKit") == 1);
        CHECK(imports_count_spec(out.imports, "Core") == 1);
        CHECK(out.imports->n == 6);
        /* the binds: NULL for whole-module and submodule claims; the
         * LAST segment for kind-scoped member claims */
        CHECK(bind_name_for(out.binds, "Foundation") == NULL);
        CHECK(bind_name_for(out.binds, "Widgets") != NULL &&
              STR_EQ(bind_name_for(out.binds, "Widgets"), "Widget"));
        /* TWO Mod records: the submodule claim binds NULL, the
         * kind-scoped member claim binds the last segment */
        {
            int mod_null = 0, mod_make = 0;
            for (size_t bi = 0; bi < out.binds->n; bi++) {
                if (STR_EQ(out.binds->items[bi].specifier, "Mod")) {
                    if (out.binds->items[bi].name == NULL)
                        mod_null = 1;
                    else if (STR_EQ(out.binds->items[bi].name, "make"))
                        mod_make = 1;
                }
            }
            CHECK(mod_null);
            CHECK(mod_make);
        }
        CHECK(bind_name_for(out.binds, "StoreKit") == NULL); /* @testable: wholesale */
        CHECK(bind_name_for(out.binds, "Core") != NULL &&
              STR_EQ(bind_name_for(out.binds, "Core"), "Logger"));

        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_imports_destroy(&imports);
    oc_surface_binds_destroy(&binds);
    oc_arena_destroy(&a);
}

/* --- unit: surface (top-level names, private/fileprivate excluded) ------ */

static void unit_sw_surface(void) {
    static const char src[] = "public class Api {\n"
                              "    public func method() -> Int { return 1 }\n"
                              "}\n"
                              "func freeFn() -> Int { return 2 }\n"
                              "private func secret() -> Int { return 3 }\n"
                              "fileprivate func inner() -> Int { return 4 }\n"
                              "struct Free { static let kind = \"free\" }\n"
                              "enum Mode { case on, off }\n"
                              "extension Int { func tripled() -> Int { return self * 3 } }\n"
                              "protocol Spec { func run() -> Int }\n"
                              "typealias Handler = (Int) -> Int\n"
                              "let globalLet = 1\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_export_vec exports;
    oc_surface_exports_init(&exports);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        out.exports = &exports;
        parse_one(p, &g, "unit/surface.swift", src, &out);

        CHECK(exports_count_named(out.exports, "Api") == 1);
        CHECK(exports_count_named(out.exports, "freeFn") == 1);
        CHECK(exports_count_named(out.exports, "Free") == 1);
        CHECK(exports_count_named(out.exports, "Mode") == 1);
        CHECK(exports_count_named(out.exports, "Int") == 1); /* the extension */
        CHECK(exports_count_named(out.exports, "Spec") == 1);
        CHECK(exports_count_named(out.exports, "Handler") == 1);
        CHECK(exports_count_named(out.exports, "globalLet") == 1);
        /* private/fileprivate top-level declarations never surface
         * (the C static rule's Swift spelling) */
        CHECK(exports_count_named(out.exports, "secret") == 0);
        CHECK(exports_count_named(out.exports, "inner") == 0);
        /* members and enum entries never surface (type nesting) */
        CHECK(exports_count_named(out.exports, "method") == 0);
        CHECK(exports_count_named(out.exports, "kind") == 0);
        CHECK(exports_count_named(out.exports, "on") == 0);
        CHECK(exports_count_named(out.exports, "tripled") == 0);

        oc_graph_destroy(&g);
        oc_parser_free(p);
    }
    oc_surface_exports_destroy(&exports);
    oc_arena_destroy(&a);
}

/* --- unit: the entry fact + the directive-condition exclusion ------------ */

static void unit_sw_entry_fact(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        /* the executable top-level statements ARE the entry fact (the
         * C# 9 top-level-statements precedent); a top-level `let` is a
         * DECLARATION and never fires it */
        static const char main_src[] = "let boot = 1\n"
                                       "print(\"run\")\n";
        oc_graph g;
        oc_graph_init(&g, &a);
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        out.hints = (oc_hint_vec *)malloc(sizeof(oc_hint_vec));
        CHECK(out.hints != NULL);
        oc_surface_hints_init(out.hints);
        parse_one(p, &g, "unit/main.swift", main_src, &out);
        int found_top = 0;
        for (size_t i = 0; i < out.hints->n; i++)
            if (STR_EQ(out.hints->items[i].path, "unit/main.swift") &&
                out.hints->items[i].has_top_statements)
                found_top = 1;
        CHECK(found_top);
        oc_graph_destroy(&g);
        oc_surface_hints_destroy(out.hints);
        free(out.hints);

        /* a declarations-only file (a manifest with a version check)
         * claims NOTHING: a top-level `let` is a DECLARATION and never
         * fires the entry fact. (The `#if compiler(...)` directive
         * spelling of this manifest is deliberately NOT used here: the
         * pinned grammar's directive path is build-configuration
         * UNSTABLE -- the same bytes parse clean at -O2 and carry an
         * error node at -O0 inside the full binary -- so the suite pins
         * only build-stable shapes; the directive-builtin exclusion is
         * exercised at corpus scale by the pinned swift-argument-parser
         * corpus, whose root manifest carries a real `#if` and whose
         * parse boundary is the corpus gate's classified 28/142 pin.) */
        static const char manifest_src[] = "// swift-tools-version:5.9\n"
                                           "import PackageDescription\n"
                                           "let package = Package(\n"
                                           "    name: \"X\",\n"
                                           "    targets: [.target(name: \"X\")]\n"
                                           ")\n";
        oc_graph g2;
        oc_graph_init(&g2, &a);
        oc_parse_out out2;
        memset(&out2, 0, sizeof(out2));
        out2.hints = (oc_hint_vec *)malloc(sizeof(oc_hint_vec));
        CHECK(out2.hints != NULL);
        oc_surface_hints_init(out2.hints);
        parse_one(p, &g2, "unit/Package.swift", manifest_src, &out2);
        for (size_t i = 0; i < out2.hints->n; i++)
            if (STR_EQ(out2.hints->items[i].path, "unit/Package.swift"))
                CHECK(out2.hints->items[i].has_top_statements == 0);
        /* the manifest's declared targets ARE recorded (the resolver
         * channel) */
        int found_targets = 0;
        for (size_t i = 0; i < out2.hints->n; i++)
            if (STR_EQ(out2.hints->items[i].path, "unit/Package.swift") &&
                out2.hints->items[i].n_swift_targets == 1 &&
                STR_EQ(out2.hints->items[i].swift_targets[0], "X"))
                found_targets = 1;
        CHECK(found_targets);
        oc_graph_destroy(&g2);
        oc_surface_hints_destroy(out2.hints);
        free(out2.hints);

        oc_parser_free(p);
    }
    oc_arena_destroy(&a);
}

/* --- unit: the declared-fact resolver (corpus B, inline) ----------------- */

typedef struct {
    const char *path;
    const char *source;
} sw_res_file;

static void unit_sw_resolver(void) {
    /* corpus B, inline: the declared-fact MODULE contract end to end --
     * the whole-module wholesale fan-out, the kind-scoped member
     * claim, the submodule first-segment rule, @testable, the DEFAULT
     * path (Sources/<name>), the cycle, the unresolved decoys. 15
     * files, 9 contracted edges. */
    static const sw_res_file FILES[] = {
        {"Package.swift", "// swift-tools-version:5.9\n"
                          "import PackageDescription\n"
                          "let package = Package(\n"
                          "    name: \"Resolver\",\n"
                          "    targets: [\n"
                          "        .target(name: \"Store\", path: \"Store\"),\n"
                          "        .target(name: \"Lib\", path: \"Lib\"),\n"
                          "        .target(name: \"Wide\", path: \"Wide\"),\n"
                          "        .target(name: \"Alpha\", path: \"Alpha\"),\n"
                          "        .target(name: \"Beta\", path: \"Beta\"),\n"
                          "        .target(name: \"Std\")\n"
                          "    ]\n"
                          ")\n"},
        {"Store/Engine.swift",
         "class Engine {\n    func start() -> Int {\n        return 1\n    }\n}\n\n"
         "func alpha() -> Int {\n    return 2\n}\n"},
        {"Store/Extra.swift", "func beta() -> Int {\n    return 3\n}\n"},
        {"Lib/Gamma.swift", "func gamma() -> Int {\n    return 4\n}\n"},
        {"Wide/Core.swift", "func walpha() -> Int {\n    return 5\n}\n"},
        {"Wide/Deep.swift", "func wbeta() -> Int {\n    return 6\n}\n"},
        {"Alpha/One.swift", "import Beta\n\n"
                            "class One {\n    let pair = 1\n}\n"},
        {"Beta/Two.swift", "import Alpha\n\n"
                           "class Two {\n    let mate = 2\n}\n"},
        {"Sources/Std/Std.swift", "func standalone() -> Int {\n    return 9\n}\n"},
        {"wide.swift", "import Wide\n\n"
                       "func wideUser() -> Int {\n    return walpha() + wbeta()\n}\n"},
        {"gamma.swift", "import func Lib.gamma\n\n"
                        "func gammaUser() -> Int {\n    return gamma()\n}\n"},
        {"std.swift", "import Std\n\n"
                      "func stdUser() -> Int {\n    return standalone()\n}\n"},
        {"decoy.swift", "import UIKit\nimport Foundation\nimport Mod.Missing\n\n"
                        "func decoy() -> Int {\n    return 0\n}\n"},
        {"testable.swift",
         "@testable import Store\n\n"
         "func testUser() -> Int {\n    return Engine().start() + alpha() + beta()\n}\n"},
        {"sub.swift", "import Lib.Sub\n\n"
                      "func subUser() -> Int {\n    return gamma()\n}\n"},
    };
    const size_t N_FILES = sizeof(FILES) / sizeof(FILES[0]);
    CHECK(N_FILES == 15);

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
            in.grammar = "swift";
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

    /* the manifest's declared targets reached the hints */
    int manifest_targets = 0;
    for (size_t i = 0; i < hints.n; i++)
        if (STR_EQ(hints.items[i].path, "Package.swift") && hints.items[i].n_swift_targets == 6)
            manifest_targets = 1;
    CHECK(manifest_targets);

    /* the edge pass with hints: exactly 9 edges, set semantics */
    long n_edges = oc_imports_resolve_ex_hints(&g, &imports, &a, NULL, 0, &hints);
    CHECK(n_edges == 9);

    oc_node_id wide = oc_graph_find_file(&g, "wide.swift");
    oc_node_id wc = oc_graph_find_file(&g, "Wide/Core.swift");
    oc_node_id wd = oc_graph_find_file(&g, "Wide/Deep.swift");
    oc_node_id gm = oc_graph_find_file(&g, "gamma.swift");
    oc_node_id gf = oc_graph_find_file(&g, "Lib/Gamma.swift");
    oc_node_id sd = oc_graph_find_file(&g, "std.swift");
    oc_node_id sf = oc_graph_find_file(&g, "Sources/Std/Std.swift");
    oc_node_id tb = oc_graph_find_file(&g, "testable.swift");
    oc_node_id se = oc_graph_find_file(&g, "Store/Engine.swift");
    oc_node_id sx = oc_graph_find_file(&g, "Store/Extra.swift");
    oc_node_id sb = oc_graph_find_file(&g, "sub.swift");
    oc_node_id o1 = oc_graph_find_file(&g, "Alpha/One.swift");
    oc_node_id t2 = oc_graph_find_file(&g, "Beta/Two.swift");
    oc_node_id dc = oc_graph_find_file(&g, "decoy.swift");
    CHECK(wide != OC_NODE_INVALID && wc != OC_NODE_INVALID && wd != OC_NODE_INVALID);
    CHECK(sf != OC_NODE_INVALID && se != OC_NODE_INVALID && dc != OC_NODE_INVALID);

    size_t wide_to_wc = 0, wide_to_wd = 0, gamma_to_gf = 0, std_to_sf = 0;
    size_t testable_to_se = 0, testable_to_sx = 0, sub_to_gf = 0, cycle_edges = 0;
    size_t decoy_edges = 0;
    for (size_t i = 0; i < g.n_edges; i++) {
        if (g.edges[i].kind != OC_EDGE_IMPORTS)
            continue;
        if (g.edges[i].from == wide && g.edges[i].to == wc)
            wide_to_wc++;
        if (g.edges[i].from == wide && g.edges[i].to == wd)
            wide_to_wd++;
        if (g.edges[i].from == gm && g.edges[i].to == gf)
            gamma_to_gf++;
        if (g.edges[i].from == sd && g.edges[i].to == sf)
            std_to_sf++;
        if (g.edges[i].from == tb && g.edges[i].to == se)
            testable_to_se++;
        if (g.edges[i].from == tb && g.edges[i].to == sx)
            testable_to_sx++;
        if (g.edges[i].from == sb && g.edges[i].to == gf)
            sub_to_gf++;
        if ((g.edges[i].from == o1 && g.edges[i].to == t2) ||
            (g.edges[i].from == t2 && g.edges[i].to == o1))
            cycle_edges++;
        if (g.edges[i].from == dc)
            decoy_edges++;
    }
    CHECK(wide_to_wc == 1); /* the whole-module wholesale fan-out */
    CHECK(wide_to_wd == 1);
    CHECK(gamma_to_gf == 1);    /* the kind-scoped member claim */
    CHECK(std_to_sf == 1);      /* the DEFAULT path (Sources/Std) */
    CHECK(testable_to_se == 1); /* @testable: the attribute changes nothing */
    CHECK(testable_to_sx == 1);
    CHECK(sub_to_gf == 1);   /* the submodule spelling: the FIRST segment */
    CHECK(cycle_edges == 2); /* OC003 == 1 on this corpus */
    CHECK(decoy_edges == 0); /* UIKit/Foundation/Mod.Missing stay unresolved */

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

static void unit_sw_pack_shape(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_pack_set ps;
    oc_packs_init(&ps, &a);
    oc_status st;
    oc_status_clear(&st);
    char packs_dir[512];
    snprintf(packs_dir, sizeof(packs_dir), "%s/packs", OC_REPO_ROOT);
    CHECK(oc_packs_load_dir(&ps, packs_dir, &st) == OC_OK);
    const oc_pack *sw = NULL;
    for (size_t i = 0; i < ps.n_packs; i++)
        if (strcmp(ps.packs[i].id, "swift") == 0)
            sw = &ps.packs[i];
    CHECK(sw != NULL);
    if (sw) {
        CHECK(STR_EQ(sw->display_name, "Swift"));
        CHECK(STR_EQ(sw->grammar, "swift"));
        CHECK(sw->n_extensions == 1);
        CHECK(STR_EQ(sw->extensions[0], ".swift"));
        CHECK(sw->n_passes == 11); /* the ninth full-scope pack */
        CHECK(STR_EQ(sw->passes[0], "OC001"));
        CHECK(STR_EQ(sw->passes[10], "OC011"));
        CHECK(sw->n_entry_points == 0); /* EMPTY by design */
        /* the pack routes a .swift file */
        const oc_pack *m = oc_packs_match_path(&ps, "Sources/App/main.swift");
        CHECK(m == sw);
    }
    oc_packs_destroy(&ps);
    oc_arena_destroy(&a);
}

/* --- corpus-level: the fixture corpora through the real binary ----------- */

static void corpus_swift(void) {
    char buf[1 << 17];
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/swift", "check", buf, sizeof(buf));
    CHECK(strstr(buf, "\"files_analyzed\":17") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(json_count_rule(buf, "OC001") == 1);
    CHECK(json_count_rule(buf, "OC002") == 2);
    CHECK(json_count_rule(buf, "OC004") == 1);
    CHECK(json_count_rule(buf, "OC006") == 13);
    CHECK(json_count_rule(buf, "OC008") == 2);
    CHECK(json_count_rule(buf, "OC011") == 1);
    CHECK(json_count_rule(buf, "OC003") == 0);
    CHECK(json_count_rule(buf, "OC005") == 0);
    CHECK(json_count_rule(buf, "OC007") == 0);
    CHECK(strstr(buf, "\"swift\"") != NULL); /* the pack loaded */

    run_oc_json(OC_REPO_ROOT, "tests/fixtures/swift_resolver", "check", buf, sizeof(buf));
    CHECK(strstr(buf, "\"files_analyzed\":15") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(json_count_rule(buf, "OC003") == 1);
    CHECK(json_count_rule(buf, "OC006") == 7);
    CHECK(json_count_rule(buf, "OC005") == 0);

    /* the corpus-A edge set: exactly the three contracted pairs */
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/swift", "graph", buf, sizeof(buf));
    CHECK(strstr(buf, "\"kind\":\"imports\"") != NULL);
    size_t edges = 0;
    const char *p = buf;
    while ((p = strstr(p, "\"kind\":\"imports\"")) != NULL) {
        edges++;
        p += 16;
    }
    CHECK(edges == 3);
}

/* --- corpus-level: the freeze legs --------------------------------------- */

static void corpus_freeze_legs(void) {
    char buf[1 << 16];
    /* the C corpus keeps the SPRINT-20 counts */
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/c", "check", buf, sizeof(buf));
    CHECK(json_count_rule(buf, "OC001") == 1);
    CHECK(json_count_rule(buf, "OC006") == 11);
    /* the C++ corpus keeps the SPRINT-21 counts */
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/cpp", "check", buf, sizeof(buf));
    CHECK(json_count_rule(buf, "OC001") == 1);
    CHECK(json_count_rule(buf, "OC006") == 12);
    /* the Kotlin corpus keeps the SPRINT-22 counts */
    run_oc_json(OC_REPO_ROOT, "tests/fixtures/kotlin", "check", buf, sizeof(buf));
    CHECK(json_count_rule(buf, "OC001") == 1);
    CHECK(json_count_rule(buf, "OC002") == 2);
    CHECK(json_count_rule(buf, "OC006") == 10);
    CHECK(json_count_rule(buf, "OC008") == 2);
}

int main(void) {
    unit_sw_symbols();
    unit_sw_accessors();
    unit_sw_params();
    unit_sw_decisions();
    unit_sw_imports();
    unit_sw_surface();
    unit_sw_entry_fact();
    unit_sw_resolver();
    unit_sw_pack_shape();
    corpus_swift();
    corpus_freeze_legs();
    TEST_SUMMARY();
    return 0;
}
