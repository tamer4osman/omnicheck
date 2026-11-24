/* Probe tree-sitter-ruby 0.23.1 shape facts (adoption-time verification,
 * the C# / Rust probe pattern: every contract shape guess is probed
 * against the PINNED grammar with an actual parse before any adoption
 * code exists). */
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
extern const TSLanguage *tree_sitter_ruby(void);

static void dump(TSNode n, const char *src, int depth) {
    const char *t = ts_node_type(n);
    int is_named = ts_node_is_named(n);
    for (int i = 0; i < depth; i++) fputs("  ", stdout);
    printf("%s%s", t, is_named ? "" : " (anon)");
    /* field names of named children are printed by the parent loop below */
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

int main(void) {
    const char *src =
        "# frozen_string_literal: true\n"
        "# a plain comment\n"
        "=begin\n"
        "block comment\n"
        "=end\n"
        "require 'json'\n"
        "require \"a/b\"\n"
        "require_relative 'store/types'\n"
        "require_relative '../lib/helper'\n"
        "class Foo::Bar < Baz\n"
        "  def initialize(a, b = 1)\n"
        "    @x = a\n"
        "  end\n"
        "  def self.build(&blk)\n"
        "    new(&blk)\n"
        "  end\n"
        "  def long_name = :endless\n"
        "  def [](idx); idx; end\n"
        "  def name=(v); @name = v; end\n"
        "  private\n"
        "  def if_chain(x)\n"
        "    if x > 0 then :pos\n"
        "    elsif x == 0\n"
        "      :zero\n"
        "    else\n"
        "      :neg\n"
        "    end\n"
        "  end\n"
        "  def decisions(x, y)\n"
        "    z = x && y || !x ? 1 : 2\n"
        "    while x > 0\n"
        "      x -= 1\n"
        "    end\n"
        "    until x.positive? do y += 1 end\n"
        "    for i in 0..2 do z += i end\n"
        "    case x\n"
        "    when 0, 1 then z += 1\n"
        "    when Integer then z += 2\n"
        "    else z += 3\n"
        "    end\n"
        "    begin\n"
        "      z /= x\n"
        "    rescue ZeroDivisionError, RangeError => e\n"
        "      z = 0\n"
        "    rescue\n"
        "      z = -1\n"
        "    ensure\n"
        "      z += 0\n"
        "    end\n"
        "    w = z rescue 0\n"
        "    z += 1 if x.positive?\n"
        "    z += 1 unless x.nil?\n"
        "    z += 1 while x > 9\n"
        "    z += 1 until x > 99\n"
        "    z = 1 and z = 2 or z = 3\n"
        "    [1, 2].each do |i| z += i end\n"
        "    [1, 2].each { |i| z += i }\n"
        "    z\n"
        "  end\n"
        "end\n"
        "module Naming\n"
        "  VERSION = '1.0'\n"
        "end\n"
        "class << self\n"
        "  def meta; 1; end\n"
        "end\n"
        "alias allother to_s\n"
        "__END__\n"
        "this is DATA, not ruby code\n";
    TSParser *p = ts_parser_new();
    ts_parser_set_language(p, tree_sitter_ruby());
    TSTree *tree = ts_parser_parse_string(p, NULL, src, (uint32_t)strlen(src));
    TSNode root = ts_tree_root_node(tree);
    printf("has_error: %d\n", ts_node_has_error(root));
    dump(root, src, 0);
    return 0;
}
