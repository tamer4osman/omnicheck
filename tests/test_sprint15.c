/* Sprint 15 (the Rust language adoption): the contract of
 * docs/LANGUAGES.md (Rust section), pinned unit by unit.
 *
 * Symbols: function_item (free functions, impl methods, trait default
 * methods) + the item-level named closures (const/static whose value is
 * a closure_expression); bodyless signatures (function_signature_item:
 * trait signatures AND extern-block declarations) and macro_rules!
 * definitions are symbols with complexity exactly 1; local let-bound
 * closures are NEVER symbols (inclusive counting toward the enclosing
 * function). Complexity table: if/while/for/loop/match_arm/try + && ||
 * (the comp-10 hand-count pinned one decision per kind).
 * Imports: the use-tree expansion (single, groups, nested groups, glob,
 * alias) with the pre-alias leaf binding names, the mod-tree channel
 * (`mod name;`) with the #[path] guard, and resolver v1-rust (self/
 * crate/super anchors, the stem rule, the src/bin own-crate exception,
 * the item-reading pop ladder, external heads unresolved, set-semantics
 * edges). Surface: plain-pub exports (pub(crate) excluded), the fn main
 * parse fact, the cargo tests/ runner convention. Coverage: the LCOV
 * adapter (DA anchors, covered = count > 0, loud malformed input).
 * Manifests: Cargo.toml exact-= pins + path-dep skip + workspace
 * inheritance + the layered OC010 licence, Cargo.lock resolved pins,
 * OSV ecosystem crates.io.
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
#define STR_NE(a, b) (!((a) == NULL && (b) == NULL) && (!(STR_EQ(a, b))))

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the built oc binary"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif

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
    in.grammar = "rust";
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_parser_parse_file(p, &in, g, out, &st) == OC_OK);
    CHECK(out->diag.had_error == 0);
}

/* --- unit: symbols, spans, complexity (the decision table) ------------- */

static void unit_rust_symbols(void) {
    static const char src[] =
        "use crate::shapes::helper_mod;\n"
        "use serde::external;\n"
        "\n"
        "pub mod helper_mod;\n"
        "\n"
        "/// Doc comment on the promoted const closure.\n"
        "const HOP: fn(i64) -> i64 = |x| if x > 0 { x } else { -x };\n"
        "\n"
        "static LEAP: fn(i64) -> i64 = |x| match x {\n"
        "    0 => 1,\n"
        "    _ => 2,\n"
        "};\n"
        "\n"
        "static PLAIN: i64 = 7;\n"
        "\n"
        "pub trait Swims {\n"
        "    fn required(&self) -> i64;\n"
        "    fn provided(&self) -> i64 {\n"
        "        self.required() + 1\n"
        "    }\n"
        "}\n"
        "\n"
        "extern \"C\" {\n"
        "    fn foreign(a: i64) -> i64;\n"
        "}\n"
        "\n"
        "macro_rules! shard {\n"
        "    ($x:expr) => {\n"
        "        $x\n"
        "    };\n"
        "}\n"
        "\n"
        "impl Swims for i64 {\n"
        "    fn required(&self) -> i64 {\n"
        "        // one of each decision kind + both operator tokens:\n"
        "        let v = *self;\n"
        "        if v > 0 && v < 100 {\n"
        "            let mut i = 0;\n"
        "            while i < v || i == 0 {\n"
        "                i += 1;\n"
        "            }\n"
        "            for _ in 0..i {\n"
        "                let _q: Result<i64, String> = Ok(1);\n"
        "                let _r = _q?;\n"
        "            }\n"
        "            loop {\n"
        "                break;\n"
        "            }\n"
        "        }\n"
        "        match v {\n"
        "            1 => 10,\n"
        "            _ => 20,\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
        "pub fn alpha() -> i64 {\n"
        "    // a LOCAL closure: decisions belong to THIS function (inclusive);\n"
        "    // the binding is never a symbol.\n"
        "    let f = |u: i64| if u > 0 { 1 } else { 2 };\n"
        "    f(3)\n"
        "}\n"
        "\n"
        "pub(crate) fn crate_only() -> i64 {\n"
        "    1\n"
        "}\n"
        "\n"
        "fn fully_private() -> i64 {\n"
        "    2\n"
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
        parse_one(p, &g, "unit/shapes.rs", src, &out);

        /* the named defining constructs are symbols */
        CHECK(count_symbols_named(&g, "alpha") == 1);
        CHECK(count_symbols_named(&g, "required") == 2); /* trait sig + impl method */
        CHECK(count_symbols_named(&g, "provided") == 1); /* trait default method */
        CHECK(count_symbols_named(&g, "foreign") == 1);  /* extern block signature */
        CHECK(count_symbols_named(&g, "shard") == 1);    /* macro_rules! */
        CHECK(count_symbols_named(&g, "crate_only") == 1);
        CHECK(count_symbols_named(&g, "fully_private") == 1);
        /* the item-level named closures promote */
        CHECK(count_symbols_named(&g, "HOP") == 1);
        CHECK(count_symbols_named(&g, "LEAP") == 1);
        /* a const/static whose value is not a closure is NOT a symbol */
        CHECK(count_symbols_named(&g, "PLAIN") == 0);
        /* a local let-bound closure binding is NEVER a symbol */
        CHECK(count_symbols_named(&g, "f") == 0);
        CHECK(count_symbols_named(&g, "u") == 0);
        CHECK(count_symbols_named(&g, "v") == 0);
        CHECK(count_symbols_named(&g, "i") == 0);

        /* complexity: the decision table hand-count */
        const oc_node *s = symbol_named(&g, "HOP");
        CHECK(s && s->complexity == 2); /* 1 + 1 if */
        s = symbol_named(&g, "LEAP");
        CHECK(s && s->complexity == 3);   /* 1 + 2 match arms */
        s = symbol_named(&g, "required"); /* the impl method (first match:
                                           * the trait signature sorts by
                                           * decl order -- either order, one
                                           * has comp 10 and one comp 1) */
        {
            size_t comps[2] = {0, 0};
            size_t n = 0;
            for (size_t i = 0; i < g.n_nodes; i++) {
                if (g.nodes[i].kind == OC_NODE_FUNCTION &&
                    strcmp(g.nodes[i].name, "required") == 0 && n < 2)
                    comps[n++] = g.nodes[i].complexity;
            }
            CHECK(n == 2);
            CHECK((comps[0] == 10 && comps[1] == 1) || (comps[0] == 1 && comps[1] == 10));
        }
        s = symbol_named(&g, "provided");
        CHECK(s && s->complexity == 1);
        s = symbol_named(&g, "foreign");
        CHECK(s && s->complexity == 1);
        s = symbol_named(&g, "shard");
        CHECK(s && s->complexity == 1); /* token trees: no decision parses */
        s = symbol_named(&g, "alpha");
        CHECK(s && s->complexity == 2); /* the local closure's if counts INWARD */
        s = symbol_named(&g, "crate_only");
        CHECK(s && s->complexity == 1);
        s = symbol_named(&g, "fully_private");
        CHECK(s && s->complexity == 1);

        /* spans: the promoted closure spans the WHOLE const/static item
         * (definition extent, OBS-2); the method spans the whole fn */
        s = symbol_named(&g, "HOP");
        CHECK(s && s->def_end_line >= s->def_start_line);
        s = symbol_named(&g, "alpha");
        CHECK(s && s->def_end_line - s->def_start_line >= 2);

        /* comments: doc comments are plain line_comment text (the doc
         * marker child is INSIDE the span) */
        oc_node_id file_id = OC_NODE_INVALID;
        for (size_t i = 0; i < g.n_nodes; i++) {
            if (g.nodes[i].kind == OC_NODE_FILE && strcmp(g.nodes[i].path, "unit/shapes.rs") == 0)
                file_id = (oc_node_id)i;
        }
        CHECK(file_id != OC_NODE_INVALID);
        oc_doc *doc = oc_graph_doc(&g, file_id);
        size_t n_comments = 0;
        int saw_doc = 0;
        if (doc) {
            for (size_t i = 0; i < doc->n_comments; i++) {
                n_comments++;
                if (strstr(doc->comments[i].text, "/// Doc comment on the promoted"))
                    saw_doc = 1;
            }
        }
        CHECK(n_comments >= 3);
        CHECK(saw_doc);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: import channels (use-tree expansion + mod records) ---------- */

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

static void unit_rust_imports(void) {
    static const char src[] = "use crate::shapes::helper_mod;\n"
                              "use crate::shapes::{alpha, beta::gamma};\n"
                              "use std::collections::{HashMap, HashSet};\n"
                              "use ext::path::{inner1, inner2::*};\n"
                              "use crate::globbed::*;\n"
                              "use crate::aliased::thing as renamed;\n"
                              "use serde::external;\n"
                              "pub use crate::reexported::Item;\n"
                              "\n"
                              "pub mod helper_mod;\n"
                              "mod plain_mod;\n"
                              "#[cfg(test)]\n"
                              "mod cfg_gated;\n"
                              "#[path = \"moved_elsewhere.rs\"]\n"
                              "mod attr_moved;\n";

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
        parse_one(p, &g, "unit/uses.rs", src, &out);

        /* single path + head kept verbatim */
        CHECK(imports_count_spec(&imports, "crate::shapes::helper_mod") == 1);
        /* brace groups expand to leaves with the prefix joined */
        CHECK(imports_count_spec(&imports, "crate::shapes::alpha") == 1);
        CHECK(imports_count_spec(&imports, "crate::shapes::beta::gamma") == 1);
        /* nested groups + globs */
        CHECK(imports_count_spec(&imports, "ext::path::inner1") == 1);
        CHECK(imports_count_spec(&imports, "ext::path::inner2::*") == 1);
        CHECK(imports_count_spec(&imports, "crate::globbed::*") == 1);
        /* aliases record the PRE-ALIAS path (the alias is a local rename) */
        CHECK(imports_count_spec(&imports, "crate::aliased::thing") == 1);
        CHECK(imports_count_spec(&imports, "crate::aliased::thing as renamed") == 0);
        /* external heads keep their own head text */
        CHECK(imports_count_spec(&imports, "serde::external") == 1);
        CHECK(imports_count_spec(&imports, "std::collections::HashMap") == 1);
        CHECK(imports_count_spec(&imports, "std::collections::HashSet") == 1);
        /* pub use records the same channel */
        CHECK(imports_count_spec(&imports, "crate::reexported::Item") == 1);
        /* the mod-tree channel records `mod <name>` */
        CHECK(imports_count_spec(&imports, "mod helper_mod") == 1);
        CHECK(imports_count_spec(&imports, "mod plain_mod") == 1);
        CHECK(imports_count_spec(&imports, "mod cfg_gated") == 1);
        /* a #[path = ...]-attributed mod records NOTHING (the attribute
         * moved the file; probing the conventional name would fabricate
         * a WRONG edge) */
        CHECK(imports_count_spec(&imports, "mod attr_moved") == 0);

        /* bindings: the pre-alias LEAF segment; globs bind NULL */
        {
            const char *bn = bind_name_for(&binds, "crate::shapes::helper_mod");
            CHECK_STR_EQ(bn, "helper_mod");
            bn = bind_name_for(&binds, "crate::shapes::beta::gamma");
            CHECK_STR_EQ(bn, "gamma");
            bn = bind_name_for(&binds, "crate::aliased::thing");
            CHECK_STR_EQ(bn, "thing"); /* marking uses the target's own name */
            CHECK(bind_name_for(&binds, "crate::globbed::*") == NULL);
            CHECK(bind_name_for(&binds, "ext::path::inner2::*") == NULL);
            /* mod records push NO binding: declaring is not using */
            CHECK(bind_name_for(&binds, "mod helper_mod") == NULL);
            CHECK(bind_name_for(&binds, "mod plain_mod") == NULL);
        }
        oc_imports_destroy(&imports);
        oc_surface_binds_destroy(&binds);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: resolver v1-rust (anchors, stem rule, pop ladder, guards) --- */

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

static void unit_rust_resolver(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    /* A cargo layout: the lib crate with nested modules, a bin target
     * (its OWN crate) and an integration test directory. */
    oc_graph_add_file(&g, "proj/src/lib.rs");
    oc_graph_add_file(&g, "proj/src/a.rs");
    oc_graph_add_file(&g, "proj/src/b.rs");
    oc_graph_add_file(&g, "proj/src/store/mod.rs");
    oc_graph_add_file(&g, "proj/src/store/types.rs");
    oc_graph_add_file(&g, "proj/src/store/types/widget.rs");
    oc_graph_add_file(&g, "proj/src/util.rs");
    oc_graph_add_file(&g, "proj/src/util/helper.rs");
    oc_graph_add_file(&g, "proj/src/util/notes.rs");
    oc_graph_add_file(&g, "proj/src/bin/tool.rs");
    oc_graph_add_file(&g, "proj/src/bin/store/types.rs");
    oc_graph_add_file(&g, "proj/tests/intg.rs");
    oc_graph_add_file(&g, "proj/tests/common/mod.rs");

    const char *roots[1] = {"proj"};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }

    /* crate:: anchors at src/ (the nearest src ancestor) */
    CHECK_STR_EQ(oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "crate::a::A", &a),
                 "proj/src/a.rs");
    /* the ITEM-READING POP LADDER: the full module probe misses, the
     * parent module file wins */
    CHECK_STR_EQ(oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "crate::store::types::Widget", &a),
                 "proj/src/store/types.rs");
    /* the ladder pops FURTHER than one segment when needed: X would be
     * an ITEM inside the widget module, whose module file exists */
    CHECK_STR_EQ(
        oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "crate::store::types::widget::X", &a),
        "proj/src/store/types/widget.rs");
    /* the deepest module file the path names wins */
    CHECK_STR_EQ(oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "crate::store::types::widget", &a),
                 "proj/src/store/types/widget.rs");
    /* the mod.rs spelling resolves through the same pair probe */
    CHECK_STR_EQ(oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "crate::store::store_root", &a),
                 "proj/src/store/mod.rs");
    /* self:: anchors at dirname/stem (the stem rule) */
    CHECK_STR_EQ(oc_resolve_rust_use_path(cx, "proj/src/util.rs", "self::helper::fmt_inner", &a),
                 "proj/src/util/helper.rs");
    /* self:: from a mod.rs anchors at the mod.rs's own directory */
    CHECK_STR_EQ(oc_resolve_rust_use_path(cx, "proj/src/store/mod.rs", "self::types::Widget", &a),
                 "proj/src/store/types.rs");
    /* super:: from a mod.rs climbs one module level */
    CHECK_STR_EQ(oc_resolve_rust_use_path(cx, "proj/src/store/mod.rs", "super::a::A", &a),
                 "proj/src/a.rs");
    /* super:: from a stem-anchored child reaches the parent module dir
     * (the parent module of helper IS util -- src/util/notes.rs is the
     * module-file spelling of the util child `notes`) */
    CHECK_STR_EQ(oc_resolve_rust_use_path(cx, "proj/src/util/helper.rs", "super::notes::Z", &a),
                 "proj/src/util/notes.rs");
    /* an honest miss inside the parent module: util/other.rs does not exist */
    CHECK(oc_resolve_rust_use_path(cx, "proj/src/util/helper.rs", "super::other::Y", &a) == NULL);
    /* super::b::B names util::b, NOT the crate's b -- never guessed */
    CHECK(oc_resolve_rust_use_path(cx, "proj/src/util/helper.rs", "super::b::B", &a) == NULL);
    /* super:: from the crate root has no parent module */
    CHECK(oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "super::a::A", &a) == NULL);
    /* src/bin/<name>.rs are their OWN crates: crate:: anchors at src/bin/,
     * so the lib crate's store module is unreachable (never guessed) */
    CHECK_STR_EQ(
        oc_resolve_rust_use_path(cx, "proj/src/bin/tool.rs", "crate::store::types::Widget", &a),
        "proj/src/bin/store/types.rs");
    CHECK(oc_resolve_rust_use_path(cx, "proj/src/bin/tool.rs", "crate::a::A", &a) == NULL);
    /* external crate heads stay unresolved ALWAYS (no manifest read) */
    CHECK(oc_resolve_rust_use_path(cx, "proj/tests/intg.rs", "mycorp::store::types::Widget", &a) ==
          NULL);
    CHECK(oc_resolve_rust_use_path(cx, "proj/src/util/helper.rs", "serde::fix::Thing", &a) == NULL);
    CHECK(oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "std::collections::HashMap", &a) == NULL);
    /* guards: malformed and escape forms */
    CHECK(oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "crate::", &a) == NULL);
    CHECK(oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "crate::a::..::b", &a) == NULL);
    CHECK(oc_resolve_rust_use_path(cx, "proj/src/lib.rs", "crate::a/b::c", &a) == NULL);

    /* the mod-tree channel: the stem rule + the mod.rs spelling */
    CHECK_STR_EQ(oc_resolve_rust_mod_decl(cx, "proj/src/lib.rs", "mod a", &a), "proj/src/a.rs");
    CHECK_STR_EQ(oc_resolve_rust_mod_decl(cx, "proj/src/store/mod.rs", "mod types", &a),
                 "proj/src/store/types.rs");
    CHECK_STR_EQ(oc_resolve_rust_mod_decl(cx, "proj/src/util.rs", "mod helper", &a),
                 "proj/src/util/helper.rs");
    CHECK(oc_resolve_rust_mod_decl(cx, "proj/src/lib.rs", "mod missing", &a) == NULL);
    CHECK(oc_resolve_rust_mod_decl(cx, "proj/src/lib.rs", "mod ../escape", &a) == NULL);

    /* edges: one per resolved record, SET semantics (the mod record and
     * the use leaf naming the same target collapse into ONE edge)
     * lib->a (mod + use), a->b, b->a = 3 edges; the malformed record
     * ("pub reexport") resolves nothing */
    oc_import_vec v;
    oc_imports_init(&v);
    CHECK(oc_imports_push(&v, &a, "proj/src/lib.rs", "mod a", OC_LANG_RUST));
    CHECK(oc_imports_push(&v, &a, "proj/src/lib.rs", "crate::a::A", OC_LANG_RUST));
    CHECK(oc_imports_push(&v, &a, "proj/src/a.rs", "crate::b::B", OC_LANG_RUST));
    CHECK(oc_imports_push(&v, &a, "proj/src/b.rs", "crate::a::A", OC_LANG_RUST));
    CHECK(oc_imports_push(&v, &a, "proj/src/store/mod.rs", "pub reexport", OC_LANG_RUST));
    CHECK(oc_imports_resolve_ex(&g, &v, &a, roots, 1) == 3);
    CHECK(has_edge(&g, "proj/src/lib.rs", "proj/src/a.rs"));
    CHECK(has_edge(&g, "proj/src/a.rs", "proj/src/b.rs"));
    CHECK(has_edge(&g, "proj/src/b.rs", "proj/src/a.rs"));
    /* the malformed record ("pub reexport") resolves nothing */
    CHECK(!has_edge(&g, "proj/src/store/mod.rs", "proj/src/store/mod.rs"));
    oc_imports_destroy(&v);

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: exports, the main fact, restricted visibility --------------- */

static void unit_rust_surface(void) {
    static const char src[] = "pub fn exported() -> i64 {\n"
                              "    1\n"
                              "}\n"
                              "\n"
                              "fn private() -> i64 {\n"
                              "    2\n"
                              "}\n"
                              "\n"
                              "pub(crate) fn crate_only() -> i64 {\n"
                              "    3\n"
                              "}\n"
                              "\n"
                              "pub(super) fn super_only() -> i64 {\n"
                              "    4\n"
                              "}\n"
                              "\n"
                              "pub struct Recorded {\n"
                              "    pub field: i64,\n"
                              "}\n"
                              "\n"
                              "pub enum Choices {\n"
                              "    Yes,\n"
                              "}\n"
                              "\n"
                              "pub type Alias = i64;\n"
                              "\n"
                              "pub const LIMIT: i64 = 9;\n"
                              "\n"
                              "pub static STATIC_LIMIT: i64 = 10;\n"
                              "\n"
                              "pub trait Feats {\n"
                              "    fn go(&self);\n"
                              "}\n"
                              "\n"
                              "pub mod inline {\n"
                              "    pub fn nested() -> i64 {\n"
                              "        5\n"
                              "    }\n"
                              "}\n"
                              "\n"
                              "impl Recorded {\n"
                              "    pub fn method(&self) -> i64 {\n"
                              "        self.field\n"
                              "    }\n"
                              "}\n"
                              "\n"
                              "fn main() {\n"
                              "    let _ = exported();\n"
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
        oc_hint_vec hints;
        oc_surface_hints_init(&hints);
        out.exports = &exports;
        out.hints = &hints;
        parse_one(p, &g, "src/lib.rs", src, &out);

        /* exports: exactly the plain-pub top-level items */
        size_t n_exports = exports.n;
        int saw[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (size_t i = 0; i < exports.n; i++) {
            const char *nm = exports.items[i].name;
            if (strcmp(nm, "exported") == 0)
                saw[0] = 1;
            else if (strcmp(nm, "Recorded") == 0)
                saw[1] = 1;
            else if (strcmp(nm, "Choices") == 0)
                saw[2] = 1;
            else if (strcmp(nm, "Alias") == 0)
                saw[3] = 1;
            else if (strcmp(nm, "LIMIT") == 0)
                saw[4] = 1;
            else if (strcmp(nm, "STATIC_LIMIT") == 0)
                saw[5] = 1;
            else if (strcmp(nm, "Feats") == 0)
                saw[6] = 1;
            else if (strcmp(nm, "inline") == 0)
                saw[7] = 1;
        }
        CHECK(n_exports == 8);
        for (int i = 0; i < 8; i++)
            CHECK(saw[i]);
        /* NOT surface: private fn, restricted-visibility fns, the impl
         * method (the Go method rule), the nested item */
        for (size_t i = 0; i < exports.n; i++) {
            const char *nm = exports.items[i].name;
            CHECK(strcmp(nm, "private") != 0);
            CHECK(strcmp(nm, "crate_only") != 0);
            CHECK(strcmp(nm, "super_only") != 0);
            CHECK(strcmp(nm, "method") != 0);
            CHECK(strcmp(nm, "nested") != 0);
        }

        /* the main fact: the top-level fn main claims the file */
        CHECK(hints.n == 1);
        CHECK(hints.items[0].has_main == 1);
        oc_surface_exports_destroy(&exports);
        oc_surface_hints_destroy(&hints);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the fn main fact does NOT fire on nested/impl methods ------- */

static void unit_rust_main_fact_scoped(void) {
    static const char src[] = "mod inner {\n"
                              "    pub fn main() -> i64 {\n"
                              "        1\n"
                              "    }\n"
                              "}\n"
                              "\n"
                              "struct S;\n"
                              "\n"
                              "impl S {\n"
                              "    fn main(&self) -> i64 {\n"
                              "        2\n"
                              "    }\n"
                              "}\n"
                              "\n"
                              "fn runner() -> i64 {\n"
                              "    3\n"
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
        oc_hint_vec hints;
        oc_surface_hints_init(&hints);
        out.hints = &hints;
        parse_one(p, &g, "src/lib.rs", src, &out);
        /* no TOP-LEVEL fn main: nothing claims the file */
        CHECK(hints.n == 0);
        oc_surface_hints_destroy(&hints);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the LCOV adapter -------------------------------------------- */

static void unit_lcov(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_coverage cov;
    oc_coverage_init(&cov);
    oc_status st;
    oc_status_clear(&st);

    const char *good = OC_REPO_ROOT "/tests/fixtures_sprint15/rustcov/good.lcov";
    const char *paths[1] = {good};
    oc_code rc = oc_coverage_load(&cov, paths, 1, &a, &st);
    CHECK(rc == OC_OK);
    CHECK(cov.n_files == 2);
    CHECK(cov.files_loaded == 2);
    /* src/lib.rs: DA anchors {3,4,5}, covered {3,4} (the BRDA line is
     * already a DA anchor; FN/FNDA/LF/LH are consumed, never anchors) */
    size_t anchors = 0, covered = 0;
    CHECK(oc_coverage_lookup(&cov, "somecrate/src/lib.rs", 1, 100, &anchors, &covered));
    CHECK(anchors == 3);
    CHECK(covered == 2);
    CHECK(oc_coverage_lookup(&cov, "somecrate/src/lib.rs", 3, 4, &anchors, &covered));
    CHECK(anchors == 2);
    CHECK(covered == 2);
    CHECK(oc_coverage_lookup(&cov, "somecrate/src/lib.rs", 5, 5, &anchors, &covered));
    CHECK(anchors == 1);
    CHECK(covered == 0);
    /* other/util.rs: DA {1}, covered {1} */
    CHECK(oc_coverage_lookup(&cov, "deep/other/util.rs", 1, 1, &anchors, &covered));
    CHECK(anchors == 1);
    CHECK(covered == 1);
    /* an unmeasured span of a COVERED file: matched, zero anchors */
    CHECK(oc_coverage_lookup(&cov, "somecrate/src/lib.rs", 7, 9, &anchors, &covered) == 1);
    CHECK(anchors == 0);
    CHECK(covered == 0);
    oc_coverage_destroy(&cov);
    oc_arena_destroy(&a);

    /* malformed input fails LOUD (never silently partial) */
    {
        oc_arena a2;
        oc_arena_init(&a2);
        oc_coverage c2;
        oc_coverage_init(&c2);
        oc_status st2;
        const char *bad[1] = {OC_REPO_ROOT "/tests/fixtures_sprint15/rustcov/bad_line.lcov"};
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
        const char *bad[1] = {OC_REPO_ROOT "/tests/fixtures_sprint15/rustcov/bad_order.lcov"};
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
        const char *bad[1] = {OC_REPO_ROOT "/tests/fixtures_sprint15/rustcov/nosf.lcov"};
        oc_status_clear(&st2);
        CHECK(oc_coverage_load(&c2, bad, 1, &a2, &st2) == OC_ERR_USAGE);
        oc_coverage_destroy(&c2);
        oc_arena_destroy(&a2);
    }
}

/* --- unit: the Cargo manifest tier -------------------------------------- */

static void unit_cargo_manifests(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_manifest_vec mv;
    const char *files[4] = {
        OC_REPO_ROOT "/tests/fixtures_sprint15/cargo/Cargo.toml",
        OC_REPO_ROOT "/tests/fixtures_sprint15/cargo/Cargo.lock",
        OC_REPO_ROOT "/tests/fixtures_sprint15/cargo_nolic/Cargo.toml",
        OC_REPO_ROOT "/tests/fixtures_sprint15/cargo_nolic/NOTICE",
    };
    CHECK(oc_supply_load_manifests(&mv, files, 4, &a, &st) == OC_OK);
    CHECK(mv.n == 3); /* NOTICE is not a manifest */
    const oc_manifest *toml = NULL;
    const oc_manifest *lock = NULL;
    const oc_manifest *nolic = NULL;
    for (size_t i = 0; i < mv.n; i++) {
        if (mv.items[i].kind == OC_MANIFEST_CARGO_TOML && strstr(mv.items[i].path, "cargo_nolic"))
            nolic = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_CARGO_TOML)
            toml = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_CARGO_LOCK)
            lock = &mv.items[i];
    }
    CHECK(toml && lock && nolic);

    if (toml) {
        CHECK(toml->eco == OC_ECO_CRATES);
        CHECK_STR_EQ(toml->license, "MIT OR Apache-2.0");
        /* 13 deps: exact, caret, tilde, star, range, inlined,
         * inlined_unpinned, gitdep, nixy, devpin, devcaret, buildpin,
         * shared; pathdep (in-repo link) and inherited (the workspace
         * table does not name it) record NOTHING. */
        CHECK(toml->n_deps == 13);
        size_t pins = 0;
        int saw_exact = 0, saw_caret = 0, saw_nixy = 0, saw_shared = 0, saw_gitdep = 0;
        for (size_t i = 0; i < toml->n_deps; i++) {
            const oc_dep *d = &toml->deps[i];
            CHECK(!STR_EQ(d->name, "pathdep"));   /* in-repo link: not OC009 material */
            CHECK(!STR_EQ(d->name, "inherited")); /* unresolvable inheritance: nothing */
            CHECK(d->line >= 5);                  /* real declaration lines (line-scanned) */
            if (d->pinned)
                pins++;
            if (strcmp(d->name, "exact") == 0) {
                saw_exact = 1;
                CHECK(d->pinned && STR_EQ(d->version, "1.2.3")); /* the "=" stripped */
            } else if (strcmp(d->name, "caret") == 0) {
                saw_caret = 1;
                CHECK(!d->pinned && d->version == NULL);
            } else if (strcmp(d->name, "nixy") == 0) {
                saw_nixy = 1;
                CHECK(d->pinned && STR_EQ(d->version, "0.27.1")); /* target.'cfg'.deps read */
            } else if (strcmp(d->name, "shared") == 0) {
                saw_shared = 1;
                CHECK(!d->pinned); /* workspace.dependencies entries are unpinned here */
            } else if (strcmp(d->name, "gitdep") == 0) {
                saw_gitdep = 1;
                CHECK(!d->pinned); /* git without a version: unpinned (warn) */
            }
        }
        CHECK(pins == 5); /* exact, inlined, nixy, devpin, buildpin */
        CHECK(saw_exact && saw_caret && saw_nixy && saw_shared && saw_gitdep);
    }
    if (lock) {
        CHECK(lock->eco == OC_ECO_CRATES);
        CHECK(lock->n_deps == 2); /* the workspace member "sample" has no source: skipped */
        int saw_libc = 0, saw_serde = 0;
        for (size_t i = 0; i < lock->n_deps; i++) {
            const oc_dep *d = &lock->deps[i];
            CHECK(d->pinned); /* the lockfile records RESOLVED pins */
            if (strcmp(d->name, "libc") == 0) {
                saw_libc = 1;
                CHECK_STR_EQ(d->version, "0.2.100");
            } else if (strcmp(d->name, "serde") == 0) {
                saw_serde = 1;
                CHECK_STR_EQ(d->version, "1.0.210");
            }
            CHECK(!STR_EQ(d->name, "version")); /* top-level version = 4 is not a dep */
            CHECK(!STR_EQ(d->name, "sample"));  /* the workspace member: no source, skipped */
        }
        CHECK(saw_libc && saw_serde);
    }
    if (nolic) {
        /* OC010 layering: license-file names a file that does NOT exist
         * among the discovered files -> the project-root convention
         * finds NOTICE (nope.txt is not a licence basename) */
        CHECK_STR_EQ(nolic->license, "project-root file NOTICE");
        CHECK(nolic->n_deps == 1);
        CHECK(nolic->deps[0].pinned && STR_EQ(nolic->deps[0].version, "1.0.0"));
    }
    oc_manifests_destroy(&mv);
    oc_arena_destroy(&a);
}

/* --- unit: OSV ecosystem crates.io -------------------------------------- */

static void unit_osv_crates(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_osv_snapshot snap;
    oc_osv_init(&snap);
    CHECK(oc_osv_load(&snap, OC_REPO_ROOT "/tests/fixtures/supply/osv_snapshot_rust.json", &a,
                      &st) == OC_OK);
    CHECK_STR_EQ(snap.snapshot_date, "2026-11-21");
    CHECK(snap.n_records == 2);
    /* vulnerable range [0, 0.2.99): 0.2.98 hits, the patched 0.2.100 does not */
    CHECK(oc_osv_match(&snap, "libc", "0.2.98", OC_ECO_CRATES) == 1);
    CHECK(oc_osv_match(&snap, "libc", "0.2.100", OC_ECO_CRATES) == 0);
    /* introduced 1.0.0, fixed 1.0.205: the locked 1.0.210 is patched */
    CHECK(oc_osv_match(&snap, "serde", "1.0.204", OC_ECO_CRATES) == 1);
    CHECK(oc_osv_match(&snap, "serde", "1.0.210", OC_ECO_CRATES) == 0);
    /* a different ecosystem never matches a crates.io record */
    CHECK(oc_osv_match(&snap, "libc", "0.2.98", OC_ECO_NPM) == 0);
    /* an unknown name never matches */
    CHECK(oc_osv_match(&snap, "leftpad", "0.1.0", OC_ECO_CRATES) == 0);
    oc_osv_destroy(&snap);
    oc_arena_destroy(&a);
}

/* --- unit: the shebang hint vs Rust inner attributes ------------------- */

static void unit_rust_inner_attribute_not_shebang(void) {
    static const char src[] = "#![allow(dead_code)]\n"
                              "#![cfg_attr(feature = \"x\", allow(unused))]\n"
                              "\n"
                              "pub fn lonely() -> i64 {\n"
                              "    1\n"
                              "}\n";
    static const char script[] = "#!/usr/bin/env rust-script\n"
                                 "fn main() {\n"
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
        /* the inner attribute is NOT a shebang: no entry hint */
        {
            oc_parse_out out;
            memset(&out, 0, sizeof(out));
            oc_hint_vec hints;
            oc_surface_hints_init(&hints);
            out.hints = &hints;
            parse_one(p, &g, "src/config.rs", src, &out);
            CHECK(hints.n == 0);
            oc_surface_hints_destroy(&hints);
        }
        /* a REAL shebang line still claims the file */
        {
            oc_parse_out out;
            memset(&out, 0, sizeof(out));
            oc_hint_vec hints;
            oc_surface_hints_init(&hints);
            out.hints = &hints;
            parse_one(p, &g, "script.rs", script, &out);
            CHECK(hints.n == 1);
            CHECK(hints.items[0].has_shebang == 1);
            oc_surface_hints_destroy(&hints);
        }
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: the cargo tests/ runner convention (language-blind) ---------- */

static void unit_rust_test_convention(void) {
    /* The convention lives in surface.c's language-blind matcher; the
     * observable consequence is OC006 silence for tests/<name>.rs. The CLI
     * run below (unit_rust_cli) pins it end to end; here the pure
     * classification is pinned through the surface entry API. */
    CHECK(oc_path_is_test_convention("tests/intg.rs"));
    CHECK(oc_path_is_test_convention("crate/tests/roundtrip.rs"));
    /* NOT runner data: support modules, src trees, other languages */
    CHECK(!oc_path_is_test_convention("tests/common/mod.rs"));
    CHECK(!oc_path_is_test_convention("src/main.rs"));
    CHECK(!oc_path_is_test_convention("tests/x.js"));
    CHECK(!oc_path_is_test_convention("src/tests_helper.rs"));
}

/* --- CLI: the resolver corpus end to end -------------------------------- */

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

static void unit_rust_cli(void) {
    static const char root[] = OC_REPO_ROOT;
    char buf[65536];
    /* corpus B: the resolver end to end (the CLI JSON is compact) */
    run_oc_json(root, "tests/fixtures/rust_resolver", "check", buf, sizeof(buf));
    CHECK(strstr(buf, "\"files_analyzed\":11") != NULL);
    CHECK(strstr(buf, "\"files_with_parse_errors\":0") != NULL);
    /* OC003 exactly once (the a/b crate-path cycle) */
    {
        const char *needle = "\"rule\":\"OC003\"";
        size_t hits = 0;
        const char *q = buf;
        while ((q = strstr(q, needle)) != NULL) {
            hits++;
            q += strlen(needle);
        }
        CHECK(hits == 1);
    }
    /* OC006 exactly once, on the orphan (tests/intg.rs is runner data;
     * src/bin/tool.rs is claimed by the fn main fact) */
    CHECK(strstr(buf, "orphan.rs") != NULL);
    {
        const char *needle = "\"rule\":\"OC006\"";
        size_t hits = 0;
        const char *q = buf;
        while ((q = strstr(q, needle)) != NULL) {
            hits++;
            q += strlen(needle);
        }
        CHECK(hits == 1);
    }
    /* the decoy Cargo.toml is OC010 material (no licence at the root)
     * and the OC005 verdicts are exactly the honest four */
    CHECK(strstr(buf, "\"rule\":\"OC010\"") != NULL);
    CHECK(strstr(buf, "Export 'isolated'") != NULL);
    CHECK(strstr(buf, "Export 'store_root'") != NULL);
    CHECK(strstr(buf, "Export 'fmt_name'") != NULL);
    CHECK(strstr(buf, "Export 'fmt_inner'") != NULL);
    /* graph: 12 edges with the contracted endpoints; the bin target and
     * the integration test produce NONE */
    run_oc_json(root, "tests/fixtures/rust_resolver", "graph", buf, sizeof(buf));
    {
        size_t hits = 0;
        const char *q = buf;
        while ((q = strstr(q, "\"kind\":\"imports\"")) != NULL) {
            hits++;
            q += 17;
        }
        CHECK(hits == 12);
    }
    /* the bin file appears as a file node (name + path) AND as its
     * function symbol's path -- never as an edge endpoint */
    {
        size_t node_hits = 0;
        const char *q = buf;
        while ((q = strstr(q, "bin/tool.rs")) != NULL) {
            node_hits++;
            q += 11;
        }
        CHECK(node_hits == 3); /* file node name+path + the main symbol */
    }
}

int main(void) {
    unit_rust_symbols();
    unit_rust_imports();
    unit_rust_resolver();
    unit_rust_surface();
    unit_rust_main_fact_scoped();
    unit_lcov();
    unit_cargo_manifests();
    unit_osv_crates();
    unit_rust_inner_attribute_not_shebang();
    unit_rust_test_convention();
    unit_rust_cli();
    TEST_SUMMARY();
}
