/* Sprint 16 (the Ruby language adoption): the contract of
 * docs/LANGUAGES.md (Ruby section), pinned unit by unit.
 *
 * Symbols: method + singleton_method (endless defs are the same node;
 * empty methods are symbols with complexity exactly 1 through the
 * bodyless slot; operator/setter names keep the name as written; blocks
 * are NEVER symbols). Complexity: the decision table -- if/unless/while/
 * until/for + when (one per clause; comma patterns are ONE clause) +
 * in_clause (one per clause; an if_guard does NOT count separately) +
 * rescue + the five postfix modifier forms + the ternary, plus &&/||/and/
 * or inside `binary` (the named-only kind match keeps the keyword tokens
 * from double-counting). Imports: the require/require_relative call
 * channel with the exact-one-static-string guard. Resolver v1-ruby:
 * require_relative against the requiring file's directory (root-anchored
 * so `..` pops lexically), bare require through the two load-root
 * conventions, external unresolved, set-semantics edges. Surface:
 * top-level class/module/constant exports, the shebang hint, the
 * runner-collected NAME conventions. Coverage: the SimpleCov adapter
 * (positional lines, null = not relevant, loud malformed input). Supply:
 * Gemfile pins (bundler semantics), Gemfile.lock resolved pins, the
 * project-root licence probe, OSV ecosystem rubygems.
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

#define STR_EQ(a, b) ((a) != NULL && (b) != NULL && strcmp((a), (b)) == 0)

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the built oc binary"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif

/* SPRINT-18 portability: substitute the @@OC_REPO_ROOT@@ placeholder of
 * a committed coverage fixture template with THIS machine's repo root
 * into a /tmp copy (the V8/istanbul fixtures used to bake in the
 * generating machine's absolute path, which made the suites unrunnable
 * on any other checkout). Returns 0 on any failure; callers CHECK it. */
static int fixture_subst(const char *tpl_path, const char *out_path) {
    char *tpl = NULL;
    size_t tlen = 0;
    oc_status st;
    oc_status_clear(&st);
    if (oc_read_file(tpl_path, &tpl, &tlen, &st) != OC_OK)
        return 0;
    const char *ph = "@@OC_REPO_ROOT@@";
    size_t phl = strlen(ph), rl = strlen(OC_REPO_ROOT);
    size_t n = 0;
    for (const char *p = tpl; (p = strstr(p, ph)) != NULL; p += phl)
        n++;
    long delta = (long)rl - (long)phl;
    long olen = (long)tlen + delta * (long)n;
    char *out = olen > 0 ? malloc((size_t)olen) : NULL;
    int ok = 0;
    if (out) {
        size_t w = 0;
        const char *p = tpl, *q;
        for (;;) {
            q = strstr(p, ph);
            if (!q) {
                size_t rest = tlen - (size_t)(p - tpl);
                memcpy(out + w, p, rest);
                w += rest;
                break;
            }
            memcpy(out + w, p, (size_t)(q - p));
            w += (size_t)(q - p);
            memcpy(out + w, OC_REPO_ROOT, rl);
            w += rl;
            p = q + phl;
        }
        FILE *f = fopen(out_path, "wb");
        if (f) {
            ok = fwrite(out, 1, w, f) == w;
            ok = (fclose(f) == 0) && ok;
        }
        free(out);
    }
    free(tpl);
    return ok;
}

/* --- helpers ----------------------------------------------------------- */

static size_t count_symbols_named(const oc_graph *g, const char *name) {
    size_t n = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].kind == OC_NODE_FUNCTION && strcmp(g->nodes[i].name, name) == 0)
            n++;
    }
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
    in.grammar = "ruby";
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

/* --- unit: symbols, spans, complexity ---------------------------------- */

static void unit_ruby_symbols(void) {
    static const char src[] = "# frozen_string_literal: true\n"
                              "\n"
                              "class Widget\n"
                              "  # a plain method: symbol, span = the whole definition\n"
                              "  def plain(a, b = 1)\n"
                              "    a + b\n"
                              "  end\n"
                              "\n"
                              "  # a singleton method: symbol, same span contract\n"
                              "  def self.build(name)\n"
                              "    new(name)\n"
                              "  end\n"
                              "\n"
                              "  # an endless definition: the SAME method node\n"
                              "  def tag = :endless\n"
                              "\n"
                              "  # an empty method: symbol through the bodyless slot, comp 1\n"
                              "  def hook; end\n"
                              "\n"
                              "  # a setter: the name as written\n"
                              "  def label=(v)\n"
                              "    @label = v\n"
                              "  end\n"
                              "\n"
                              "  # an operator: the name as written\n"
                              "  def [](idx)\n"
                              "    idx\n"
                              "  end\n"
                              "\n"
                              "  # a block is NEVER a symbol; its decisions count inward\n"
                              "  def with_block(xs)\n"
                              "    xs.each do |x|\n"
                              "      return x if x.positive?\n"
                              "    end\n"
                              "    0\n"
                              "  end\n"
                              "end\n"
                              "\n"
                              "class << self\n"
                              "  # a singleton-class method is an ordinary method symbol\n"
                              "  def meta; 1; end\n"
                              "end\n";

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
        parse_one(p, &g, "unit/widget.rb", src, &out);

        CHECK(count_symbols_named(&g, "plain") == 1);
        CHECK(count_symbols_named(&g, "build") == 1);  /* def self.build */
        CHECK(count_symbols_named(&g, "tag") == 1);    /* endless */
        CHECK(count_symbols_named(&g, "hook") == 1);   /* empty */
        CHECK(count_symbols_named(&g, "label=") == 1); /* setter, name as written */
        CHECK(count_symbols_named(&g, "[]") == 1);     /* operator, name as written */
        CHECK(count_symbols_named(&g, "with_block") == 1);
        CHECK(count_symbols_named(&g, "meta") == 1);
        /* the block body is NOT a symbol (no invented name) */
        CHECK(count_symbols_named(&g, "x") == 0);

        /* span contract: the whole definition extent */
        const oc_node *n = symbol_named(&g, "plain");
        CHECK(n != NULL);
        if (n) {
            CHECK(n->decl_line == 5);
            CHECK(n->def_start_line == 5);
            CHECK(n->def_end_line == 7);
            CHECK(n->complexity == 1);
        }
        /* the empty method: complexity exactly 1 */
        n = symbol_named(&g, "hook");
        CHECK(n != NULL);
        if (n)
            CHECK(n->complexity == 1);
        /* the block's if_modifier counts INWARD (inclusive) */
        n = symbol_named(&g, "with_block");
        CHECK(n != NULL);
        if (n)
            CHECK(n->complexity == 2);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the decision table (hand-counts) ---------------------------- */

static void unit_ruby_decision_table(void) {
    struct {
        const char *name;
        const char *body;
        size_t comp;
    } cases[] = {
        /* if + elsif x2 (each branch by itself; else never) = 3 */
        {"branch_chain",
         "def branch_chain(x)\n"
         "  if x > 0\n"
         "    1\n"
         "  elsif x == 0\n"
         "    2\n"
         "  elsif x < 0\n"
         "    3\n"
         "  else\n"
         "    4\n"
         "  end\n"
         "end\n",
         4},
        /* ONE decision per when clause: the comma patterns are ONE clause */
        {"when_clauses",
         "def when_clauses(x)\n"
         "  case x\n"
         "  when 0, 1 then 1\n"
         "  when 2 then 2\n"
         "  else 3\n"
         "  end\n"
         "end\n",
         3},
        /* pattern matching: one decision per in_clause (2 here); the
         * if_guard does NOT count separately (a failing guard falls to
         * the next clause) */
        {"in_clauses",
         "def in_clauses(x)\n"
         "  case x\n"
         "  in Integer => n then n\n"
         "  in [a, b] if a then b\n"
         "  else 0\n"
         "  end\n"
         "end\n",
         3},
        /* rescue clauses count; ensure does not */
        {"rescues",
         "def rescues(x)\n"
         "  begin\n"
         "    x / 1\n"
         "  rescue ZeroDivisionError\n"
         "    0\n"
         "  rescue TypeError => e\n"
         "    -1\n"
         "  rescue StandardError\n"
         "    -2\n"
         "  ensure\n"
         "    x + 0\n"
         "  end\n"
         "end\n",
         4},
        /* the five postfix modifier forms, each one decision */
        {"modifiers",
         "def modifiers(x)\n"
         "  a = 1\n"
         "  a += 1 if x > 0\n"
         "  a += 1 unless x.nil?\n"
         "  a += 1 while x > 99\n"
         "  a += 1 until x > 999\n"
         "  b = a rescue 0\n"
         "  a\n"
         "end\n",
         6},
        /* the ternary + both operator spellings: && || and or (each postfix
         * if is itself one decision) */
        {"ops",
         "def ops(x, y)\n"
         "  z = x ? 1 : 2\n"
         "  z += 1 if x && y\n"
         "  z += 1 if x || y\n"
         "  z = 1 and z = 2\n"
         "  z = 3 or z = 4\n"
         "  z\n"
         "end\n",
         8},
        /* the comp-10 corpus shape: if/elsif/while/for/when x2/ternary/
         * if_modifier+&& = 9 decisions */
        {"flow",
         "def flow(x)\n"
         "  if x > 0\n"
         "    sign = 1\n"
         "  elsif x == 0\n"
         "    sign = 0\n"
         "  else\n"
         "    sign = -1\n"
         "  end\n"
         "  while x > 100\n"
         "    x -= 100\n"
         "  end\n"
         "  for i in 0..2\n"
         "    sign += i\n"
         "  end\n"
         "  case x\n"
         "  when 0, 1 then tag = :low\n"
         "  when Integer then tag = :num\n"
         "  else tag = :other\n"
         "  end\n"
         "  z = sign.positive? ? 1 : 0\n"
         "  z += 1 if x.positive? && sign.positive?\n"
         "  z\n"
         "end\n",
         10},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
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
            parse_one(p, &g, "unit/decisions.rb", cases[i].body, &out);
            const oc_node *n = symbol_named(&g, cases[i].name);
            CHECK(n != NULL);
            if (n) {
                if (n->complexity != cases[i].comp)
                    fprintf(stderr, "  comp %s: got %zu want %zu\n", cases[i].name, n->complexity,
                            cases[i].comp);
                CHECK(n->complexity == cases[i].comp);
            }
        }
        oc_parser_free(p);
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
    }
}

/* --- unit: the require/require_relative channel ------------------------ */

static void unit_ruby_imports(void) {
    static const char src[] = "# frozen_string_literal: true\n"
                              "\n"
                              "require 'json'\n"
                              "require \"a/b\"\n"
                              "require_relative 'store/types'\n"
                              "require_relative \"../lib/helper\"\n"
                              "Kernel.require 'set'\n"
                              "require 'engine', 'render'\n"
                              "require \"#{dir}/dynamic\"\n"
                              "require nonstring_arg\n"
                              "require 'x' if cond\n"
                              "\n"
                              "def shell\n"
                              "  require_relative 'nested/thing'\n"
                              "end\n";

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
        parse_one(p, &g, "unit/requires.rb", src, &out);

        /* the two channels, tagged specifiers (the Rust `mod ` precedent) */
        CHECK(imports_count_spec(&imports, "require json") == 1);
        CHECK(imports_count_spec(&imports, "require a/b") == 1);
        CHECK(imports_count_spec(&imports, "require_relative store/types") == 1);
        CHECK(imports_count_spec(&imports, "require_relative ../lib/helper") == 1);
        /* the receiver form (Kernel.require) keeps the same shape */
        CHECK(imports_count_spec(&imports, "require set") == 1);
        /* multi-argument calls record NOTHING (never guessed) */
        CHECK(imports_count_spec(&imports, "require engine") == 0);
        /* interpolated strings record NOTHING */
        CHECK(imports_count_spec(&imports, "require #{dir}/dynamic") == 0);
        /* non-string arguments record NOTHING */
        CHECK(imports_count_spec(&imports, "require nonstring_arg") == 0);
        /* require inside an if_modifier is still a call */
        CHECK(imports_count_spec(&imports, "require x") == 1);
        /* require inside a method body is still a call */
        CHECK(imports_count_spec(&imports, "require_relative nested/thing") == 1);

        /* a require is a WHOLE-FILE claim: the binding name is NULL */
        CHECK(bind_name_for(&binds, "require json") == NULL);
        CHECK(bind_name_for(&binds, "require_relative store/types") == NULL);
        /* and every record pushes exactly one bind */
        CHECK(binds.n == imports.n);
        oc_imports_destroy(&imports);
        oc_surface_binds_destroy(&binds);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: resolver v1-ruby --------------------------------------------- */

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

static void unit_ruby_resolver(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    /* A gem layout: the lib load root, a root-level load root, and the
     * runner test tree. */
    oc_graph_add_file(&g, "proj/engine.rb");
    oc_graph_add_file(&g, "proj/lib/mycorp.rb");
    oc_graph_add_file(&g, "proj/lib/mycorp/version.rb");
    oc_graph_add_file(&g, "proj/lib/mycorp/cli.rb");
    oc_graph_add_file(&g, "proj/lib/mycorp/store.rb");
    oc_graph_add_file(&g, "proj/lib/mycorp/store/types.rb");
    oc_graph_add_file(&g, "proj/test/mycorp/store_test.rb");

    const char *roots[1] = {"proj"};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }

    /* require_relative: the requiring file's directory, .rb appended */
    CHECK_STR_EQ(
        oc_resolve_ruby_require(cx, "proj/lib/mycorp.rb", "require_relative mycorp/version", &a),
        "proj/lib/mycorp/version.rb");
    CHECK_STR_EQ(
        oc_resolve_ruby_require(cx, "proj/lib/mycorp/store.rb", "require_relative store/types", &a),
        "proj/lib/mycorp/store/types.rb");
    /* `..` traversal is legal Ruby; the join is anchored at the analysis
     * root so the lexical normalizer pops across directories */
    CHECK_STR_EQ(oc_resolve_ruby_require(cx, "proj/test/mycorp/store_test.rb",
                                         "require_relative ../../lib/mycorp", &a),
                 "proj/lib/mycorp.rb");
    /* one level up would land in proj/test/lib: an honest miss */
    CHECK(oc_resolve_ruby_require(cx, "proj/test/mycorp/store_test.rb",
                                  "require_relative ../lib/mycorp", &a) == NULL);
    /* traversal that leaves the analysis root resolves to NOTHING (the
     * probe can only hit the analyzed set) */
    CHECK(oc_resolve_ruby_require(cx, "proj/test/mycorp/store_test.rb",
                                  "require_relative ../../../../outside", &a) == NULL);

    /* bare require: the two documented load-root conventions, in order */
    CHECK_STR_EQ(oc_resolve_ruby_require(cx, "proj/lib/mycorp.rb", "require engine", &a),
                 "proj/engine.rb"); /* <root>/engine.rb: the root load root */
    CHECK_STR_EQ(oc_resolve_ruby_require(cx, "proj/lib/mycorp.rb", "require mycorp/store", &a),
                 "proj/lib/mycorp/store.rb"); /* <root>/lib/: the gem load root */
    /* misses stay unresolved -- no load path is guessed beyond the two */
    CHECK(oc_resolve_ruby_require(cx, "proj/lib/mycorp.rb", "require mycorp/missing", &a) == NULL);
    /* external names (json, set, rake) never resolve */
    CHECK(oc_resolve_ruby_require(cx, "proj/lib/mycorp.rb", "require json", &a) == NULL);

    /* never-guess guards */
    CHECK(oc_resolve_ruby_require(cx, "proj/lib/mycorp.rb", "require /abs/path", &a) == NULL);
    CHECK(oc_resolve_ruby_require(cx, "proj/lib/mycorp.rb", "require back\\slash", &a) == NULL);
    CHECK(oc_resolve_ruby_require(cx, "proj/lib/mycorp.rb", "require ", &a) == NULL);
    CHECK(oc_resolve_ruby_require(cx, "proj/lib/mycorp.rb", "wat", &a) == NULL);

    /* edges: set semantics (the same target named twice collapses) */
    oc_import_vec v;
    oc_imports_init(&v);
    CHECK(oc_imports_push(&v, &a, "proj/lib/mycorp.rb", "require_relative mycorp/version",
                          OC_LANG_RUBY));
    CHECK(oc_imports_push(&v, &a, "proj/lib/mycorp.rb", "require mycorp/store", OC_LANG_RUBY));
    CHECK(oc_imports_push(&v, &a, "proj/lib/mycorp.rb", "require_relative mycorp/version",
                          OC_LANG_RUBY)); /* duplicate target */
    CHECK(oc_imports_push(&v, &a, "proj/lib/mycorp/store.rb", "require_relative store/types",
                          OC_LANG_RUBY));
    CHECK(oc_imports_push(&v, &a, "proj/lib/mycorp/a.rb", "require mycorp/missing",
                          OC_LANG_RUBY)); /* unresolved */
    CHECK(oc_imports_resolve_ex(&g, &v, &a, roots, 1) == 3);
    CHECK(has_edge(&g, "proj/lib/mycorp.rb", "proj/lib/mycorp/version.rb"));
    CHECK(has_edge(&g, "proj/lib/mycorp.rb", "proj/lib/mycorp/store.rb"));
    CHECK(has_edge(&g, "proj/lib/mycorp/store.rb", "proj/lib/mycorp/store/types.rb"));
    oc_imports_destroy(&v);

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: exports, the shebang hint, the runner conventions ------------ */

static void unit_ruby_surface(void) {
    static const char src[] = "#!/usr/bin/env ruby\n"
                              "# frozen_string_literal: true\n"
                              "\n"
                              "require_relative 'helper'\n"
                              "\n"
                              "class Top\n"
                              "  class Nested\n"
                              "  end\n"
                              "end\n"
                              "\n"
                              "module Naming\n"
                              "  VERSION = '1.0'\n"
                              "end\n"
                              "\n"
                              "class Scoped::Deep < Top\n"
                              "end\n"
                              "\n"
                              "TOOL = :top\n"
                              "\n"
                              "class << self\n"
                              "  def meta; 1; end\n"
                              "end\n";

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
        parse_one(p, &g, "unit/surface.rb", src, &out);

        /* top-level classes/modules/constant assignments are candidates;
         * nested ones and singleton classes never are */
        CHECK(exports_count_named(&exports, "Top") == 1);
        CHECK(exports_count_named(&exports, "Naming") == 1);
        CHECK(exports_count_named(&exports, "Scoped::Deep") == 1); /* name as written */
        CHECK(exports_count_named(&exports, "TOOL") == 1);
        CHECK(exports_count_named(&exports, "Nested") == 0);
        CHECK(exports_count_named(&exports, "VERSION") == 0); /* inside the module */
        CHECK(exports_count_named(&exports, "helper") == 0);
        oc_surface_exports_destroy(&exports);

        /* the shebang hint: #! is a comment to the Ruby parser, so a #!
         * first line is ALWAYS a kernel shebang for Ruby */
        oc_parse_out out2;
        memset(&out2, 0, sizeof(out2));
        oc_hint_vec hints;
        oc_surface_hints_init(&hints);
        out2.hints = &hints;
        parse_one(p, &g, "unit/surface.rb", src, &out2);
        CHECK(hints.n == 1);
        if (hints.n == 1)
            CHECK(hints.items[0].has_shebang == 1);
        oc_surface_hints_destroy(&hints);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);

    /* the runner-collected NAME conventions (name-based only) */
    CHECK(oc_path_is_test_convention("test/store_test.rb") == 1);
    CHECK(oc_path_is_test_convention("test/test_store.rb") == 1);
    CHECK(oc_path_is_test_convention("spec/models/user_spec.rb") == 1);
    CHECK(oc_path_is_test_convention("test/support/helper.rb") == 0); /* no dir claiming */
    CHECK(oc_path_is_test_convention("lib/store.rb") == 0);
    CHECK(oc_path_is_test_convention("lib/probe.rb") == 0);
}

/* --- unit: the SimpleCov adapter ---------------------------------------- */

static void unit_simplecov(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_coverage cov;
    oc_coverage_init(&cov);
    oc_status st;
    oc_status_clear(&st);

    const char *good = OC_REPO_ROOT "/tests/fixtures_sprint16/rubycov/good.resultset.json";
    const char *paths[1] = {good};
    oc_code rc = oc_coverage_load(&cov, paths, 1, &a, &st);
    CHECK(rc == OC_OK);
    CHECK(cov.n_files == 2);
    /* suites UNION per file: user.rb anchors {1,2,3,6,7,8} (the null
     * entries are not-relevant and are NOT anchors), covered {1,2,3,6,7}
     * (the two 0 entries and the nulls are not covered) */
    size_t anchors = 0, covered = 0;
    CHECK(oc_coverage_lookup(&cov, "app/models/user.rb", 1, 8, &anchors, &covered));
    CHECK(anchors == 6);
    CHECK(covered == 5);
    /* a null-only span of a measured file: matched, zero anchors */
    CHECK(oc_coverage_lookup(&cov, "app/models/user.rb", 4, 5, &anchors, &covered) == 1);
    CHECK(anchors == 0);
    CHECK(covered == 0);
    /* billing.rb: anchors {2,3,4}, covered {2,3} (the trailing 0) */
    CHECK(oc_coverage_lookup(&cov, "app/services/billing.rb", 1, 4, &anchors, &covered));
    CHECK(anchors == 3);
    CHECK(covered == 2);
    oc_coverage_destroy(&cov);
    oc_arena_destroy(&a);

    /* malformed input fails LOUD (never silently partial) */
    {
        oc_arena a2;
        oc_arena_init(&a2);
        oc_coverage c2;
        oc_coverage_init(&c2);
        oc_status st2;
        const char *bad[1] = {OC_REPO_ROOT
                              "/tests/fixtures_sprint16/rubycov/bad_elem.resultset.json"};
        oc_status_clear(&st2);
        CHECK(oc_coverage_load(&c2, bad, 1, &a2, &st2) == OC_ERR_USAGE);
        oc_coverage_destroy(&c2);
        oc_arena_destroy(&a2);
    }
    {
        oc_arena a2;
        oc_arena_init(&a2);
        oc_coverage c2;
        oc_coverage_init(&c2);
        oc_status st2;
        const char *bad[1] = {OC_REPO_ROOT
                              "/tests/fixtures_sprint16/rubycov/no_lines.resultset.json"};
        oc_status_clear(&st2);
        CHECK(oc_coverage_load(&c2, bad, 1, &a2, &st2) == OC_ERR_USAGE);
        oc_coverage_destroy(&c2);
        oc_arena_destroy(&a2);
    }
    {
        /* a MIXED resultset dispatches on the object suite and then fails
         * LOUD on the suite that is not an object (never silently
         * partial) */
        oc_arena a2;
        oc_arena_init(&a2);
        oc_coverage c2;
        oc_coverage_init(&c2);
        oc_status st2;
        const char *bad[1] = {OC_REPO_ROOT
                              "/tests/fixtures_sprint16/rubycov/mixed_suite.resultset.json"};
        oc_status_clear(&st2);
        CHECK(oc_coverage_load(&c2, bad, 1, &a2, &st2) == OC_ERR_USAGE);
        oc_coverage_destroy(&c2);
        oc_arena_destroy(&a2);
    }
    /* the dispatch stays honest for the OTHER json shapes (istanbul still
     * loads: no coverage-suite wrapper) */
    {
        oc_arena a2;
        oc_arena_init(&a2);
        oc_coverage c2;
        oc_coverage_init(&c2);
        oc_status st2;
        oc_status_clear(&st2);
        const char *ist[1] = {"/tmp/oc_s16_ist_fixture.json"};
        CHECK(fixture_subst(OC_REPO_ROOT "/tests/fixtures/coverage/istanbul.json", ist[0]));
        CHECK(oc_coverage_load(&c2, ist, 1, &a2, &st2) == OC_OK);
        CHECK(c2.n_files > 0);
        oc_coverage_destroy(&c2);
        oc_arena_destroy(&a2);
    }
}

/* --- unit: the Gemfile / Gemfile.lock supply tier ------------------------ */

static void unit_gemfile_manifests(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_manifest_vec mv;
    const char *files[4] = {
        OC_REPO_ROOT "/tests/fixtures_sprint16/gemfile/Gemfile",
        OC_REPO_ROOT "/tests/fixtures_sprint16/gemfile_lock/Gemfile.lock",
        OC_REPO_ROOT "/tests/fixtures_sprint16/gemfile_lic/Gemfile",
        OC_REPO_ROOT "/tests/fixtures_sprint16/gemfile_lic/LICENSE",
    };
    CHECK(oc_supply_load_manifests(&mv, files, 4, &a, &st) == OC_OK);
    CHECK(mv.n == 3); /* LICENSE is not a manifest */
    const oc_manifest *gf = NULL;
    const oc_manifest *lock = NULL;
    const oc_manifest *lic = NULL;
    for (size_t i = 0; i < mv.n; i++) {
        if (mv.items[i].kind == OC_MANIFEST_GEMFILE && strstr(mv.items[i].path, "gemfile_lic"))
            lic = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_GEMFILE)
            gf = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_GEMFILE_LOCK)
            lock = &mv.items[i];
    }
    CHECK(gf && lock && lic);

    if (gf) {
        CHECK(gf->eco == OC_ECO_RUBYGEMS);
        /* 5 deps: pinned, exact, tilde, ranged, nospec; the path: link and
         * the commented-out gem record NOTHING */
        CHECK(gf->n_deps == 5);
        int saw_pinned = 0, saw_exact = 0, saw_tilde = 0, saw_ranged = 0, saw_nospec = 0;
        for (size_t i = 0; i < gf->n_deps; i++) {
            const oc_dep *d = &gf->deps[i];
            CHECK(!STR_EQ(d->name, "excluded")); /* path: in-repo link: not OC009 material */
            CHECK(!STR_EQ(d->name, "ghost"));    /* a comment: never a dependency */
            CHECK(d->line >= 8);                 /* real declaration lines (line-scanned) */
            if (strcmp(d->name, "pinned") == 0) {
                saw_pinned = 1;
                CHECK(d->pinned && STR_EQ(d->version, "1.2.3")); /* the bare version form */
            } else if (strcmp(d->name, "exact") == 0) {
                saw_exact = 1;
                CHECK(d->pinned && STR_EQ(d->version, "2.0.1")); /* "= x.y.z" stripped */
            } else if (strcmp(d->name, "tilde") == 0) {
                saw_tilde = 1;
                CHECK(!d->pinned && d->version == NULL); /* "~>" is NOT a pin */
            } else if (strcmp(d->name, "ranged") == 0) {
                saw_ranged = 1;
                CHECK(!d->pinned && d->version == NULL);
            } else if (strcmp(d->name, "nospec") == 0) {
                saw_nospec = 1;
                CHECK(!d->pinned && d->version == NULL); /* no spec at all: unpinned */
            }
        }
        CHECK(saw_pinned && saw_exact && saw_tilde && saw_ranged && saw_nospec);
        /* no licence file in the gemfile/ directory */
        CHECK(gf->license == NULL);
    }
    if (lock) {
        CHECK(lock->eco == OC_ECO_RUBYGEMS);
        /* 5 resolved pins (4-space GEM>specs): exact, pinned, rake, tilde,
         * json; the 6-space sub-entry (racc), the GIT gem (edgegem), the
         * PATH gem (excluded) and DEPENDENCIES record NOTHING */
        CHECK(lock->n_deps == 5);
        int saw_rake = 0, saw_pinned = 0, saw_json = 0;
        for (size_t i = 0; i < lock->n_deps; i++) {
            const oc_dep *d = &lock->deps[i];
            CHECK(d->pinned);                    /* the lockfile records RESOLVED pins */
            CHECK(!STR_EQ(d->name, "racc"));     /* a 6-space requested sub-entry */
            CHECK(!STR_EQ(d->name, "edgegem"));  /* GIT section */
            CHECK(!STR_EQ(d->name, "excluded")); /* PATH section */
            if (strcmp(d->name, "rake") == 0) {
                saw_rake = 1;
                CHECK_STR_EQ(d->version, "13.0.6");
            } else if (strcmp(d->name, "pinned") == 0) {
                saw_pinned = 1;
                CHECK_STR_EQ(d->version, "1.2.3");
            } else if (strcmp(d->name, "json") == 0) {
                saw_json = 1;
                CHECK_STR_EQ(d->version, "2.7.1");
            }
        }
        CHECK(saw_rake && saw_pinned && saw_json);
        CHECK(lock->license == NULL); /* no licence file in gemfile_lock/ */
    }
    if (lic) {
        /* the project-root licence probe (the go.mod discipline) */
        CHECK_STR_EQ(lic->license, "module-root file LICENSE");
    }
    oc_manifests_destroy(&mv);
    oc_arena_destroy(&a);
}

/* --- unit: OSV ecosystem rubygems --------------------------------------- */

static void unit_osv_rubygems(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_osv_snapshot snap;
    oc_osv_init(&snap);
    CHECK(oc_osv_load(&snap, OC_REPO_ROOT "/tests/fixtures_sprint16/osv_snapshot_ruby.json", &a,
                      &st) == OC_OK);
    CHECK_STR_EQ(snap.snapshot_date, "2026-11-22");
    CHECK(snap.n_records == 2);
    /* vulnerable range [0, 14.0.0): the locked 13.0.6 hits, 14.0.1 patched */
    CHECK(oc_osv_match(&snap, "rake", "13.0.6", OC_ECO_RUBYGEMS) == 1);
    CHECK(oc_osv_match(&snap, "rake", "14.0.1", OC_ECO_RUBYGEMS) == 0);
    /* introduced 99.0: nothing below matches */
    CHECK(oc_osv_match(&snap, "json", "2.7.1", OC_ECO_RUBYGEMS) == 0);
    /* a different ecosystem never matches a rubygems record */
    CHECK(oc_osv_match(&snap, "rake", "13.0.6", OC_ECO_CRATES) == 0);
    /* an unknown name never matches */
    CHECK(oc_osv_match(&snap, "leftpad", "0.1.0", OC_ECO_RUBYGEMS) == 0);
    oc_osv_destroy(&snap);
    oc_arena_destroy(&a);
}

/* --- unit: the fixtures end to end (the CLI) ---------------------------- */

static void unit_ruby_cli(void) {
    static const char root[] = OC_REPO_ROOT;
    char buf[65536];
    /* corpus A: the structural corpus end to end */
    run_oc_json(root, "tests/fixtures/ruby", "check", buf, sizeof(buf));
    CHECK(strstr(buf, "\"files_analyzed\":7") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    CHECK(strstr(buf, "\"ruby\"") != NULL); /* the pack loaded */
    {
        static const struct {
            const char *rule;
            size_t want;
        } rules[] = {
            {"OC001", 1}, {"OC002", 1}, {"OC004", 1}, {"OC006", 5}, {"OC008", 2}, {"OC011", 1},
        };
        for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
            char needle[32];
            snprintf(needle, sizeof(needle), "\"rule\":\"%s\"", rules[i].rule);
            size_t hits = 0;
            const char *q = buf;
            while ((q = strstr(q, needle)) != NULL) {
                hits++;
                q += strlen(needle);
            }
            if (hits != rules[i].want)
                fprintf(stderr, "  %s: got %zu want %zu\n", rules[i].rule, hits, rules[i].want);
            CHECK(hits == rules[i].want);
        }
    }
    /* OC003/OC005 stay silent (every require names an external gem; the
     * whole-file claim marks everything used) */
    CHECK(strstr(buf, "\"rule\":\"OC003\"") == NULL);
    CHECK(strstr(buf, "\"rule\":\"OC005\"") == NULL);
    /* OC007/OC009/OC010 stay silent without their inputs */
    CHECK(strstr(buf, "\"rule\":\"OC007\"") == NULL);
    CHECK(strstr(buf, "\"rule\":\"OC009\"") == NULL);
    CHECK(strstr(buf, "\"rule\":\"OC010\"") == NULL);

    /* corpus A graph: ZERO imports edges -- an edge always means "the
     * target is part of this analysis" */
    run_oc_json(root, "tests/fixtures/ruby", "graph", buf, sizeof(buf));
    {
        size_t hits = 0;
        const char *q = buf;
        while ((q = strstr(q, "\"kind\":\"imports\"")) != NULL) {
            hits++;
            q += 17;
        }
        CHECK(hits == 0);
    }

    /* corpus B: the resolver end to end */
    run_oc_json(root, "tests/fixtures/ruby_resolver", "check", buf, sizeof(buf));
    CHECK(strstr(buf, "\"files_analyzed\":11") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    {
        /* OC003 exactly once (the a/b require cycle) */
        const char *needle = "\"rule\":\"OC003\"";
        size_t hits = 0;
        const char *q = buf;
        while ((q = strstr(q, needle)) != NULL) {
            hits++;
            q += strlen(needle);
        }
        CHECK(hits == 1);
    }
    /* OC006 exactly twice, on the lonely and the orphan (the test file is
     * runner data; the bin script is claimed by the shebang) */
    CHECK(strstr(buf, "lonely.rb") != NULL);
    CHECK(strstr(buf, "orphan.rb") != NULL);
    {
        const char *needle = "\"rule\":\"OC006\"";
        size_t hits = 0;
        const char *q = buf;
        while ((q = strstr(q, needle)) != NULL) {
            hits++;
            q += strlen(needle);
        }
        CHECK(hits == 2);
    }
    /* the OC005 verdict set is EMPTY by construction (the whole-file
     * claim, the Go precedent) */
    CHECK(strstr(buf, "\"rule\":\"OC005\"") == NULL);
    /* OC010 twice: the Gemfile AND the Gemfile.lock (both declare no
     * licence and the corpus root has no licence file) */
    {
        const char *needle = "\"rule\":\"OC010\"";
        size_t hits = 0;
        const char *q = buf;
        while ((q = strstr(q, needle)) != NULL) {
            hits++;
            q += strlen(needle);
        }
        CHECK(hits == 2);
    }

    /* corpus B graph: EIGHT edges with the contracted endpoints */
    run_oc_json(root, "tests/fixtures/ruby_resolver", "graph", buf, sizeof(buf));
    {
        static const struct {
            const char *from;
            const char *to;
        } pairs[] = {
            {"lib/mycorp.rb", "lib/mycorp/version.rb"},
            {"lib/mycorp.rb", "lib/mycorp/cli.rb"},
            {"lib/mycorp.rb", "lib/mycorp/store.rb"},
            {"lib/mycorp/store.rb", "lib/mycorp/store/types.rb"},
            {"lib/mycorp/a.rb", "lib/mycorp/b.rb"},
            {"lib/mycorp/b.rb", "lib/mycorp/a.rb"},
            {"test/mycorp/store_test.rb", "lib/mycorp.rb"},
            {"test/mycorp/store_test.rb", "lib/mycorp/store.rb"},
        };
        size_t hits = 0;
        const char *q = buf;
        while ((q = strstr(q, "\"kind\":\"imports\"")) != NULL) {
            hits++;
            q += 17;
        }
        CHECK(hits == 8);
        for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
            char needle[256];
            snprintf(needle, sizeof(needle), "\"path\":\"tests/fixtures/ruby_resolver/%s\"",
                     pairs[i].from);
            CHECK(strstr(buf, needle) != NULL);
        }
        /* the decoy requires never resolve: no edge endpoint outside the
         * contracted set (json/set/missing stay unresolved) */
        CHECK(strstr(buf, "mycorp/missing") == NULL);
    }
}

int main(void) {
    unit_ruby_symbols();
    unit_ruby_decision_table();
    unit_ruby_imports();
    unit_ruby_resolver();
    unit_ruby_surface();
    unit_simplecov();
    unit_gemfile_manifests();
    unit_osv_rubygems();
    unit_ruby_cli();
    TEST_SUMMARY();
}
