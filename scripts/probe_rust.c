/* Probe tree-sitter-rust 0.24.0 shape facts (adoption-time verification). */
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
extern const TSLanguage *tree_sitter_rust(void);

static void dump(TSNode n, const char *src, int depth) {
    const char *t = ts_node_type(n);
    int is_named = ts_node_is_named(n);
    for (int i = 0; i < depth; i++) fputs("  ", stdout);
    printf("%s%s", t, is_named ? "" : " (anon)");
    if (!is_named) {
        uint32_t s = ts_node_start_byte(n), e = ts_node_end_byte(n);
        printf(" [%.*s]", (int)(e - s), src + s);
    }
    printf("\n");
    uint32_t nc = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc; i++)
        dump(ts_node_child(n, i), src, depth + 1);
}

int main(void) {
    const char *src =
        "use crate::foo::bar;\n"
        "use self::a::b;\n"
        "use super::c;\n"
        "use ext::d::e as f;\n"
        "use crate::g::{h, i, j::k};\n"
        "use crate::l::*;\n"
        "pub use crate::m::n;\n"
        "pub(crate) fn hidden() {}\n"
        "pub fn shown() {}\n"
        "fn plain() {}\n"
        "mod inline_mod { pub fn deep() {} }\n"
        "mod file_mod;\n"
        "#[cfg(test)]\n"
        "mod attr_mod;\n"
        "static S: i32 = 1;\n"
        "const C: i32 = 2;\n"
        "type Alias = i32;\n"
        "pub struct St { a: i32 }\n"
        "pub enum En { A }\n"
        "pub trait Tr { fn sig(&self); fn def(&self) {} }\n"
        "macro_rules! mac { () => {}; }\n"
        "impl Tr for St {\n"
        "    fn def(&self) {\n"
        "        let x = |v| v + 1;\n"
        "        if x > 0 && x < 9 { loop { break; } }\n"
        "        while x > 0 { x -= 1; }\n"
        "        for i in 0..3 { if let Ok(y) = Ok(i) { let _ = y; } }\n"
        "        match x { 0 => {}, _ | 1 => {}, other => {}, }\n"
        "    }\n"
        "}\n";
    TSParser *p = ts_parser_new();
    ts_parser_set_language(p, tree_sitter_rust());
    TSTree *tree = ts_parser_parse_string(p, NULL, src, (uint32_t)strlen(src));
    TSNode root = ts_tree_root_node(tree);
    printf("has_error: %d\n", ts_node_has_error(root));
    dump(root, src, 0);
    return 0;
}
