#include "parser_internal.h"

#include <stdio.h>

/*
 * tree-sitter backend: parses a file into the unified graph (file node,
 * function symbol nodes with body spans + parse-time cyclomatic
 * complexity, CONTAINS edges), extracts comment docs, and records raw
 * import specifiers for later resolution (imports.c). Queries are
 * compiled once per language at startup.
 *
 * Adding a language = vendor its grammar (scripts/vendor.sh), add a
 * TSLanguage declaration + function/comment/import queries + complexity
 * kind table here, and extend LANGS. Passes never change
 * (register-not-train).
 */

#include "oc/imports.h"
#include "oc/surface.h"

extern const TSLanguage *tree_sitter_javascript(void);
extern const TSLanguage *tree_sitter_python(void);
extern const TSLanguage *tree_sitter_typescript(void);
extern const TSLanguage *tree_sitter_tsx(void);
extern const TSLanguage *tree_sitter_go(void);
extern const TSLanguage *tree_sitter_java(void);
extern const TSLanguage *tree_sitter_c_sharp(void);
extern const TSLanguage *tree_sitter_rust(void);
extern const TSLanguage *tree_sitter_ruby(void);
extern const TSLanguage *tree_sitter_php(void);
extern const TSLanguage *tree_sitter_c(void);
extern const TSLanguage *tree_sitter_cpp(void);
extern const TSLanguage *tree_sitter_kotlin(void);
extern const TSLanguage *tree_sitter_swift(void);
extern const TSLanguage *tree_sitter_scala(void);

static const char Q_JS_FUNCTIONS[] =
    "(function_declaration name: (identifier) @fname"
    "  body: (statement_block) @fbody) @fexpr"
    " (method_definition name: (property_identifier) @fname"
    "  body: (statement_block) @fbody) @fexpr"
    " (variable_declarator name: (identifier) @fname"
    "  value: (arrow_function body: (statement_block) @fbody)) @fexpr";

static const char Q_PY_FUNCTIONS[] = "(function_definition name: (identifier) @fname"
                                     "  body: (block) @fbody) @fexpr";

/* Go (SPRINT-9, Track D wave 1): named function and method declarations.
 * SPRINT-14 (§Go.2): named function-value assignments are ALSO symbols --
 * three promoted shapes, each with the func_literal as the WHOLE right
 * side (a direct child of the right expression_list / the spec's value
 * list): `f := func() {...}` (short_var_declaration), `f = func() {...}`
 * (assignment_statement), `var f = func() {...}` (var_spec, plain or
 * grouped -- the spec is the defining construct). The C side rejects
 * multi-name left sides (which name would carry the function is a guess),
 * the blank `_`, and non-`=` operators for the plain-assignment shape.
 * Anonymous literals (go/defer calls, arguments) stay non-symbols.
 * Contract: docs/LANGUAGES.md §Go.2. */
static const char Q_GO_FUNCTIONS[] = "(function_declaration name: (identifier) @fname"
                                     "  body: (block) @fbody) @fexpr"
                                     " (method_declaration name: (field_identifier) @fname"
                                     "  body: (block) @fbody) @fexpr"
                                     " (short_var_declaration"
                                     "  left: (expression_list (identifier) @fname)"
                                     "  right: (expression_list (func_literal) @fbody)) @fexpr"
                                     " (assignment_statement"
                                     "  left: (expression_list (identifier) @fname)"
                                     "  right: (expression_list (func_literal) @fbody)) @fexpr"
                                     " (var_spec name: (identifier) @fname"
                                     "  value: (expression_list (func_literal) @fbody)) @fexpr";

static const char Q_COMMENTS[] = "(comment) @comment";

/* Module surface (WBS 3.x, OC005/OC006): JS export statements are all
 * handled in C from the statement node (default tokens, export clauses,
 * re-export sources, declarations) so one query compiles for the whole
 * JS family. Python exports are top-level function/class definitions
 * (functions come from the function query above) plus __all__ lists. */
static const char Q_EXPORTS_JS[] = "(export_statement) @es";

/* BUGFIX N-3: the CommonJS export channel. The ESM query above sees only
 * export_statement nodes; CommonJS modules (all of nodejs/node lib/, the
 * whole tools tree, most webpack-era codebases) export through
 * ASSIGNMENTS and Object.assign calls, which were invisible to the
 * surface -- the compat gate reported SAFE across v25.0.0 -> HEAD while
 * functions were disappearing from lib/. Two patterns, verified
 * textually in C (the C side rejects anything that is not exactly a CJS
 * export target):
 *   1. assignment_expression with a member_expression LEFT
 *      (module.exports, exports, or one-level member exports.name);
 *   2. call_expression Object.assign(<target>, {...}, ...).
 * Computed keys, spreads, nested member chains and opaque reassignment
 * shapes are skipped: never guessed (see the C-side classifiers). */
static const char Q_EXPORTS_JS_CJS[] = "(assignment_expression"
                                       "  left: (member_expression) @cjsleft"
                                       "  right: (_) @cjsrhs) @cjasg"
                                       " (call_expression"
                                       "   function: (member_expression"
                                       "     object: (identifier) @oai"
                                       "     property: (property_identifier) @oap)"
                                       "   arguments: (arguments) @oaargs) @oacall";
static const char Q_EXPORTS_PY[] = "(class_definition name: (identifier) @cname) @cdef"
                                   " (assignment left: (identifier) @alname) @asg";

/* Go: package-level declarations with an exported (upper-case first rune,
 * ASCII this sprint -- Unicode case classes are a recorded descope) name.
 * Methods are NOT package surface: they hang off receiver types, and the
 * method name does not decide Go exportedness alone. The query itself
 * constrains every capture to direct source_file children, so no C-side
 * parent check is needed. Contract: docs/LANGUAGES.md §Go.6. */
static const char Q_EXPORTS_GO[] =
    "(source_file (function_declaration name: (identifier) @fname))"
    " (source_file (type_declaration (type_spec name: (type_identifier) @tname)))"
    " (source_file (const_declaration (const_spec name: (identifier) @cname)))"
    " (source_file (var_declaration (var_spec name: (identifier) @vname)))";

/* Raw import specifiers, resolved later by imports.c against the
 * discovered file set. JS: static import/export + require()/import()
 * calls (the callee is filtered in C, not by query predicate).
 * Python: plain dotted imports + from-imports (dotted and relative).
 * @ist/@est/@ifs capture the whole statement so the C side can enumerate
 * imported NAMES for the module-surface bindings. */
static const char Q_IMPORTS_JS[] = "(import_statement source: (string) @isrc) @ist"
                                   " (export_statement source: (string) @isrc) @est"
                                   " (call_expression function: (identifier) @ical"
                                   "  arguments: (arguments (string) @isrc))";

static const char Q_IMPORTS_PY[] =
    "(import_statement (dotted_name) @iname)"
    " (import_statement (aliased_import name: (dotted_name) @iname))"
    " (import_from_statement module_name: (dotted_name) @imod) @ifs"
    " (import_from_statement module_name: (relative_import) @imod) @ifs";

/* Go: one raw specifier per import_spec (aliased / blank / dot imports
 * bind the same package path) plus the import_spec node itself so the C
 * side can record the import BINDING (SPRINT-10): the alias when aliased,
 * NULL otherwise (plain, blank `_`, dot `.`). The binding feeds the
 * module surface; marking stays package-wide fail-open per contract, so
 * the alias is surface data, never a narrowing predicate.
 * The query matches the import_spec at ANY depth under its declaration,
 * covering both `import "x"` and the grouped `import ( ... )` form --
 * SPRINT-9's declaration-anchored pattern silently missed every grouped
 * block (latent bug, invisible while the spike resolver resolved
 * nothing; surfaced by the SPRINT-10 resolver fixtures).
 * Contract: docs/LANGUAGES.md §Go.5/§Go.6. */
static const char Q_IMPORTS_GO[] = "(import_spec path: (interpreted_string_literal) @isrc) @ispec";

/* Go: the one package_clause per file, recorded as a parse-time hint so
 * the surface build can claim `package main` files as entry points
 * (SPRINT-10, docs/LANGUAGES.md §Go.6). Note: grammar v0.25.0 declares
 * NO field name on package_clause -- the package_identifier is a plain
 * named child (verified against the pinned grammar, not assumed). */
static const char Q_GO_PACKAGE[] = "(package_clause (package_identifier) @pkg)";

/* Java (SPRINT-12, Track D wave 1): the three defining constructs are
 * symbols; the canonical span is the whole declaration (definition
 * extent). Abstract and interface methods were promoted to symbols at
 * SPRINT-14 (the bodyless query below); the bodyful query keeps its
 * `block`/`constructor_body` requirement. compact_constructor_declaration
 * (records) is the compact `R { ... }` form. */
static const char Q_JAVA_FUNCTIONS[] =
    "(method_declaration name: (identifier) @fname body: (block) @fbody) @fexpr"
    " (constructor_declaration name: (identifier) @fname"
    "  body: (constructor_body) @fbody) @fexpr"
    " (compact_constructor_declaration name: (identifier) @fname"
    "  body: (block) @fbody) @fexpr";

/* Java (SPRINT-14): abstract and interface methods ARE symbols -- the
 * declaration itself is the definition extent, complexity exactly 1 (no
 * body, no decisions). The negated-field pattern (`!body`) can only
 * match bodyless method declarations, so a bodyful method never matches
 * twice (the functions query above keeps its body requirement).
 * Contract: docs/LANGUAGES.md Java section 2. */
static const char Q_JAVA_BODYLESS[] = "(method_declaration name: (identifier) @fname !body) @fexpr";

/* Java splits line and block comments (OC002 consumes both verbatim;
 * Javadoc is plain comments -- no Javadoc model). */
static const char Q_JAVA_COMMENTS[] = "(line_comment) @comment (block_comment) @comment";

/* Java import declarations: the C side decides single-type / static /
 * wildcard shape from the declaration's children (the path child's
 * text IS the dotted path as written; `static` is an anonymous token;
 * the wildcard is an `asterisk` node). */
static const char Q_JAVA_IMPORTS[] = "(import_declaration) @ist";

/* Java module surface: top-level type declarations are export
 * candidates; the `public` modifier decides exportedness (the query
 * constrains every capture to direct `program` children, so nested
 * types are never candidates). Members are not module surface v1 --
 * methods are reachable through their type. Contract: docs/LANGUAGES.md
 * Java section. */
static const char Q_JAVA_EXPORTS[] =
    "(program (class_declaration (modifiers) @jmods name: (identifier) @tname)) @tdecl"
    " (program (interface_declaration (modifiers) @jmods name: (identifier) @tname)) @tdecl"
    " (program (enum_declaration (modifiers) @jmods name: (identifier) @tname)) @tdecl"
    " (program (record_declaration (modifiers) @jmods name: (identifier) @tname)) @tdecl";

/* Java: classic decision kinds + the ternary node (named
 * `ternary_expression` in the pinned grammar -- verified against the
 * symbol table at adoption) + && / || inside binary_expression. The
 * `switch_block_statement_group` case-label check (a default-only
 * group is not a decision) is applied in C by the family hook below.
 * Contract: docs/LANGUAGES.md Java section. */
static const char *const CX_KINDS_JAVA[] = {"if_statement",
                                            "for_statement",
                                            "enhanced_for_statement",
                                            "while_statement",
                                            "do_statement",
                                            "switch_rule",
                                            "switch_block_statement_group",
                                            "catch_clause",
                                            "ternary_expression",
                                            NULL};
static const char *const CX_OPS_JAVA[] = {"&&", "||", NULL};

/* Cyclomatic-complexity decision kinds per language (register-not-train:
 * the table ships with the language descriptor, not with the pass).
 * JS family: branch/loop/case/catch/ternary nodes, plus the && / || / ??
 * operators inside binary_expression. Python: branch/loop/except nodes,
 * boolean operators, ternaries and comprehension ifs. */
static const char *const CX_KINDS_JS[] = {
    "if_statement",    "for_statement",          "for_in_statement",
    "while_statement", "do_statement",           "switch_case",
    "catch_clause",    "conditional_expression", NULL};
static const char *const CX_OPS_JS[] = {"&&", "||", "??", NULL};

static const char *const CX_KINDS_PY[] = {
    "if_statement",           "elif_clause",   "for_statement",
    "while_statement",        "except_clause", "boolean_operator",
    "conditional_expression", "if_clause",     NULL};

/* Go: gocyclo-aligned decision kinds (if/for/case clauses of the three
 * switch forms; Go has no ternary; else-if is a nested if_statement so
 * each branch increments by itself) plus && and || inside
 * binary_expression. Contract: docs/LANGUAGES.md §Go.4. */
static const char *const CX_KINDS_GO[] = {"if_statement", "for_statement",      "expression_case",
                                          "type_case",    "communication_case", NULL};
static const char *const CX_OPS_GO[] = {"&&", "||", NULL};

/* C# (SPRINT-13, Track D wave 1): the five named defining constructs
 * with a real body are symbols; the canonical span is the whole
 * declaration (definition extent). Bodies are `block` or, for
 * expression-bodied members, the grammar's `arrow_expression_clause`
 * -- both clauses per kind. Interface/abstract methods (no body) are
 * honestly NOT symbols; property/event accessors are a recorded
 * descope (docs/LANGUAGES.md C# section 2). */
static const char Q_CSHARP_FUNCTIONS[] =
    "(method_declaration name: (identifier) @fname body: (block) @fbody) @fexpr"
    " (method_declaration name: (identifier) @fname"
    "  body: (arrow_expression_clause) @fbody) @fexpr"
    " (constructor_declaration name: (identifier) @fname"
    "  body: (block) @fbody) @fexpr"
    " (constructor_declaration name: (identifier) @fname"
    "  body: (arrow_expression_clause) @fbody) @fexpr"
    " (local_function_statement name: (identifier) @fname"
    "  body: (block) @fbody) @fexpr"
    " (local_function_statement name: (identifier) @fname"
    "  body: (arrow_expression_clause) @fbody) @fexpr"
    " (operator_declaration operator: _ @fname"
    "  body: (block) @fbody) @fexpr"
    " (operator_declaration operator: _ @fname"
    "  body: (arrow_expression_clause) @fbody) @fexpr"
    " (destructor_declaration name: (identifier) @fname"
    "  body: (block) @fbody) @fexpr";

/* C# (SPRINT-14): property/event accessors with a real body are symbols.
 * The accessor_declaration's `name` field IS the keyword token (verified
 * against the pinned grammar: `get`/`set`/`add`/`remove`); the C side
 * synthesizes the honest qualified name `Declaring.get` from the
 * accessor_list's owner (`property_declaration` / `event_declaration`
 * `name` field). Auto-property accessors (`get;`) have no body and never
 * match. This closes a real accounting hole: accessor decisions were
 * previously counted toward NOTHING (properties are not symbols, and
 * class members have no enclosing function). Contract:
 * docs/LANGUAGES.md C# section 2. */
static const char Q_CSHARP_ACCESSORS[] =
    "(accessor_declaration body: (block) @fbody) @fexpr"
    " (accessor_declaration body: (arrow_expression_clause) @fbody) @fexpr";

/* C# import directives: the C side decides plain / static / alias
 * shape from the directive's children (the path child's text is the
 * dotted path as written; `static` is an anonymous token; the alias
 * form carries the field `name` identifier and an anonymous `equals`).
 * docs/LANGUAGES.md C# section 5. */
static const char Q_CSHARP_IMPORTS[] = "(using_directive) @ist";

/* C# main fact (SPRINT-13): a method_declaration named Main whose
 * modifier set carries `static` (accessibility NOT required -- C#
 * accepts non-public entry points; the fact fires on the declaration
 * as parsed). The C side walks the declaration's named `modifier`
 * children (verified: individual modifier nodes, no wrapper). */
static const char Q_CSHARP_MAIN[] = "(method_declaration name: (identifier) @jname) @jdecl";

/* C# top-level-statements fact (SPRINT-13): a compilation_unit with a
 * `global_statement` direct child IS the program entry point (C# 9
 * semantics -- a cheap parse fact, the same class as the Go
 * package_clause and the Java main fact). */
static const char Q_CSHARP_TOP[] = "(compilation_unit (global_statement)) @gs";

/* C# module surface: top-level type declarations are export
 * candidates; the `public` modifier decides exportedness. The C side
 * checks (a) the declaration's modifier children for `public` and
 * (b) the top-level parent walk (namespace nesting and preprocessor
 * wrappers are climbed; type nesting is not -- the verified node
 * walk). Multiple public types per file are all candidates (C# has no
 * one-public-type-per-file rule). Members are not module surface v1.
 * docs/LANGUAGES.md C# section 6. */
static const char Q_CSHARP_EXPORTS[] = "(class_declaration name: (identifier) @tname) @tdecl"
                                       " (interface_declaration name: (identifier) @tname) @tdecl"
                                       " (struct_declaration name: (identifier) @tname) @tdecl"
                                       " (enum_declaration name: (identifier) @tname) @tdecl"
                                       " (record_declaration name: (identifier) @tname) @tdecl";

/* C# decision kinds (verified against the pinned grammar's symbol
 * table, docs/LANGUAGES.md C# section 4): classic loops + the
 * switch_section case-label check (a default-only section is not a
 * decision -- applied in C by the family hook below, the anonymous
 * `case` token) + switch expression arms + catch clauses and their
 * `when` filters + the ternary (named `conditional_expression` -- the
 * verified name; the pre-adoption `ternary_expression` guess would not
 * have compiled) + && / || / ?? inside binary_expression. */
static const char *const CX_KINDS_CSHARP[] = {"if_statement",
                                              "for_statement",
                                              "foreach_statement",
                                              "while_statement",
                                              "do_statement",
                                              "switch_section",
                                              "switch_expression_arm",
                                              "catch_clause",
                                              "catch_filter_clause",
                                              "conditional_expression",
                                              NULL};
static const char *const CX_OPS_CSHARP[] = {"&&", "||", "??", NULL};

/* Rust (SPRINT-15, Track D wave 1): the named defining constructs with a
 * real body are symbols -- free functions, impl methods, trait default
 * methods (all `function_item`), plus the ITEM-LEVEL named closures: a
 * const_item / static_item whose value IS a closure_expression (the
 * const/static promotion, docs/LANGUAGES.md Rust section 2 -- a named
 * function-value binding at module scope; a const/static whose value is
 * not a closure is a named constant, not a symbol). Local `let`-bound
 * closures are NEVER symbols (the deliberate divergence from Go's
 * promoted locals: the binding lives inside the enclosing function's
 * span already -- inclusive counting, nothing hidden). The canonical
 * span is the whole defining construct (definition extent, OBS-2). */
static const char Q_RUST_FUNCTIONS[] =
    "(function_item name: (identifier) @fname body: (block) @fbody) @fexpr"
    " (const_item name: (identifier) @fname"
    "  value: (closure_expression) @fbody) @fexpr"
    " (static_item name: (identifier) @fname"
    "  value: (closure_expression) @fbody) @fexpr";

/* Rust (SPRINT-15): trait method signatures and extern-block function
 * declarations ARE symbols (the Java SPRINT-14 discipline through the
 * same q_bodyless slot) -- the uniform bodyless node of the pinned
 * grammar is `function_signature_item` (trait declaration_list items
 * AND foreign_mod_item items, verified at adoption); the declaration
 * itself is the definition extent, complexity exactly 1.
 * `macro_rules!` definitions are ALSO symbols here (docs/LANGUAGES.md
 * Rust section 2): a named defining construct whose complexity is
 * exactly 1 BY CONSTRUCTION -- macro bodies are token trees, no
 * decision node parses inside them (verified at adoption). */
static const char Q_RUST_BODYLESS[] = "(function_signature_item name: (identifier) @fname) @fexpr"
                                      " (macro_definition name: (identifier) @fname) @fexpr";

/* Rust comments: line_comment covers // and doc comments (the doc
 * markers are children INSIDE the comment span -- consumed verbatim, no
 * doc-tree model). BUGFIX OC002-R: block_comment joins the same comment
 * channel -- the nodejs/node audit caught OC002 missing every block
 * comment marker in vendored crates (astronomy.rs:430, hex_float.rs:318,
 * float_traits.rs:359) because only line_comment was queried. */
static const char Q_RUST_COMMENTS[] = "(line_comment) @comment (block_comment) @comment";

/* Rust import channels (SPRINT-15): (a) `use_declaration` nodes -- the
 * C side expands the argument use-tree into leaf specifiers (the head
 * keywords crate/self/super are NAMED nodes whose text is the keyword;
 * brace groups expand prefix::leaf per child; use_as_clause records the
 * pre-alias path); (b) the bodyless `mod_item` (`mod name;`) -- the
 * mod-tree channel records `mod <name>` (a real reference: Rust
 * compiles the file; it pushes NO import binding -- declaring a module
 * is not using any of its items). The negated-field pattern (`!body`)
 * matches only file-module declarations, never inline mods.
 * Path-attributed mods (`#[path = ...]`) are dropped in C by the
 * previous-sibling attribute walk (the attribute moves the file;
 * probing the conventional name would fabricate a WRONG edge).
 * docs/LANGUAGES.md Rust section 5. */
static const char Q_RUST_IMPORTS[] = "(use_declaration) @ist"
                                     " (mod_item name: (identifier) @imodname !body) @imod";

/* Rust module surface (SPRINT-15): top-level source_file items with
 * PLAIN pub visibility -- the modifier node's exact text `pub` decides
 * in C (pub(crate)/pub(super)/pub(in path) carry extra tokens and are
 * NOT module surface: restricted visibility does not cross the crate
 * boundary, Rust's own rule). Methods are not surface (the Go method
 * rule); macros are not surface (#[macro_export] is a recorded
 * boundary); a pub use re-export is import-channel data, not an export
 * candidate. The query constrains every capture to direct source_file
 * children, so nested items never become candidates.
 * docs/LANGUAGES.md Rust section 6. */
static const char Q_RUST_EXPORTS[] =
    "(source_file (function_item (visibility_modifier) @rvis name: (identifier) @rname))"
    " (source_file (struct_item (visibility_modifier) @rvis name: (type_identifier) @rname))"
    " (source_file (enum_item (visibility_modifier) @rvis name: (type_identifier) @rname))"
    " (source_file (type_item (visibility_modifier) @rvis name: (type_identifier) @rname))"
    " (source_file (const_item (visibility_modifier) @rvis name: (identifier) @rname))"
    " (source_file (static_item (visibility_modifier) @rvis name: (identifier) @rname))"
    " (source_file (trait_item (visibility_modifier) @rvis name: (type_identifier) @rname))"
    " (source_file (mod_item (visibility_modifier) @rvis name: (identifier) @rname))";

/* Rust main fact (SPRINT-15): a `function_item` named `main` that is a
 * DIRECT child of source_file claims the file (the Go package_clause
 * class of cheap parse facts -- this claims src/bin/<name>.rs binaries,
 * which the pack glob cannot reach: they are not named main.rs). The
 * C side checks the name; the source_file anchor keeps nested/impl
 * methods out. */
static const char Q_RUST_MAIN[] = "(source_file (function_item name: (identifier) @jname) @jdecl)";

/* Rust decision kinds (verified against the pinned grammar's
 * node-types, docs/LANGUAGES.md Rust section 4): the four loop/branch
 * expression kinds (`if let` is an if_expression with a let_condition
 * and `while let` a while_expression -- there are NO separate if-let/
 * while-let node kinds in the pinned grammar; `else if` is a nested
 * if_expression, so each branch increments by itself, the Go
 * discipline) + ONE decision per match ARM (an arm with alternative
 * patterns `a | b` is still one arm -- one exhaustive split of the
 * same value space, gocyclo's case-clause discipline; pinned by
 * fixture) + the `?` propagation (try_expression) + && / || inside
 * binary_expression. */
static const char *const CX_KINDS_RUST[] = {"if_expression",
                                            "while_expression",
                                            "for_expression",
                                            "loop_expression",
                                            "match_arm",
                                            "try_expression",
                                            NULL};
static const char *const CX_OPS_RUST[] = {"&&", "||", NULL};

/* Ruby (SPRINT-16, Track D wave 1): the two defining constructs with a
 * real body are symbols -- `method` (plain def, endless defs are the
 * SAME node) and `singleton_method` (def self.x / def Const.x). The
 * name field is one of THREE verified node kinds (identifier / setter /
 * operator) -- the wildcard capture takes whatever node it is and the
 * symbol name is the name node's text as written. The canonical span is
 * the whole defining construct (definition extent, OBS-2).
 * Contract: docs/LANGUAGES.md Ruby section 2. */
static const char Q_RUBY_FUNCTIONS[] = "(method name: _ @fname body: _ @fbody) @fexpr"
                                       " (singleton_method name: _ @fname body: _ @fbody) @fexpr";

/* Ruby (SPRINT-16): EMPTY methods (`def foo; end`) have NO body field
 * (verified at adoption) -- they ARE symbols, the Java SPRINT-14
 * discipline through the same q_bodyless slot: the whole definition is
 * the span, complexity exactly 1. The negated-field pattern cannot
 * match a bodyful method twice (the functions query above keeps its
 * body requirement). Contract: docs/LANGUAGES.md Ruby section 2. */
static const char Q_RUBY_BODYLESS[] = "(method name: _ @fname !body) @fexpr"
                                      " (singleton_method name: _ @fname !body) @fexpr";

/* Ruby comments: one kind covers `#` line comments AND `=begin`/=end`
 * blocks (verified at adoption; consumed verbatim, no doc model). */
static const char Q_RUBY_COMMENTS[] = "(comment) @comment";

/* Ruby import channel (SPRINT-16): require/require_relative CALLS --
 * the query captures every call's method field + argument list and the
 * C side filters the two names (receiver forms like `Kernel.require
 * 'x'` keep the same method field shape). The argument guard (exactly
 * ONE static string argument) is applied in C. The recorded specifier
 * carries its own channel tag (`require_relative p` / `require p` --
 * the Rust `mod ` precedent). docs/LANGUAGES.md Ruby section 5. */
static const char Q_RUBY_IMPORTS[] =
    "(call method: (identifier) @imeth arguments: (argument_list) @ial) @ist";

/* Ruby module surface (SPRINT-16): top-level `class` and `module`
 * declarations (the name node's text AS WRITTEN: constant or
 * scope_resolution -- `class Foo::Bar` records `Foo::Bar`) plus
 * top-level constant assignments (the left side must be a plain
 * `constant`). Ruby has no file-scope visibility modifier (everything a
 * loaded file defines is globally visible), so there is NO
 * exportedness filter. The query constrains every capture to direct
 * `program` children, so nested classes/modules and member constants
 * never become candidates. `class << self` (singleton_class) has no
 * name and is never surface. docs/LANGUAGES.md Ruby section 6. */
static const char Q_RUBY_EXPORTS[] = "(program (class name: (_) @cname) @cdef)"
                                     " (program (module name: (_) @cname) @cdef)"
                                     " (program (assignment left: (constant) @cname) @cdef)";

/* Ruby decision kinds (verified against the pinned grammar's node-types
 * AND a live parse, docs/LANGUAGES.md Ruby section 4): the block forms
 * if/unless/while/until/for (else is never a decision; elsif is its own
 * node, so each branch increments by itself, the Go discipline) + ONE
 * decision per when clause of a case (comma-separated multiple patterns
 * are ONE clause -- gocyclo's case-clause discipline) + ONE decision per
 * in_clause of a case_match (an if_guard does NOT count separately: a
 * failing guard falls through to the NEXT clause, part of the same
 * split) + rescue clauses (ensure is not a decision) + the five postfix
 * modifier forms (each its own named node) + the ternary (conditional).
 * NOTE: the ANONYMOUS keyword tokens SHARE these type strings in the
 * pinned grammar (`if`, `when`, `rescue`, ...) -- the shared complexity
 * walker matches kinds on NAMED nodes only (the SPRINT-16 hardening)
 * or every Ruby count would be roughly double. */
static const char *const CX_KINDS_RUBY[] = {"if",
                                            "elsif",
                                            "unless",
                                            "while",
                                            "until",
                                            "for",
                                            "when",
                                            "in_clause",
                                            "rescue",
                                            "rescue_modifier",
                                            "if_modifier",
                                            "unless_modifier",
                                            "while_modifier",
                                            "until_modifier",
                                            "conditional",
                                            NULL};
/* Ruby ops: && / || / and / or -- the low-precedence keyword forms
 * branch exactly like their symbol forms. The container node is
 * `binary` (the verified name; every other language keeps the shared
 * `binary_expression` default through the cx_ops_node descriptor
 * slot). */
static const char *const CX_OPS_RUBY[] = {"&&", "||", "and", "or", NULL};

/* PHP (SPRINT-19, Track E wave-2 completion): the two defining
 * constructs with a real body are symbols -- `function_definition`
 * (top-level and nested) and `method_declaration`. The name field is a
 * `name` node; the canonical span is the whole defining construct
 * (definition extent, OBS-2). Anonymous constructs (anonymous_function
 * closures, arrow_function, anonymous_class) are NOT symbols -- the
 * Ruby blocks precedent: their decisions count inward, inclusive
 * (they sit INSIDE the enclosing body's subtree, so the shared walker
 * reaches them with no extra rule). Contract: docs/LANGUAGES.md PHP
 * section 2. */
static const char Q_PHP_FUNCTIONS[] =
    "(function_definition name: _ @fname body: (compound_statement) @fbody) @fexpr"
    " (method_declaration name: _ @fname body: (compound_statement) @fbody) @fexpr";

/* PHP (SPRINT-19): EMPTY methods -- interface signatures and `abstract`
 * declarations -- carry NO body field (verified at adoption); they ARE
 * symbols, the Java SPRINT-14 discipline through the same q_bodyless
 * slot: the whole declaration is the span, complexity exactly 1. Only
 * method_declaration can be bodyless (a function_definition always has
 * a body). The negated-field pattern cannot match a bodyful method
 * twice (the functions query above keeps its body requirement).
 * Contract: docs/LANGUAGES.md PHP section 2. */
static const char Q_PHP_BODYLESS[] = "(method_declaration name: _ @fname !body) @fexpr";

/* PHP comments: line, block and doc comments are ONE `comment` node
 * kind (verified at adoption; consumed verbatim, no doc model) -- the
 * shared Q_COMMENTS already matches it, so q_comments stays NULL.
 * PHP 8 attributes are `attribute` nodes, not comments. */

/* PHP import channel (SPRINT-19): the two contracted channels in one
 * query, each match carrying @ist:
 *   - a `namespace_use_declaration` (the use surface: plain, aliased,
 *     function/const and grouped forms; the C side expands the clauses
 *     -- one import + one bind per clause, the Rust use-tree
 *     precedent);
 *   - a require/include EXPRESSION with a static string argument
 *     (@iarg): the query-level half of the argument guard -- a dynamic
 *     argument (a variable, a concatenation like the probe's
 *     `__DIR__ . '/x.php'` binary_expression, an interpolated string)
 *     simply does not match, so it records NOTHING; the C side still
 *     re-checks the exact-one-static-string shape (never guessed).
 * Channel tags travel in the specifier (`require p` etc., the Rust
 * `mod ` precedent). docs/LANGUAGES.md PHP section 5. */
static const char Q_PHP_IMPORTS[] = "(namespace_use_declaration) @ist"
                                    " (require_expression (string) @iarg) @ist"
                                    " (require_once_expression (string) @iarg) @ist"
                                    " (include_expression (string) @iarg) @ist"
                                    " (include_once_expression (string) @iarg) @ist";

/* PHP module surface (SPRINT-19): top-level class / interface / trait /
 * enum declarations and top-level function_definition names, AS
 * WRITTEN, plus the enum's `enum_case` members and top-level
 * const_declaration const_element names. PHP has no file-scope
 * visibility filter on namespace members (all of them are public
 * surface -- the Ruby precedent), so there is NO exportedness filter.
 * Top-level means a DIRECT child of `program` (statement-form
 * namespaces leave declarations there -- verified) or of a braced
 * namespace's body (a compound_statement). Nested classes and class
 * members never become candidates. docs/LANGUAGES.md PHP section 6. */
static const char Q_PHP_EXPORTS[] =
    "(program (class_declaration name: (name) @cname))"
    " (program (interface_declaration name: (name) @cname))"
    " (program (trait_declaration name: (name) @cname))"
    " (program (enum_declaration name: (name) @cname))"
    " (program (function_definition name: (name) @cname))"
    " (program (const_declaration (const_element (name) @cname)))"
    " (namespace_definition body: (compound_statement (class_declaration name: (name) @cname)))"
    " (namespace_definition body: (compound_statement (interface_declaration name: (name) @cname)))"
    " (namespace_definition body: (compound_statement (trait_declaration name: (name) @cname)))"
    " (namespace_definition body: (compound_statement (enum_declaration name: (name) @cname)))"
    " (namespace_definition body: (compound_statement (function_definition name: (name) @cname)))"
    " (namespace_definition body: (compound_statement"
    "   (const_declaration (const_element (name) @cname))))"
    " (enum_case name: (name) @cname)";

/* PHP decision kinds (verified against the pinned grammar AND a live
 * parse, docs/LANGUAGES.md PHP section 4): if_statement (elseif is its
 * own else_if_clause node, so each branch increments by itself, the Go
 * discipline; else is a bare else_clause, not a decision) + while/do/
 * for/foreach + ONE decision per case_statement (a default arm is the
 * DISTINCT default_statement node and counts 0 -- the gocyclo
 * discipline) + ONE decision per match arm (a comma condition list is
 * ONE match_conditional_expression arm -- the Ruby when-clause
 * discipline; the match_default_expression arm IS an arm) + catch
 * clauses (try and finally are not decisions) + the ternary
 * conditional_expression (the full `?:` and the short `?:` form are
 * the SAME node). `??` is a binary_expression and NOT a decision.
 * All kind strings are named node types: no anonymous collision (the
 * SPRINT-16 named-only match stays). */
static const char *const CX_KINDS_PHP[] = {"if_statement",
                                           "else_if_clause",
                                           "while_statement",
                                           "do_statement",
                                           "for_statement",
                                           "foreach_statement",
                                           "case_statement",
                                           "match_conditional_expression",
                                           "match_default_expression",
                                           "catch_clause",
                                           "conditional_expression",
                                           NULL};
/* PHP ops: && / || / and / or -- the low-precedence keyword forms
 * branch exactly like their symbol forms. The container node is the
 * shared `binary_expression` default (cx_ops_node stays NULL). */
static const char *const CX_OPS_PHP[] = {"&&", "||", "and", "or", NULL};

/* C (SPRINT-20, wave-2): the defining construct is `function_definition`
 * -- C functions cannot nest, so every definition is a translation-unit
 * (or preprocessor-guarded) one. The grammar has NO name field: the
 * symbol name is the identifier at the BOTTOM of the declarator chain
 * (plain `void f(void)`, pointer returns `char *dup(const char *)`,
 * parenthesized and function-pointer-return shapes all wrap it --
 * verified by the adoption probe, scripts/probe_c.c). The capture
 * carries the whole function_declarator and the C branch at the match
 * site resolves the identifier by descending `declarator` fields, never
 * guessed. Prototypes (declarations with a function_declarator) are NOT
 * symbols: unlike the Java abstract-method case the definition normally
 * exists in the same analysis root, and counting both would double every
 * function and give OC007 two anchors per name (one uncovered from the
 * header). The header's API surface is the exports query's job instead.
 * Contract: docs/LANGUAGES.md C section 2. */
static const char Q_C_FUNCTIONS[] = "(function_definition"
                                    "  declarator: (_) @fname"
                                    "  body: (compound_statement) @fbody) @fexpr";

/* C main fact (SPRINT-20): the entry-point parse fact is a function
 * DEFINITION (a prototype names nothing that runs) whose plain-identifier
 * name is `main` -- the same cheap-parse-fact class as the Go
 * `package main` clause and the Rust main fn. The C branch compares the
 * captured name text; a main behind an #ifdef wrapper still matches
 * (C functions cannot nest, so any function_definition named main is a
 * candidate -- the honest over-claim direction, OC006 severity is INFO). */
static const char Q_C_MAIN[] = "(function_definition"
                               "  declarator: (function_declarator declarator: (identifier) @jname)"
                               "  body: (compound_statement) @jdecl)";

/* C import channel (SPRINT-20): the ONE directive kind, one match per
 * include with the path child captured -- the C side classifies the two
 * honest static shapes (string_literal = the quoted form -> `include p`;
 * system_lib_string = the angle form -> `system p`) and records NOTHING
 * for a macro path (identifier/call_expression: the payload is not
 * static text, never guessed). Channel tags travel in the specifier
 * (the Rust `mod ` precedent). docs/LANGUAGES.md C section 5. */
static const char Q_C_IMPORTS[] = "(preproc_include path: (_) @ipath) @ist";

/* C module surface (SPRINT-20): top-level declarations AS PARSED, the
 * exportedness filter being C's own internal-linkage rule -- a top-level
 * declaration carrying a `static` storage-class specifier is NOT module
 * surface (the C# public-modifier precedent, inverted: C's default is
 * external, `static` narrows). The query matches candidate kinds ANYWHERE
 * and the C side climbs preprocessor wrappers (#ifndef include guards
 * wrap every conventional header) and `extern "C"` blocks through the
 * parent chain; the immediate non-wrapper parent must be translation_unit.
 * Captured: function definitions, declarations (prototypes, objects,
 * arrays, multi-declarator lines -- each declarator separately), type
 * definitions, struct/union/enum specifiers with BOTH a name and a body
 * (a bare `struct point p;` merely uses the tag), and object-like /
 * function-like macros. Enum constant names (enumerator) are file-scope
 * in C but NOT module surface v1 -- a documented boundary, honest and
 * pinned by fixture. docs/LANGUAGES.md C section 6. */
static const char Q_C_EXPORTS[] = "(function_definition) @fdef"
                                  " (declaration) @decl"
                                  " (type_definition) @tdef"
                                  " (struct_specifier) @sspec"
                                  " (union_specifier) @uspec"
                                  " (enum_specifier) @espec"
                                  " (preproc_def) @pdef"
                                  " (preproc_function_def) @pfdef";

/* C decision kinds (verified against the pinned grammar AND a live parse,
 * docs/LANGUAGES.md C section 4): if_statement (`else if` is a NESTED
 * if_statement inside an else_clause, so each branch increments by
 * itself, the Go discipline; a bare else_clause is not a decision) +
 * while/do/for + ONE decision per case_statement that carries a `value`
 * field (a `default:` arm is the SAME node type WITHOUT the value -- the
 * family guard in the walker counts it 0, the gocyclo discipline;
 * fall-through `case 2: case 3:` is two nodes, two decisions) + the
 * ternary conditional_expression. && and || are anonymous tokens inside
 * the shared binary_expression container (cx_ops_node stays NULL).
 * Preprocessor conditionals (#if/#ifdef/#elif) are NOT decisions:
 * they select code at COMPILE time, the analysis runs on raw source,
 * and no preprocessed configuration exists to tie a count to. */
static const char *const CX_KINDS_C[] = {"if_statement",
                                         "while_statement",
                                         "do_statement",
                                         "for_statement",
                                         "case_statement",
                                         "conditional_expression",
                                         NULL};
/* C ops: && / || only -- standard C has no keyword operator spellings
 * (iso646.h is a header the analysis never preprocesses). */
static const char *const CX_OPS_C[] = {"&&", "||", NULL};

/* C++ (SPRINT-21, wave-2 close-out): the defining construct is the SAME
 * `function_definition` node as C (the C grammar is inlined at generation
 * time), so the query text is identical -- but the shape universe is
 * wider: free functions, inline methods (inside a class body),
 * out-of-class definitions (`void S::f()`), constructors (no type
 * field), destructors (`~S`), operator overloads and TEMPLATED
 * functions (template_declaration wraps the definition) all match, and
 * the symbol name is resolved by the declarator-chain walk extended for
 * the cpp node kinds (field_identifier, qualified_identifier's name
 * field, destructor_name and operator_name carried AS WRITTEN). Member
 * declarations without a body are `declaration`/`field_declaration`
 * nodes, not function_definitions -- never symbols (the C prototype
 * discipline). Lambdas are anonymous constructs (the Ruby blocks
 * precedent): NOT symbols, their decisions count inward. Contract:
 * docs/LANGUAGES.md C++ section 2. */
static const char Q_CPP_FUNCTIONS[] = "(function_definition"
                                      "  declarator: (_) @fname"
                                      "  body: (compound_statement) @fbody) @fexpr";

/* C++ main fact (SPRINT-21): the same parse-fact class as C -- a
 * free-standing function DEFINITION whose declarator chain bottoms at a
 * plain identifier named `main`. The query text is Q_C_MAIN verbatim
 * (verified against the pinned cpp grammar by the adoption probe): the
 * identifier-only inner declarator keeps member functions (field
 * identifiers), out-of-class definitions (qualified identifiers) and
 * destructors out, so a class with a method `main` claims nothing --
 * the honest reading of the C++ entry model, OC006 severity INFO. */
static const char Q_CPP_MAIN[] =
    "(function_definition"
    "  declarator: (function_declarator declarator: (identifier) @jname)"
    "  body: (compound_statement) @jdecl)";

/* C++ import channel (SPRINT-21): the SAME directive kind as C -- C++
 * uses the preprocessor's include model unchanged (verified by the
 * probe: string_literal / system_lib_string / identifier path children).
 * The `using` family (using_declaration, alias_declaration,
 * using-directive, namespace aliases) records NOTHING in v1: no honest
 * file mapping exists without a manifest, and none is invented. Channel
 * tags travel in the specifier (the Rust `mod ` precedent). Contract:
 * docs/LANGUAGES.md C++ section 5. */
static const char Q_CPP_IMPORTS[] = "(preproc_include path: (_) @ipath) @ist";

/* C++ module surface (SPRINT-21): top-level declarations AS PARSED with
 * C++'s own internal-linkage filter. The toplevel climb is C's extended
 * with the two C++ wrapper kinds -- named namespace_definition bodies
 * (the PHP namespace precedent: the members are the file's surface) and
 * template_declaration (a wrapper around class/struct/function
 * templates); an ANONYMOUS namespace is internal linkage and every
 * candidate inside it is excluded (the C `static` rule generalized).
 * Captured: function definitions, declarations (every declarator-field
 * child separately), type definitions, the alias_declaration (`using X
 * = Y;` -- C++'s typedef spelling), struct/union/enum/class specifiers
 * with BOTH a name and a body, and macros. Class members are NOT module
 * surface (the class is the surface unit -- the C# type-nesting
 * precedent); enumerator constants stay the documented C boundary.
 * Contract: docs/LANGUAGES.md C++ section 6. */
static const char Q_CPP_EXPORTS[] = "(function_definition) @fdef"
                                    " (declaration) @decl"
                                    " (type_definition) @tdef"
                                    " (struct_specifier) @sspec"
                                    " (union_specifier) @uspec"
                                    " (enum_specifier) @espec"
                                    " (class_specifier) @cspec"
                                    " (alias_declaration) @adef"
                                    " (preproc_def) @pdef"
                                    " (preproc_function_def) @pfdef";

/* C++ decision kinds (verified against the pinned grammar AND a live
 * parse, docs/LANGUAGES.md C++ section 4): C's table verbatim (the C
 * statement nodes are inlined into the cpp grammar -- else-if is a
 * NESTED if_statement, the case_statement carries the `value` field
 * family guard) PLUS the two C++ statement kinds: for_range_loop (the
 * C++11 range-for is its OWN node type, not a for_statement) and
 * catch_clause (each handler is a branch into the handler -- the PHP
 * catch discipline; try/finally-like shapes are not decisions).
 * Preprocessor conditionals are not decisions (the C boundary). */
static const char *const CX_KINDS_CPP[] = {
    "if_statement",           "while_statement", "do_statement",
    "for_statement",          "for_range_loop",  "case_statement",
    "conditional_expression", "catch_clause",    NULL};
/* C++ ops: && / || (the keyword spellings need iso646.h, which the
 * analysis never preprocesses -- the C boundary). */
static const char *const CX_OPS_CPP[] = {"&&", "||", NULL};

/* Kotlin symbols (SPRINT-22, docs/LANGUAGES.md Kotlin section 2): a
 * function_declaration WITH a function_body -- free functions, methods,
 * extension functions (the receiver lives inside the `user_type`, so the
 * declaration's ONLY direct `simple_identifier` child is the name;
 * probed), generic/suspend/inline functions (modifiers and type
 * parameters are separate children; the wrapper adds no extent, the C++
 * template precedent). A body-less declaration (abstract/interface
 * members) is NOT a symbol -- the C prototype discipline: Kotlin has no
 * prototypes, so every real definition carries a body (block or `=`
 * expression, the SAME function_body node). Anonymous functions and
 * lambdas are anonymous_function/lambda_literal nodes: anonymous
 * constructs (the Ruby blocks precedent), decisions count inward. */
static const char Q_KT_FUNCTIONS[] = "(function_declaration"
                                     "  (simple_identifier) @fname"
                                     "  (function_body) @fbody) @fexpr";

/* Kotlin main fact (SPRINT-22): a top-level function DEFINITION named
 * `main` -- the source_file-anchored query (the Rust precedent) keeps
 * class/object members out, so `class App { fun main() }` claims
 * nothing (probed); the body requirement keeps nothing out at top level
 * (top-level functions cannot be abstract). The name text check happens
 * in C (the Rust branch shape). The entry model is honest: `fun main()`
 * and `fun main(args: Array<String>)` both match (the parameters are
 * not part of the shape); OC006 severity INFO. */
static const char Q_KT_MAIN[] = "(source_file (function_declaration"
                                "  (simple_identifier) @jname"
                                "  (function_body)))";

/* Kotlin import channel (SPRINT-22): every import_header of the file
 * (the import_list wrapper is never matched -- the header is the fact).
 * The C side classifies the three shapes from the header's children:
 * the dotted `identifier` text is the path AS WRITTEN; a
 * wildcard_import child makes it a package claim (the Java `.*`
 * asterisk-marker record); an import_alias child leaves the bind name
 * as the ORIGINAL last segment (the alias is local surface detail; the
 * reference still proves the original declaration is used -- the
 * over-mark direction for OC005, never a narrowing predicate). A member
 * import `a.b.C.f` (the object-member shape) is the AMBIGUOUS member
 * record: the path stays AS WRITTEN with a NULL bind (whole-file claim,
 * the C++ include discipline) and the resolver matches BOTH declared
 * readings. docs/LANGUAGES.md Kotlin section 5. */
static const char Q_KT_IMPORTS[] = "(import_header (identifier) @ipath) @ist";

/* Kotlin module surface (SPRINT-22): the named top-level declarations.
 * Kotlin has NO internal-linkage filter beyond `private`: a private
 * top-level declaration is file-visible only and never surface (the C
 * `static` rule's Kotlin spelling); public (the default) and internal
 * (module-visible) surface. The C side checks the parent chain is the
 * source_file (Kotlin has no wrapper nodes: no climb needed) and the
 * declaration's modifiers for `private`. Captured: function
 * definitions (a top-level function always has a body), class /
 * interface / enum declarations (ALL are class_declaration nodes in
 * the pinned grammar -- probed: `interface` and `enum class` are the
 * anonymous `interface`/`enum` tokens), object declarations,
 * property declarations (the val/var member shape; property accessors
 * are the property's parts, never surface), type aliases. Enum entries
 * and class members are not module surface (the type-nesting
 * precedent). docs/LANGUAGES.md Kotlin section 6. */
static const char Q_KT_EXPORTS[] = "(function_declaration) @fdef"
                                   " (class_declaration) @cdecl"
                                   " (object_declaration) @odecl"
                                   " (property_declaration) @pdecl"
                                   " (type_alias) @talias";

/* Kotlin package fact (SPRINT-22): the one package_header's dotted
 * identifier text, recorded as a parse hint (the Go package_clause
 * channel). The resolver v1-kotlin keys on it (the declared-fact
 * channel; docs/LANGUAGES.md Kotlin section 5). */
static const char Q_KT_PACKAGE[] = "(package_header (identifier) @pkg)";

/* Kotlin comments (SPRINT-22): the pinned grammar's two comment node
 * kinds (the Java pair with Kotlin's multiline spelling). The
 * shebang_line node is the shebang hint's source -- the shared
 * line-based shebang scan already covers it. */
static const char Q_KT_COMMENTS[] = "(line_comment) @comment (multiline_comment) @comment";

/* Kotlin accessors (SPRINT-22, the C# precedent): property accessors
 * with a real body are symbols -- a getter/setter body carries real
 * decisions that would otherwise count toward NOTHING (properties are
 * not symbols and class members have no enclosing function; the C#
 * accounting-hole note applies verbatim). The C side synthesizes the
 * honest qualified name `Prop.get` / `Prop.set` from the
 * property_declaration owner's variable_declaration identifier. A
 * body-less accessor (`get` = delegate to the backing field) never
 * matches. docs/LANGUAGES.md Kotlin section 2. */
static const char Q_KT_ACCESSORS[] = "(getter (function_body) @fbody) @fexpr"
                                     " (setter (function_body) @fbody) @fexpr";

/* Kotlin decision kinds (SPRINT-22, verified against the pinned grammar
 * AND a live parse, docs/LANGUAGES.md Kotlin section 4): if_expression
 * (else-if is a NESTED if_expression, the Java discipline),
 * when_entry (EVERY entry counts including the else arm -- the Rust
 * match_arm precedent: each arm is an explicit branch), for_statement,
 * while_statement, do_while_statement, catch_block (each handler is a
 * branch -- the Java catch discipline; finally is not a decision),
 * elvis_expression (Kotlin's conditional-expression spelling, the C#
 * `??` op precedent -- but the pinned grammar gives it its OWN node
 * type, so it is a KIND here), and the two boolean-operator containers
 * conjunction_expression / disjunction_expression (Kotlin has NO shared
 * binary_expression: `&&` and `||` are distinct node types, so the
 * ops mechanism -- which counts anonymous tokens inside ONE container
 * type -- cannot express them; as kinds each named node counts exactly
 * once per occurrence, the identical semantics, verified by the
 * hand-counted test). Preprocessor-free language: no more kinds. */
static const char *const CX_KINDS_KT[] = {
    "if_expression",          "when_entry",  "for_statement",    "while_statement",
    "do_while_statement",     "catch_block", "elvis_expression", "conjunction_expression",
    "disjunction_expression", NULL};

/* Kotlin symbol parameter lists: function_value_parameters carries the
 * named `parameter` nodes (lambda_parameters are the anonymous-function
 * shape and are not symbol parameters). */
static const char *const PL_KT[] = {"function_value_parameters", NULL};

/* Swift symbols (SPRINT-23, docs/LANGUAGES.md Swift section 2): the
 * WITH-BODY rule, the Kotlin discipline. A `function_declaration` with
 * its `function_body` field -- free functions, methods, extension
 * methods (the receiver is the EXTENSION declaration, not part of the
 * function: the declaration's own name field is the name -- probed),
 * static/class methods (the modifiers carry the keyword; the wrapper
 * adds no extent, the C++ template precedent), nested functions,
 * generic functions (type_parameters are separate children). Swift has
 * no prototypes, so every real definition carries a body; a body-less
 * declaration (protocol REQUIREMENTS) is NOT a symbol -- EXCEPT the
 * protocol default body: the pinned grammar parses a requirement's
 * default body as a SIBLING wrapper (a second protocol_function_declaration
 * node carrying `{ statements }`, a DIRECT protocol_body child with the
 * `body` FIELD -- probed twice: the wrapper is NOT nested inside its
 * requirement), so the fourth alternative pairs the requirement with
 * its ADJACENT body wrapper (the `.` anchor) and promotes the default
 * implementation to a symbol the same way Kotlin promoted interface
 * default bodies (the C# interface-default precedent: a body carries
 * decisions that would otherwise count toward nothing; the span is the
 * requirement's own extent -- the wrapper adds none).
 * `init_declaration` / `deinit_declaration` with a body ARE symbols --
 * the C# constructor_declaration precedent (SPRINT-13); the pinned
 * grammar gives `init` no readable name node (the name FIELD is the
 * anonymous `init` token), so the C side synthesizes the honest
 * `init` / `deinit` spellings. Lambdas (lambda_literal) are anonymous
 * constructs (the Ruby blocks precedent): NOT symbols, decisions count
 * inward. */
static const char Q_SW_FUNCTIONS[] =
    "(function_declaration (simple_identifier) @fname (function_body) @fbody) @fexpr"
    " (init_declaration (function_body) @fbody) @fexpr"
    " (deinit_declaration (function_body) @fbody) @fexpr"
    " (protocol_body (protocol_function_declaration (simple_identifier) @fname)"
    "   . (protocol_function_declaration (statements) @fbody)) @fexpr";

/* Swift top-level-statement fact (SPRINT-23, the C# 9 SPRINT-13
 * precedent): the program entry point of an executable module is the
 * file with TOP-LEVEL CODE -- Swift's spec lets exactly one file per
 * module carry top-level statements (main.swift for executables). The
 * pinned grammar has no global_statement wrapper: the statements sit
 * as DIRECT source_file children (probed: `print("run")` is a
 * call_expression child of source_file; a top-level `let` is a
 * property_declaration child and is a DECLARATION, not code). Each
 * alternative is one statement shape; the fact records the file as an
 * entry parse fact (the Q_CS_TOP shape, Swift spelling). THE
 * DIRECTIVE-CONDITION EXCLUSION: the pinned grammar flattens `#if`
 * compilation directives -- the condition call (`#if compiler(>=5.9)`
 * on every SPM manifest) is a DIRECT source_file call_expression
 * SIBLING of the directive node (probed), so the @stmt capture plus
 * the C-side directive-builtin callee set (canImport/compiler/swift/
 * os/arch/targetEnvironment/sourceLocation/module) keeps the version
 * check out: a condition is not code. A conditionally-compiled
 * STATEMENT still claims (it IS executable top-level code). */
static const char Q_SW_TOP[] = "(source_file (call_expression) @stmt)"
                               " (source_file (assignment) @stmt)"
                               " (source_file (for_statement) @stmt)"
                               " (source_file (while_statement) @stmt)"
                               " (source_file (repeat_while_statement) @stmt)"
                               " (source_file (if_statement) @stmt)"
                               " (source_file (guard_statement) @stmt)"
                               " (source_file (do_statement) @stmt)"
                               " (source_file (switch_statement) @stmt)"
                               " (source_file (control_transfer_statement) @stmt)";

/* Swift import channel (SPRINT-23): every import_declaration of the
 * file. The dotted module path lives in ONE `identifier` node whose
 * children are the `simple_identifier` SEGMENTS joined by anonymous
 * `.` tokens (probed -- unlike Kotlin's single-text identifier, the C
 * side joins the segment texts). The optional import KIND is an
 * ANONYMOUS child token (`class`/`func`/`struct`/`enum`/`protocol`/
 * `typealias`/`var`/`let` -- probed); `@testable` / `@_exported` ride
 * in modifiers > attribute and never change the module. The C side
 * classifies the shapes (swift_import_shape): the module is the FIRST
 * segment (the compilation target, the declared-fact key); a kind-
 * scoped import with 2+ segments names a MEMBER (the last segment --
 * the over-mark direction, bounded by the declared facts). The pinned
 * grammar has NO import-alias shape (probed boundary:
 * `import struct Kit.Tag as T` does not parse -- recorded, never
 * guessed). docs/LANGUAGES.md Swift section 5. */
static const char Q_SW_IMPORTS[] = "(import_declaration (identifier) @ipath) @ist";

/* Swift module surface (SPRINT-23): the named top-level declarations.
 * Swift has NO internal-linkage filter below `private`/`fileprivate`:
 * a private or fileprivate top-level declaration is file-visible only
 * and never surface (the C `static` rule's Swift spelling; both
 * keywords ride modifiers > visibility_modifier > ANONYMOUS tokens --
 * the Kotlin shape verbatim, probed). public/open/internal (the
 * default) surface. The C side checks the parent chain is the
 * source_file (top-level code files keep every declaration flat --
 * probed) and the declaration's modifiers. Captured: function
 * definitions (a top-level function always has a body), type
 * declarations (class/struct/enum/extension/actor are ALL
 * class_declaration nodes -- the anonymous declaration_kind token
 * carries the keyword, probed; extensions surface under their extended
 * type name, the Kotlin extension-function discipline), protocol
 * declarations, property declarations (the stored let/var globals AND
 * top-level code bindings; property accessors are the property's
 * parts, never surface), type aliases. Enum entries and type members
 * are not module surface (the type-nesting precedent).
 * docs/LANGUAGES.md Swift section 6. */
static const char Q_SW_EXPORTS[] = "(function_declaration) @fdef"
                                   " (class_declaration) @cdecl"
                                   " (protocol_declaration) @pdecl"
                                   " (property_declaration) @prop"
                                   " (typealias_declaration) @talias";

/* Swift comments (SPRINT-23): the pinned grammar's two comment node
 * kinds (the Java pair; block comments are `multiline_comment` -- the
 * external scanner token -- and they NEST in Swift, probed). The
 * shebang_line node is the shebang hint's source -- the shared
 * line-based shebang scan already covers it. */
static const char Q_SW_COMMENTS[] = "(comment) @comment (multiline_comment) @comment";

/* Swift accessors (SPRINT-23, the C# SPRINT-14 precedent): property
 * accessor bodies ARE symbols -- the accounting-hole note applies
 * verbatim (properties are not symbols and type members have no
 * enclosing function). The pinned grammar nests them INSIDE the
 * property_declaration (probed: computed_getter/computed_setter sit in
 * a computed_property wrapper under the property's `computed_value`
 * FIELD; willset_clause/didset_clause sit in a willset_didset_block --
 * a PARENT CLIMB, not Kotlin's sibling walk). The implicit getter
 * (`var x: Int { return 2 }`, no `get` keyword) puts its statements
 * DIRECTLY in the computed_property -- the third alternative promotes
 * it to the same `Prop.get` symbol. An accessor with no statements
 * (an empty `didSet { }`) never matches -- the body-less accessor
 * rule. The C side synthesizes `Prop.get` / `Prop.set` /
 * `Prop.willSet` / `Prop.didSet` from the owner property's name.
 * docs/LANGUAGES.md Swift section 2. */
static const char Q_SW_ACCESSORS[] = "(computed_getter (statements) @fbody) @fexpr"
                                     " (computed_setter (statements) @fbody) @fexpr"
                                     " (computed_property (statements) @fbody) @fexpr"
                                     " (willset_clause (statements) @fbody) @fexpr"
                                     " (didset_clause (statements) @fbody) @fexpr";

/* Swift decision kinds (SPRINT-23, verified against the pinned grammar
 * AND a live parse, docs/LANGUAGES.md Swift section 4): if_statement
 * (else-if is a NESTED if_statement -- probed, the Java discipline),
 * guard_statement (the early-exit branch is an explicit branch), the
 * ternary_expression (the C# conditional_expression precedent),
 * switch_entry (EVERY arm counts including the default arm -- the Rust
 * match_arm precedent), for_statement, while_statement,
 * repeat_while_statement (Swift's do-while), catch_block (each handler
 * is a branch -- the Java catch discipline), nil_coalescing_expression
 * (the C# `??` op precedent -- but the pinned grammar gives it its OWN
 * node type, so it is a KIND here, the Kotlin elvis_expression
 * precedent), and the two boolean-operator containers
 * conjunction_expression / disjunction_expression (Swift has NO shared
 * binary_expression: `&&` and `||` are distinct node types, so the ops
 * mechanism -- which counts anonymous tokens inside ONE container type
 * -- cannot express them; as kinds each named node counts exactly once
 * per occurrence, the identical semantics, verified by the
 * hand-counted test). `#if` compilation directives are NOT a decision
 * kind -- the C/C++/C# packs count no preprocessor conditional (the
 * recorded boundary). */
static const char *const CX_KINDS_SW[] = {"if_statement",
                                          "guard_statement",
                                          "ternary_expression",
                                          "switch_entry",
                                          "for_statement",
                                          "while_statement",
                                          "repeat_while_statement",
                                          "catch_block",
                                          "nil_coalescing_expression",
                                          "conjunction_expression",
                                          "disjunction_expression",
                                          NULL};

/* Swift symbol parameter lists: the `parameter` nodes are DIRECT
 * children of the declaration (there is NO parameter-list wrapper in
 * the pinned grammar -- probed), so the list IS the declaration
 * (find_param_list checks the root first). init_declaration carries
 * parameters the same way. */
static const char *const PL_SW[] = {"function_declaration", "init_declaration", NULL};

/* Scala symbols (SPRINT-24, docs/LANGUAGES.md Scala section 2): the
 * WITH-BODY rule, the Kotlin discipline -- `function_definition` is by
 * construction a `def` with a body (free functions, methods, extension
 * and given-body methods, nested and generic defs); the bodyless
 * `function_declaration` (trait/abstract members) is NOT a symbol (the
 * C prototype discipline; Kotlin took the same boundary). Lambdas
 * (lambda_expression) are anonymous constructs: NOT symbols, decisions
 * count inward. The body field is a wildcard capture -- the probed
 * body shapes are `block`, an expression, and the Scala 3
 * `indented_block`; a field-qualified `_` matches all three (the
 * query compiles against the pinned grammar, verified by the live
 * parse). */
static const char Q_SC_FUNCTIONS[] =
    "(function_definition name: (identifier) @fname body: (_) @fbody) @fexpr";

/* Scala import channel (SPRINT-24): the WHOLE import_declaration is the
 * fact -- the C side joins the dotted `path` field segments (the path
 * AS WRITTEN) and classifies the trailing child:
 *   namespace_wildcard        -> `a.b.*` (BOTH the Scala 2 `_` and the
 *                                Scala 3 `*` spellings parse to the same
 *                                node -- probed; the Java asterisk-marker
 *                                record, whole-package fan-out);
 *   namespace_selectors       -> ONE record per selector (the Rust
 *                                use-tree expansion precedent): an
 *                                `identifier` selector claims
 *                                `a.b.C` and binds `C`; a rename
 *                                (as_renamed_identifier /
 *                                arrow_renamed_identifier) binds the
 *                                ORIGINAL name -- the alias is local
 *                                surface detail, never a narrowing
 *                                predicate (the Kotlin alias note);
 *   as_renamed_identifier     -> the Scala 3 tail rename `a.b.C as C2`
 *                                claims `a.b.C` and binds `C`;
 *   plain path                -> `a.b.C`: the bind follows the Kotlin
 *                                rule -- ONE segment or TWO segments
 *                                bind the LAST segment (the unambiguous
 *                                single-type record; in Scala packages
 *                                are routinely two segments deep, e.g.
 *                                `import a.b.C` IS the canonical class
 *                                import); THREE or more segments keep a
 *                                NULL bind -- the AMBIGUOUS MEMBER
 *                                shape (`a.b.C.f` is the object-member
 *                                import: binding `f` would under-mark
 *                                the object `C` that the member reading
 *                                actually uses, so the claim stays a
 *                                whole-file claim, the C++ include
 *                                discipline; the resolver matches BOTH
 *                                declared readings).
 * docs/LANGUAGES.md Scala section 5. */
static const char Q_SC_IMPORTS[] = "(import_declaration) @ist";

/* Scala module surface (SPRINT-24): the named top-level declarations.
 * The C side checks the parent chain is the compilation_unit (Scala has
 * no wrapper node for a top-level declaration; class members and
 * nested declarations fail it -- the type-nesting precedent) and the
 * declaration's modifiers for `private` (the Kotlin `private` filter;
 * `protected` is package/superclass-visible and stays surface -- the
 * Kotlin treatment of `internal`). Captured: function definitions (a
 * top-level `def` always has a body), class / object / trait / enum
 * definitions (case/sealed/abstract are anonymous modifier tokens on
 * the SAME class_definition node -- probed), type aliases (`type X =
 * ...`; `typealias` is NOT Scala syntax -- the probe caught exactly
 * that contract mistake), top-level val/var definitions (the pattern
 * field's simple `identifier` shape; a tuple pattern like
 * `val (a, b) = ...` records NOTHING -- the extraction is the honest
 * simple-name shape, never guessed). given/extension definitions are
 * NOT surface (unnamed by construction or container nodes whose member
 * defs are the surface -- the Swift extension precedent).
 * docs/LANGUAGES.md Scala section 6. */
static const char Q_SC_EXPORTS[] = "(function_definition) @fdef"
                                   " (class_definition) @cdecl"
                                   " (object_definition) @odecl"
                                   " (trait_definition) @tdef"
                                   " (enum_definition) @edef"
                                   " (type_definition) @talias"
                                   " (val_definition) @pdecl"
                                   " (var_definition) @pdecl";

/* Scala package fact (SPRINT-24): the FIRST package_clause's
 * package_identifier text (segments joined by anonymous `.` tokens, the
 * path AS WRITTEN -- the consumer reads only the first match, the Go
 * package_clause channel). Chained packages (`package a { package b {
 * ... } }`) record the OUTER clause -- a recorded v1 boundary, never
 * guessed. The resolver v1-scala keys on it (the declared-fact
 * channel). docs/LANGUAGES.md Scala section 5. */
static const char Q_SC_PACKAGE[] = "(package_clause (package_identifier) @pkg)";

/* Scala comments (SPRINT-24): the pinned grammar's two comment node
 * kinds (the Java pair with Scala's spelling; block comments NEST --
 * the external scanner's tokens, probed). */
static const char Q_SC_COMMENTS[] = "(comment) @comment (block_comment) @comment";

/* Scala main fact (SPRINT-24): the TWO entry spellings, both
 * name-checked in C (the Rust/Kotlin branch shape):
 *   - a `function_definition` NAMED `main` carrying a parameters list
 *     inside an `object_definition` (the JVM entry shape; the object
 *     need not be top-level -- the honest over-claim direction, the
 *     C #ifdef note, OC006 severity INFO);
 *   - a `function_definition` carrying an `@main` annotation (the
 *     Scala 3 spelling; the annotation's name field is checked).
 * docs/LANGUAGES.md Scala section 6. */
static const char Q_SC_MAIN[] =
    "(object_definition (template_body (function_definition name: (identifier) @jname"
    " (parameters)))) @jdecl"
    " (function_definition (annotation name: (type_identifier) @jname)) @jdecl";

/* Scala decision kinds (SPRINT-24, verified against the pinned grammar
 * AND a live parse, docs/LANGUAGES.md Scala section 4): if_expression
 * (else-if is a NESTED if_expression, the Java discipline; Scala's `if`
 * is an expression AND the ternary -- one kind), case_clause (EVERY
 * match/catch arm counts including the wildcard arm -- the Rust
 * match_arm precedent; the catch's case_block arms cover the catch
 * decision so there is NO separate catch kind: counting both would
 * double-count every caught arm), for_expression, while_expression,
 * do_while_expression, and infix_expression -- which counts ONLY for
 * the boolean operators `&&` / `||` (the operator is a NAMED
 * operator_identifier child, so the shared ops mechanism -- which
 * counts ANONYMOUS tokens inside one container type -- cannot express
 * it; the Java switch-group family special case pattern applies: the
 * C side gates the kind on the operator field's text). `match`
 * itself is not a kind (its arms are); Scala 3 indented match arms
 * still parse as case_clause nodes (the same kind); enum cases
 * (simple_enum_case) are NOT decisions. */
static const char *const CX_KINDS_SC[] = {
    "if_expression",    "case_clause", "for_expression", "while_expression", "do_while_expression",
    "infix_expression", NULL};

/* Scala symbol parameter lists: the `parameters` wrapper node (the
 * find_param_list preorder is safe: a function-type parameter's type is
 * a `function_type` whose arguments are type nodes, never a `parameters`
 * node, so the first hit is the declaration's own). */
static const char *const PL_SC[] = {"parameters", NULL};

static const struct {
    const char *grammar;
    oc_lang_family family;
    const TSLanguage *(*lang_fn)(void);
    const char *q_functions;
    const char *q_imports;
    const char *q_exports;
    const char *const *cx_kinds;
    const char *const *cx_ops; /* anonymous operator tokens, NULL-terminated */
    const char *q_package;     /* per-file package-clause fact, or NULL */
    const char *q_comments;    /* NULL -> the shared Q_COMMENTS */
    const char *q_main;        /* per-file main-method fact, or NULL */
    const char *q_top;         /* per-file top-level-statements fact, or NULL */
    const char *q_bodyless;    /* SPRINT-14: bodyless-symbol query, or NULL (Java) */
    const char *q_accessors;   /* SPRINT-14: accessor query, or NULL (C#) */
    const char *cx_ops_node;   /* SPRINT-16: the binary-operator container node
                                * type, NULL -> the shared binary_expression
                                * default (Ruby: `binary`) */
} LANG_DESCRIPTORS[] = {
    {"javascript", OC_LANG_JS, tree_sitter_javascript, Q_JS_FUNCTIONS, Q_IMPORTS_JS, Q_EXPORTS_JS,
     CX_KINDS_JS, CX_OPS_JS, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {"typescript", OC_LANG_JS, tree_sitter_typescript, Q_JS_FUNCTIONS, Q_IMPORTS_JS, Q_EXPORTS_JS,
     CX_KINDS_JS, CX_OPS_JS, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {"tsx", OC_LANG_JS, tree_sitter_tsx, Q_JS_FUNCTIONS, Q_IMPORTS_JS, Q_EXPORTS_JS, CX_KINDS_JS,
     CX_OPS_JS, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {"python", OC_LANG_PY, tree_sitter_python, Q_PY_FUNCTIONS, Q_IMPORTS_PY, Q_EXPORTS_PY,
     CX_KINDS_PY, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {"go", OC_LANG_GO, tree_sitter_go, Q_GO_FUNCTIONS, Q_IMPORTS_GO, Q_EXPORTS_GO, CX_KINDS_GO,
     CX_OPS_GO, Q_GO_PACKAGE, NULL, NULL, NULL, NULL, NULL, NULL},
    {"java", OC_LANG_JAVA, tree_sitter_java, Q_JAVA_FUNCTIONS, Q_JAVA_IMPORTS, Q_JAVA_EXPORTS,
     CX_KINDS_JAVA, CX_OPS_JAVA, NULL, Q_JAVA_COMMENTS, NULL, NULL, Q_JAVA_BODYLESS, NULL, NULL},
    {"csharp", OC_LANG_CSHARP, tree_sitter_c_sharp, Q_CSHARP_FUNCTIONS, Q_CSHARP_IMPORTS,
     Q_CSHARP_EXPORTS, CX_KINDS_CSHARP, CX_OPS_CSHARP, NULL, NULL, Q_CSHARP_MAIN, Q_CSHARP_TOP,
     NULL, Q_CSHARP_ACCESSORS, NULL},
    {"rust", OC_LANG_RUST, tree_sitter_rust, Q_RUST_FUNCTIONS, Q_RUST_IMPORTS, Q_RUST_EXPORTS,
     CX_KINDS_RUST, CX_OPS_RUST, NULL, Q_RUST_COMMENTS, Q_RUST_MAIN, NULL, Q_RUST_BODYLESS, NULL,
     NULL},
    {"ruby", OC_LANG_RUBY, tree_sitter_ruby, Q_RUBY_FUNCTIONS, Q_RUBY_IMPORTS, Q_RUBY_EXPORTS,
     CX_KINDS_RUBY, CX_OPS_RUBY, NULL, Q_RUBY_COMMENTS, NULL, NULL, Q_RUBY_BODYLESS, NULL,
     "binary"},
    {"php", OC_LANG_PHP, tree_sitter_php, Q_PHP_FUNCTIONS, Q_PHP_IMPORTS, Q_PHP_EXPORTS,
     CX_KINDS_PHP, CX_OPS_PHP, NULL, NULL, NULL, NULL, Q_PHP_BODYLESS, NULL, NULL},
    {"c", OC_LANG_C, tree_sitter_c, Q_C_FUNCTIONS, Q_C_IMPORTS, Q_C_EXPORTS, CX_KINDS_C, CX_OPS_C,
     NULL, NULL, Q_C_MAIN, NULL, NULL, NULL, NULL},
    {"cpp", OC_LANG_CPP, tree_sitter_cpp, Q_CPP_FUNCTIONS, Q_CPP_IMPORTS, Q_CPP_EXPORTS,
     CX_KINDS_CPP, CX_OPS_CPP, NULL, NULL, Q_CPP_MAIN, NULL, NULL, NULL, NULL},
    {"kotlin", OC_LANG_KOTLIN, tree_sitter_kotlin, Q_KT_FUNCTIONS, Q_KT_IMPORTS, Q_KT_EXPORTS,
     CX_KINDS_KT, NULL, Q_KT_PACKAGE, Q_KT_COMMENTS, Q_KT_MAIN, NULL, NULL, Q_KT_ACCESSORS, NULL},
    {"swift", OC_LANG_SWIFT, tree_sitter_swift, Q_SW_FUNCTIONS, Q_SW_IMPORTS, Q_SW_EXPORTS,
     CX_KINDS_SW, NULL, NULL, Q_SW_COMMENTS, NULL, Q_SW_TOP, NULL, Q_SW_ACCESSORS, NULL},
    {"scala", OC_LANG_SCALA, tree_sitter_scala, Q_SC_FUNCTIONS, Q_SC_IMPORTS, Q_SC_EXPORTS,
     CX_KINDS_SC, NULL, Q_SC_PACKAGE, Q_SC_COMMENTS, Q_SC_MAIN, NULL, NULL, NULL, NULL},
};
static TSNode match_capture(TSQuery *q, const TSQueryMatch *m, const char *cap_name) {
    for (uint32_t i = 0; i < m->capture_count; i++) {
        uint32_t name_len = 0;
        const char *name = ts_query_capture_name_for_id(q, m->captures[i].index, &name_len);
        if (strlen(cap_name) == name_len && memcmp(name, cap_name, name_len) == 0)
            return m->captures[i].node;
    }
    TSNode null_node;
    memset(&null_node, 0, sizeof(null_node));
    return null_node;
}

static char *node_text(oc_arena *a, TSNode n, const char *src) {
    uint32_t start = ts_node_start_byte(n);
    uint32_t end = ts_node_end_byte(n);
    if (end < start)
        return NULL;
    return oc_arena_strndup(a, src + start, (size_t)(end - start));
}

/* ---- SPRINT-17 (B3): parameter signature extraction ----------------
 * Contract: docs/SPRINT-17.md section 2. Ordered (name, type, default)
 * per parameter; raw type text, NULL when undeclared; splat/rest/
 * variadic/unnamed/destructuring collapse to the "..." marker so arity
 * stays caller-visible while markers stay out of type/rename diffs. */

typedef struct param_vec {
    oc_param *items;
    size_t n, cap;
    int oom;
    oc_arena *a;
} param_vec;

static void pv_push(param_vec *v, const char *name, const char *type, int has_default) {
    if (v->oom)
        return;
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 4;
        oc_param *ni = (oc_param *)oc_arena_alloc(v->a, cap * sizeof(oc_param));
        if (!ni) {
            v->oom = 1;
            return;
        }
        if (v->n)
            memcpy(ni, v->items, v->n * sizeof(oc_param));
        v->items = ni;
        v->cap = cap;
    }
    oc_param *p = &v->items[v->n++];
    p->name = name;
    p->type = type;
    p->has_default = has_default;
}

static const char *field_text(oc_arena *a, TSNode n, const char *field, const char *src) {
    TSNode f = ts_node_child_by_field_name(n, field, (uint32_t)strlen(field));
    return ts_node_is_null(f) ? NULL : node_text(a, f, src);
}

/* First identifier-text descendant of `n` (used where a grammar wraps
 * the parameter name one level down, e.g. Java variable_declarator). */
static const char *first_identifier_text(oc_arena *a, TSNode n, const char *src) {
    if (ts_node_is_null(n))
        return NULL;
    uint32_t k = ts_node_child_count(n);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(n, i);
        if (strcmp(ts_node_type(c), "identifier") == 0)
            return node_text(a, c, src);
        const char *deep = first_identifier_text(a, c, src);
        if (deep)
            return deep;
    }
    return NULL;
}

static int node_is(TSNode n, const char *t) {
    return strcmp(ts_node_type(n), t) == 0;
}

/* TS type annotations arrive as `type_annotation` nodes whose text
 * starts with ":" -- trim it (and surrounding blanks) so type text is
 * comparable across languages that do not carry the colon. */
static const char *trim_type_colon(const char *t) {
    if (!t)
        return NULL;
    while (*t == ':' || *t == ' ' || *t == '\t')
        t++;
    return t;
}

/* First pre-order descendant of `root` whose type is one of `types`.
 * The function's own parameter list precedes any nested lambda's in
 * pre-order, so the first match is always the right one. */
static TSNode find_param_list(TSNode root, const char *const *types) {
    TSNode null_node;
    memset(&null_node, 0, sizeof(null_node));
    if (ts_node_is_null(root))
        return null_node;
    for (size_t i = 0; types[i]; i++)
        if (node_is(root, types[i]))
            return root;
    uint32_t k = ts_node_child_count(root);
    for (uint32_t i = 0; i < k; i++) {
        TSNode hit = find_param_list(ts_node_child(root, i), types);
        if (!ts_node_is_null(hit))
            return hit;
    }
    return null_node;
}

/* True when the parameter node carries a default: an assignment_pattern
 * child (JS), or an anonymous "=" child (the TS shapes where the value
 * sits directly on required_parameter). */
static int js_param_has_default(TSNode n) {
    uint32_t k = ts_node_child_count(n);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(n, i);
        if (node_is(c, "assignment_pattern"))
            return 1;
        if (!ts_node_is_named(c) && strcmp(ts_node_type(c), "=") == 0)
            return 1;
    }
    return 0;
}

static void extract_params_js(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "identifier")) {
            pv_push(v, node_text(a, c, src), NULL, 0);
        } else if (node_is(c, "assignment_pattern")) {
            const char *nm = field_text(a, c, "left", src);
            pv_push(v, nm ? nm : "...", NULL, 1);
        } else if (node_is(c, "required_parameter")) {
            const char *nm = field_text(a, c, "pattern", src);
            const char *ty = trim_type_colon(field_text(a, c, "type", src));
            pv_push(v, nm ? nm : "...", ty, js_param_has_default(c));
        } else if (node_is(c, "optional_parameter")) {
            const char *nm = field_text(a, c, "pattern", src);
            const char *ty = trim_type_colon(field_text(a, c, "type", src));
            pv_push(v, nm ? nm : "...", ty, 1);
        } else if (node_is(c, "rest_pattern")) {
            pv_push(v, "...", NULL, 0);
        } else {
            pv_push(v, "...", NULL, 0); /* unknown named shape: arity stays visible */
        }
    }
}

static void extract_params_py(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "identifier")) {
            pv_push(v, node_text(a, c, src), NULL, 0);
        } else if (node_is(c, "default_parameter") || node_is(c, "typed_default_parameter")) {
            const char *nm = field_text(a, c, "name", src);
            if (!nm)
                nm = first_identifier_text(a, c, src);
            const char *ty =
                node_is(c, "typed_default_parameter") ? field_text(a, c, "type", src) : NULL;
            pv_push(v, nm ? nm : "...", ty, 1);
        } else if (node_is(c, "typed_parameter")) {
            const char *nm = first_identifier_text(a, c, src);
            const char *ty = field_text(a, c, "type", src);
            pv_push(v, nm ? nm : "...", ty, 0);
        } else if (node_is(c, "list_splat_pattern") || node_is(c, "dictionary_splat_pattern")) {
            pv_push(v, "...", NULL, 0);
        } else {
            /* separator-shaped named nodes (bare *, **): no identifier
             * inside means it is a separator, not a parameter */
            if (first_identifier_text(a, c, src))
                pv_push(v, "...", NULL, 0);
        }
    }
}

static void extract_params_go(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "parameter_declaration")) {
            const char *ty = field_text(a, c, "type", src);
            /* Go groups names: `(a, b int)` is ONE declaration with two
             * name fields sharing the type -- expand to one param per
             * name so arity stays honest. */
            size_t names = 0;
            const char *first_name = NULL;
            for (uint32_t j = 0; j < ts_node_child_count(c); j++) {
                TSNode ch = ts_node_child(c, j);
                if (ts_node_is_named(ch) && node_is(ch, "identifier")) {
                    if (!first_name)
                        first_name = node_text(a, ch, src);
                    names++;
                }
            }
            if (!first_name && !ty)
                continue; /* empty declaration slot */
            if (names > 1) {
                for (uint32_t j = 0; j < ts_node_child_count(c); j++) {
                    TSNode ch = ts_node_child(c, j);
                    if (ts_node_is_named(ch) && node_is(ch, "identifier")) {
                        const char *nm = node_text(a, ch, src);
                        pv_push(v, nm ? nm : "...", ty, 0);
                    }
                }
            } else {
                pv_push(v, first_name ? first_name : "...", ty, 0);
            }
        } else if (node_is(c, "variadic_parameter_declaration")) {
            const char *nm = field_text(a, c, "name", src);
            const char *ty = field_text(a, c, "type", src);
            pv_push(v, nm ? nm : "...", ty, 0);
        } else {
            pv_push(v, "...", NULL, 0);
        }
    }
}

static void extract_params_java(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "formal_parameter")) {
            const char *ty = field_text(a, c, "type", src);
            TSNode vd = ts_node_child_by_field_name(c, "name", 4);
            /* java variable names are `identifier`; type names are
             * `type_identifier`, so the first identifier descendant is
             * the parameter name even when the name field is absent. */
            const char *nm = ts_node_is_null(vd) ? NULL : first_identifier_text(a, vd, src);
            if (!nm)
                nm = first_identifier_text(a, c, src);
            pv_push(v, nm ? nm : "...", ty, 0);
        } else if (node_is(c, "spread_parameter")) {
            const char *ty = field_text(a, c, "type", src);
            pv_push(v, "...", ty, 0);
        }
        /* receiver_parameter: implicit this, not caller-visible -- skip */
    }
}

static void extract_params_cs(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "parameter")) {
            const char *ty = field_text(a, c, "type", src);
            const char *nm = field_text(a, c, "name", src);
            /* tree-sitter-c-sharp v0.24: `int b = 2` is parameter(type,
             * name, "=" expression) -- the "=" is an anonymous token
             * child (the expression surfaces as its concrete type, so
             * scanning for it would miss); same detection as JS. */
            int has_default = 0;
            for (uint32_t j = 0; j < ts_node_child_count(c); j++) {
                TSNode ch = ts_node_child(c, j);
                if (!ts_node_is_named(ch) && strcmp(ts_node_type(ch), "=") == 0) {
                    has_default = 1;
                    break;
                }
            }
            pv_push(v, nm ? nm : "...", ty, has_default);
        } else if (node_is(c, "parameter_array")) {
            const char *ty = field_text(a, c, "type", src);
            pv_push(v, "...", ty, 0);
        }
    }
}

static void extract_params_rust(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "parameter")) {
            TSNode pat = ts_node_child_by_field_name(c, "pattern", 7);
            const char *ty = field_text(a, c, "type", src);
            const char *nm = (!ts_node_is_null(pat) && node_is(pat, "identifier"))
                                 ? node_text(a, pat, src)
                                 : "...";
            pv_push(v, nm, ty, 0);
        } else if (node_is(c, "self_parameter")) {
            pv_push(v, "self", NULL, 0);
        } else {
            pv_push(v, "...", NULL, 0);
        }
    }
}

/* PHP (SPRINT-19): the compat (B3) signature records the parameter name
 * WITHOUT the `$` sigil -- the `name` node child inside the grammar's
 * `variable_name` wrapper (whose own text carries the sigil). Raw type
 * text, and `has_default` from the `default_value` field (the value
 * sits behind an anonymous `=`, the C# detection). */
static const char *php_var_identifier(oc_arena *a, TSNode var, const char *src) {
    if (node_is(var, "name"))
        return node_text(a, var, src);
    uint32_t k = ts_node_child_count(var);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(var, i);
        if (ts_node_is_named(c) && node_is(c, "name"))
            return node_text(a, c, src);
    }
    return NULL;
}

/* The default is the default_value-FIELD child behind an anonymous
 * `=`; a node with no `=` child carries no default. */
static int php_param_has_default(TSNode n) {
    uint32_t k = ts_node_child_count(n);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(n, i);
        if (!ts_node_is_named(c) && strcmp(ts_node_type(c), "=") == 0)
            return 1;
    }
    return 0;
}

static void extract_params_php(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "simple_parameter") || node_is(c, "property_promotion_parameter")) {
            TSNode nm_node = ts_node_child_by_field_name(c, "name", 4);
            const char *ty = field_text(a, c, "type", src);
            const char *nm = ts_node_is_null(nm_node) ? NULL : php_var_identifier(a, nm_node, src);
            pv_push(v, nm ? nm : "...", ty, php_param_has_default(c));
        } else if (node_is(c, "variadic_parameter")) {
            const char *ty = field_text(a, c, "type", src);
            /* a variadic parameter collapses to the `...` marker: the
             * caller-visible arity still ends here (contract section 2) */
            pv_push(v, "...", ty, 0);
        } else {
            /* an unrecognized named shape collapses to ONE marker so
             * arity stays caller-visible */
            pv_push(v, "...", NULL, 0);
        }
    }
}

/* C (SPRINT-20): the identifier at the BOTTOM of a declarator chain --
 * the shared name-resolution for functions, typedefs and object
 * declarators. The chain is linked by `declarator` fields (verified by
 * the adoption probe): function_declarator, pointer_declarator,
 * attributed_declarator and array_declarator all carry one; the
 * PARENTHESIZED wrapper does NOT (the grammar packs its inner declarator
 * as a bare positional child -- seq('(', _declarator, ')')), so the walk
 * falls back to the first named child when the field is absent; the chain
 * ends at an identifier (objects and function names) or a type_identifier
 * (typedef names, `typedef struct point point_t`). The tag inside a
 * type-field specifier is NOT on the chain, so the two names never
 * collide. A chain that ends without either (gnu_asm_expression
 * declarators) resolves to NULL -- nothing is guessed.
 *
 * SPRINT-21 (the C++ adoption, probe-verified): the cpp grammar shares
 * the declarator-chain model and adds four shapes the walk handles
 * without changing ANY C behavior (none of the node kinds exist in a
 * tree-sitter-c tree):
 *   - field_identifier: the inline method's name field (a C tree's
 *     field_declaration_list does not exist);
 *   - qualified_identifier (`void S::f()`): the name is the `name` FIELD
 *     child -- the positional fallback would wrongly take the scope's
 *     namespace_identifier, so the field is checked explicitly;
 *   - destructor_name (`~S`): carried AS WRITTEN ("~S") -- returning the
 *     bare identifier would collide with the constructor of the same
 *     class;
 *   - operator_name (`operator==`): carried AS WRITTEN -- the node has
 *     no identifier child at all (both tokens are anonymous). */
static TSNode c_declarator_ident(TSNode n) {
    TSNode null_node;
    memset(&null_node, 0, sizeof(null_node));
    while (!ts_node_is_null(n)) {
        const char *t = ts_node_type(n);
        if (strcmp(t, "identifier") == 0 || strcmp(t, "type_identifier") == 0 ||
            strcmp(t, "field_identifier") == 0 || strcmp(t, "operator_name") == 0)
            return n;
        if (strcmp(t, "qualified_identifier") == 0) {
            TSNode name = ts_node_child_by_field_name(n, "name", 4);
            /* the name child may itself be a destructor_name: return it
             * and let the next iteration apply the destructor rule */
            return name;
        }
        if (strcmp(t, "destructor_name") == 0)
            return n; /* as written: "~S" */
        TSNode next = ts_node_child_by_field_name(n, "declarator", 10);
        if (ts_node_is_null(next)) {
            /* the parenthesized wrapper (and any other fieldless
             * declarator shape): its single named child IS the inner
             * declarator */
            uint32_t k = ts_node_child_count(n);
            for (uint32_t i = 0; i < k; i++) {
                TSNode c = ts_node_child(n, i);
                if (ts_node_is_named(c)) {
                    next = c;
                    break;
                }
            }
        }
        if (ts_node_is_null(next))
            return null_node;
        n = next;
    }
    return null_node;
}

/* C (SPRINT-20): the function's OWN parameter list. For the
 * function-pointer-return shape `int (*get_step(int n))(int)` the
 * declarator chain carries TWO function_declarators and BOTH parameter
 * lists are honest AST facts -- but they are NOT the same list: the
 * OUTER declarator is the returned function POINTER's type (its
 * parameter list `(int)` is the pointed-to signature, abstract), and the
 * INNER one (the declarator chain bottom, adjacent to the identifier) is
 * the function's own caller-visible list `(int n)`. The finder descends
 * the full declarator chain (positional fallback for the parenthesized
 * wrapper) and takes the LAST function_declarator -- the one adjacent to
 * the identifier. */
static TSNode c_own_param_list(TSNode fexpr) {
    TSNode null_node;
    memset(&null_node, 0, sizeof(null_node));
    TSNode n = ts_node_child_by_field_name(fexpr, "declarator", 10);
    TSNode last_fd = null_node;
    while (!ts_node_is_null(n)) {
        if (node_is(n, "function_declarator"))
            last_fd = n;
        if (strcmp(ts_node_type(n), "identifier") == 0 ||
            strcmp(ts_node_type(n), "type_identifier") == 0)
            break;
        TSNode next = ts_node_child_by_field_name(n, "declarator", 10);
        if (ts_node_is_null(next)) {
            /* the parenthesized wrapper: positional named child */
            uint32_t k = ts_node_child_count(n);
            for (uint32_t i = 0; i < k; i++) {
                TSNode c = ts_node_child(n, i);
                if (ts_node_is_named(c)) {
                    next = c;
                    break;
                }
            }
        }
        if (ts_node_is_null(next))
            break;
        n = next;
    }
    if (ts_node_is_null(last_fd))
        return null_node;
    return ts_node_child_by_field_name(last_fd, "parameters", 10);
}

/* C (SPRINT-20): the raw AS-WRITTEN type text of a parameter -- the
 * source slice from the parameter_declaration's start to the declarator's
 * start (the qualifier and the base type as written; the pointer is
 * declarator syntax and stays out), whitespace collapsed. An UNNAMED
 * (abstract) parameter has no declarator: the whole node's text is the
 * type, pointer shape included (`char *`). Never guessed. */
static char *c_param_type_text(oc_arena *a, TSNode decl, TSNode declarator, const char *src) {
    uint32_t start = ts_node_start_byte(decl);
    uint32_t end =
        ts_node_is_null(declarator) ? ts_node_end_byte(decl) : ts_node_start_byte(declarator);
    if (end < start)
        return NULL;
    char *out = oc_arena_alloc(a, (size_t)(end - start) + 1);
    if (!out)
        return NULL;
    size_t w = 0;
    int pending_space = 0;
    for (uint32_t i = start; i < end; i++) {
        char ch = src[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f') {
            pending_space = w > 0;
            continue;
        }
        if (pending_space) {
            out[w++] = ' ';
            pending_space = 0;
        }
        out[w++] = ch;
    }
    out[w] = '\0';
    return w > 0 ? out : NULL;
}

/* C (SPRINT-20): the compat (B3) signature of a parameter_declaration --
 * the name is the declarator chain's bottom identifier (UNNAMED and
 * abstract parameters have none: the honest `...` marker with the type
 * recorded, arity stays caller-visible); the type is the as-written
 * slice (c_param_type_text). A variadic parameter collapses to the
 * `...` marker (the PHP convention); K&R old-style lists carry bare
 * identifier NAMES (types are declared after the closing paren, the
 * probe verified) -- each pushes its name with the type honestly
 * unknown. */
static void extract_params_c(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "parameter_declaration")) {
            TSNode dn = ts_node_child_by_field_name(c, "declarator", 10);
            TSNode id = ts_node_is_null(dn) ? dn : c_declarator_ident(dn);
            const char *nm = ts_node_is_null(id) ? NULL : node_text(a, id, src);
            char *ty = c_param_type_text(a, c, dn, src);
            pv_push(v, nm ? nm : "...", ty, 0);
        } else if (node_is(c, "variadic_parameter")) {
            pv_push(v, "...", NULL, 0);
        } else if (node_is(c, "identifier")) {
            pv_push(v, node_text(a, c, src), NULL, 0);
        } else {
            /* an unrecognized named shape collapses to ONE marker so
             * arity stays caller-visible */
            pv_push(v, "...", NULL, 0);
        }
    }
}

/* C++ (SPRINT-21): the compat (B3) signature of one C++ parameter. The
 * C shapes carry over unchanged (parameter_declaration with the
 * declarator chain; the as-written type slice via c_param_type_text;
 * the bare `...` variadic_parameter). Two C++ additions, verified by
 * the adoption probe:
 *   - optional_parameter_declaration (`int b = 2`): C++'s default-argument
 *     shape -- the name and type slice work identically and the DEFAULT
 *     flag is recorded (the B3 signature's third slot), so a signature
 *     diff flags default-value changes;
 *   - variadic_parameter_declaration (`Args &&... args`): the parameter
 *     PACK -- it collapses to the `...` marker like every other
 *     non-ordinary form (arity stays caller-visible, markers stay out of
 *     name diffs, the PHP convention). */
static void extract_params_cpp(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "parameter_declaration")) {
            TSNode dn = ts_node_child_by_field_name(c, "declarator", 10);
            TSNode id = ts_node_is_null(dn) ? dn : c_declarator_ident(dn);
            /* operator/destructor name nodes carry no parameter name:
             * the honest marker (their text is not an identifier) */
            const char *nm = NULL;
            if (!ts_node_is_null(id) && strcmp(ts_node_type(id), "identifier") == 0)
                nm = node_text(a, id, src);
            char *ty = c_param_type_text(a, c, dn, src);
            pv_push(v, nm ? nm : "...", ty, 0);
        } else if (node_is(c, "optional_parameter_declaration")) {
            TSNode dn = ts_node_child_by_field_name(c, "declarator", 10);
            TSNode id = ts_node_is_null(dn) ? dn : c_declarator_ident(dn);
            const char *nm = NULL;
            if (!ts_node_is_null(id) && strcmp(ts_node_type(id), "identifier") == 0)
                nm = node_text(a, id, src);
            char *ty = c_param_type_text(a, c, dn, src);
            pv_push(v, nm ? nm : "...", ty, 1);
        } else if (node_is(c, "variadic_parameter") ||
                   node_is(c, "variadic_parameter_declaration")) {
            pv_push(v, "...", NULL, 0);
        } else {
            /* an unrecognized named shape collapses to ONE marker so
             * arity stays caller-visible */
            pv_push(v, "...", NULL, 0);
        }
    }
}

static int node_has_anon_child_text(TSNode n, const char *src, const char *text);

/* Kotlin parameter extraction (SPRINT-22, docs/LANGUAGES.md Kotlin
 * section 3): the pinned grammar's `parameter` node has NO fields -- the
 * name is the direct `simple_identifier` child, the type the first
 * type-ish child (user_type / nullable_type / not_nullable_type /
 * parenthesized_type / function_type, as written) and the default value
 * trails an anonymous `=` token (the C# `=` detection). The `vararg`
 * modifier wraps its parameter in a SIBLING `parameter_modifiers` node
 * (probed: the modifier is a separate child BEFORE the parameter), so a
 * vararg parameter collapses to the `...` marker (the C++ pack
 * convention: arity stays caller-visible, markers stay out of name
 * diffs); the parameter itself is still named/typed and is skipped only
 * as a distinct entry. An unrecognized shape collapses to ONE marker. */
static void extract_params_kotlin(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "parameter_modifiers"))
            continue; /* the vararg marker is handled with its parameter below */
        if (!node_is(c, "parameter"))
            continue;
        /* vararg: an adjacent parameter_modifiers sibling carrying the
         * anonymous `vararg` token (the probed shape) */
        int is_vararg = 0;
        if (i > 0) {
            TSNode prev = ts_node_child(list, i - 1);
            if (ts_node_is_named(prev) && node_is(prev, "parameter_modifiers")) {
                uint32_t mc = ts_node_child_count(prev);
                for (uint32_t j = 0; j < mc; j++) {
                    TSNode pm = ts_node_child(prev, j);
                    if (ts_node_is_named(pm) && node_is(pm, "parameter_modifier") &&
                        node_has_anon_child_text(pm, src, "vararg")) {
                        is_vararg = 1;
                        break;
                    }
                }
            }
        }
        const char *nm = NULL;
        char *ty = NULL;
        int has_default = 0;
        uint32_t pc = ts_node_child_count(c);
        for (uint32_t j = 0; j < pc; j++) {
            TSNode ch = ts_node_child(c, j);
            const char *t = ts_node_type(ch);
            if (ts_node_is_named(ch)) {
                if (!nm && strcmp(t, "simple_identifier") == 0) {
                    nm = node_text(a, ch, src);
                } else if (!ty && (strcmp(t, "user_type") == 0 || strcmp(t, "nullable_type") == 0 ||
                                   strcmp(t, "not_nullable_type") == 0 ||
                                   strcmp(t, "parenthesized_type") == 0 ||
                                   strcmp(t, "function_type") == 0)) {
                    ty = node_text(a, ch, src);
                }
            }
        }
        /* the default value trails the parameter at the LIST level: the
         * pinned grammar makes `b: Int = 2` a parameter followed by an
         * anonymous `=` + the expression as SIBLINGS of the parameter
         * inside function_value_parameters (probed) -- the next "=" at
         * list level marks THIS parameter defaulted */
        if (i + 1 < k) {
            TSNode next = ts_node_child(list, i + 1);
            if (!ts_node_is_named(next) && strcmp(ts_node_type(next), "=") == 0)
                has_default = 1;
        }
        if (is_vararg) {
            pv_push(v, "...", ty, 0);
        } else {
            pv_push(v, nm ? nm : "...", ty, has_default);
        }
    }
}

/* Swift parameter extraction (SPRINT-23, docs/LANGUAGES.md Swift
 * section 3): the pinned grammar's `parameter` node carries the
 * external_name/name fields -- the name is the `name` field (a direct
 * simple_identifier; an external argument label rides the separate
 * `external_name` field and stays OUT of the name: the signature the
 * caller writes after the call-site label is the local name, the
 * honest as-written choice), the type is the named child AFTER the
 * anonymous `:` token (the type ALSO carries the `name` field label in
 * the generated parser -- a grammar quirk the probe caught: field-name
 * lookup cannot distinguish them, so the extraction is positional,
 * skipping the parameter_modifiers wrapper), and the default value
 * trails the parameter AT THE LIST LEVEL (the declaration node IS the
 * list: an anonymous `=` SIBLING followed by the declaration's
 * `default_value` field -- the Kotlin shape verbatim, probed). A
 * trailing anonymous `...` child INSIDE the parameter marks the
 * variadic shape: the parameter collapses to the `...` marker (the
 * C++ pack convention: arity stays caller-visible, markers stay out of
 * name diffs). Unreadable shapes collapse to ONE marker (never
 * guessed). */
static void extract_params_swift(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c) || !node_is(c, "parameter"))
            continue;
        const char *nm = field_text(a, c, "name", src);
        char *ty = NULL;
        int has_default = 0;
        int is_vararg = 0;
        int seen_colon = 0;
        uint32_t pc = ts_node_child_count(c);
        for (uint32_t j = 0; j < pc; j++) {
            TSNode ch = ts_node_child(c, j);
            if (!ts_node_is_named(ch)) {
                const char *t = ts_node_type(ch);
                if (strcmp(t, ":") == 0)
                    seen_colon = 1;
                else if (strcmp(t, "...") == 0)
                    is_vararg = 1;
                continue;
            }
            if (seen_colon && !ty && !node_is(ch, "parameter_modifiers"))
                ty = node_text(a, ch, src);
        }
        /* the default value trails the parameter AT THE LIST LEVEL: the
         * next sibling is the anonymous `=` (the Kotlin shape) */
        if (i + 1 < k) {
            TSNode next = ts_node_child(list, i + 1);
            if (!ts_node_is_named(next) && strcmp(ts_node_type(next), "=") == 0)
                has_default = 1;
        }
        if (is_vararg) {
            pv_push(v, "...", ty, 0);
        } else {
            pv_push(v, nm ? nm : "...", ty, has_default);
        }
    }
}

/* Scala parameter extraction (SPRINT-24, docs/LANGUAGES.md Scala
 * section 3): the `parameter` node carries name/type/default_value
 * FIELDS (probed -- unlike Kotlin's field-less shape) and the class
 * variant (class_parameter) shares the same fields, so one extractor
 * serves both. A variadic parameter's type is a repeated_parameter_type
 * (the trailing `*` rides the type text) -- it collapses to the `...`
 * marker, the Kotlin vararg discipline. Implicit/using parameter
 * modifiers are anonymous tokens before the parameter node and are
 * invisible here. */
static void extract_params_scala(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c) || (!node_is(c, "parameter") && !node_is(c, "class_parameter")))
            continue;
        const char *nm = field_text(a, c, "name", src);
        char *ty = NULL;
        int is_vararg = 0;
        TSNode typ = ts_node_child_by_field_name(c, "type", 4);
        if (!ts_node_is_null(typ)) {
            TSNode inner = typ;
            /* the variadic shape: `items: String*` parses the type as a
             * repeated_parameter_type wrapper -- collapse to `...` */
            if (node_is(inner, "repeated_parameter_type")) {
                is_vararg = 1;
                inner = ts_node_child_by_field_name(inner, "type", 4);
            }
            ty = ts_node_is_null(inner) ? NULL : node_text(a, inner, src);
        }
        TSNode def = ts_node_child_by_field_name(c, "default_value", 13);
        int has_default = !ts_node_is_null(def);
        if (is_vararg) {
            pv_push(v, "...", ty, 0);
        } else {
            pv_push(v, nm ? nm : "...", ty, has_default);
        }
    }
}

static void extract_params_ruby(param_vec *v, TSNode list, oc_arena *a, const char *src) {
    uint32_t k = ts_node_child_count(list);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(list, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "identifier")) {
            pv_push(v, node_text(a, c, src), NULL, 0);
        } else if (node_is(c, "optional_parameter")) {
            /* tree-sitter-ruby v0.23.x: `b = 2` is optional_parameter
             * (fields name + value); no default_parameter node exists. */
            const char *nm = field_text(a, c, "name", src);
            pv_push(v, nm ? nm : "...", NULL, 1);
        } else if (node_is(c, "keyword_parameter")) {
            const char *nm = field_text(a, c, "name", src);
            TSNode val = ts_node_child_by_field_name(c, "value", 5);
            pv_push(v, nm ? nm : "...", NULL, ts_node_is_null(val) ? 0 : 1);
        } else if (node_is(c, "splat_parameter") || node_is(c, "hash_splat_parameter") ||
                   node_is(c, "block_parameter") || node_is(c, "forward_parameter") ||
                   node_is(c, "destructured_parameter")) {
            pv_push(v, "...", NULL, 0);
        } else {
            pv_push(v, "...", NULL, 0);
        }
    }
}
static const char *const PL_JS[] = {"formal_parameters", NULL};
static const char *const PL_PY[] = {"parameters", NULL};
static const char *const PL_GO[] = {"parameter_list", NULL};
static const char *const PL_JAVA[] = {"formal_parameters", NULL};
static const char *const PL_CS[] = {"parameter_list", NULL};
static const char *const PL_RUST[] = {"parameters", NULL};
static const char *const PL_RUBY[] = {"method_parameters", NULL};
static const char *const PL_PHP[] = {"formal_parameters", NULL};
static const char *const PL_C[] = {"parameter_list", NULL};

/* Extracts the signature of the function whose defining construct is
 * `fexpr`. Returns 1 on success (params may be NULL/0 when the family
 * has no parameter list shape in the tree); 0 on arena exhaustion. */
static int extract_params(oc_arena *a, TSNode fexpr, oc_lang_family family, const char *src,
                          oc_param **out_params, size_t *out_n) {
    *out_params = NULL;
    *out_n = 0;
    const char *const *types;
    void (*fill)(param_vec *, TSNode, oc_arena *, const char *);
    switch (family) {
    case OC_LANG_JS:
        types = PL_JS;
        fill = extract_params_js;
        break;
    case OC_LANG_PY:
        types = PL_PY;
        fill = extract_params_py;
        break;
    case OC_LANG_GO:
        types = PL_GO;
        fill = extract_params_go;
        break;
    case OC_LANG_JAVA:
        types = PL_JAVA;
        fill = extract_params_java;
        break;
    case OC_LANG_CSHARP:
        types = PL_CS;
        fill = extract_params_cs;
        break;
    case OC_LANG_RUST:
        types = PL_RUST;
        fill = extract_params_rust;
        break;
    case OC_LANG_RUBY:
        types = PL_RUBY;
        fill = extract_params_ruby;
        break;
    case OC_LANG_PHP:
        types = PL_PHP;
        fill = extract_params_php;
        break;
    case OC_LANG_C:
        /* the C family bypasses the generic preorder finder: the
         * function-pointer-return shape carries TWO parameter_lists and
         * the honest one is the OUTER function's (c_own_param_list). */
        types = PL_C;
        fill = extract_params_c;
        break;
    case OC_LANG_CPP:
        /* C++ (SPRINT-21) shares the C machinery: c_own_param_list
         * descends the (possibly qualified) declarator chain and takes
         * the LAST function_declarator -- the one adjacent to the
         * identifier -- and extract_params_cpp adds the C++ parameter
         * shapes (default arguments, parameter packs) on top. */
        types = PL_C;
        fill = extract_params_cpp;
        break;
    case OC_LANG_KOTLIN:
        /* Kotlin (SPRINT-22): the declaration's own
         * function_value_parameters (find_param_list's preorder is safe:
         * the receiver type carries no parameter list of its own; a
         * function-type parameter's type is a nested function_type whose
         * function_type_parameters is a DIFFERENT node kind, so the
         * first function_value_parameters hit is the declaration's own). */
        types = PL_KT;
        fill = extract_params_kotlin;
        break;
    case OC_LANG_SWIFT:
        /* Swift (SPRINT-23): parameters are DIRECT children of the
         * declaration -- there is no parameter-list wrapper (probed), so
         * find_param_list matches fexpr ITSELF (the root-first check);
         * function-type parameters nest their own shapes DEEPER, never
         * as a parameter of the same names. */
        types = PL_SW;
        fill = extract_params_swift;
        break;
    case OC_LANG_SCALA:
        /* Scala (SPRINT-24): the `parameters` wrapper node (the Kotlin
         * find_param_list discipline; a function-type parameter's type
         * is a function_type whose arguments are type nodes, never a
         * parameters node, so the first hit is the declaration's own). */
        types = PL_SC;
        fill = extract_params_scala;
        break;
    default:
        return 1;
    }
    TSNode list;
    if (family == OC_LANG_C || family == OC_LANG_CPP) {
        list = c_own_param_list(fexpr);
    } else {
        list = find_param_list(fexpr, types);
    }
    if (ts_node_is_null(list))
        return 1; /* no recognizable list: signature unknown, honest shape */
    param_vec v;
    memset(&v, 0, sizeof(v));
    v.a = a;
    fill(&v, list, a, src);
    if (v.oom)
        return 0;
    *out_params = v.items;
    *out_n = v.n;
    return 1;
}

/*
 * Parse-error detection (BUG-1 fix): a tree containing ERROR or MISSING
 * nodes is a PARTIAL tree. Treating it as clean is the silent-partial-
 * analysis failure mode named in plan v1.1 -- a clean report must never
 * mean "nothing was analyzed". Clean files pay exactly one
 * ts_node_has_error call; the full walk runs only when errors exist.
 *
 * The walk is an iterative DFS over a linked stack served from the
 * analysis arena with a free list, so arena growth is bounded by the
 * peak frontier, not the whole tree. An arena allocation failure
 * degrades to reporting the error at the root span -- it never degrades
 * to silently passing.
 */
typedef struct err_frame {
    TSNode node;
    struct err_frame *next;
} err_frame;

static void diagnose_tree(oc_arena *a, TSNode root, oc_parse_diagnostics *diag) {
    memset(diag, 0, sizeof(*diag));
    if (!ts_node_has_error(root))
        return;

    diag->had_error = 1;
    err_frame *free_list = NULL;
    err_frame *stack = NULL;
    size_t counted = 0;

    err_frame *root_frame = (err_frame *)oc_arena_alloc(a, sizeof(*root_frame));
    if (!root_frame)
        goto oom_root;
    root_frame->node = root;
    root_frame->next = NULL;
    stack = root_frame;

    while (stack) {
        err_frame *cur = stack;
        stack = cur->next;
        TSNode n = cur->node;
        cur->next = free_list;
        free_list = cur;

        if (ts_node_is_error(n) || ts_node_is_missing(n)) {
            if (counted == 0) {
                diag->first_line = (size_t)ts_node_start_point(n).row + 1;
                diag->first_col = (size_t)ts_node_start_point(n).column + 1;
            }
            counted++;
        }

        uint32_t n_children = ts_node_child_count(n);
        for (uint32_t i = 0; i < n_children; i++) {
            err_frame *nf = free_list;
            if (nf) {
                free_list = nf->next;
            } else {
                nf = (err_frame *)oc_arena_alloc(a, sizeof(*nf));
                if (!nf) {
                    /* Out of arena mid-walk: keep the honest count so far;
                     * if we never reached an error node, report the root. */
                    if (counted > 0) {
                        diag->error_nodes = counted;
                        return;
                    }
                    goto oom_root;
                }
            }
            nf->node = ts_node_child(n, i);
            nf->next = stack;
            stack = nf;
        }
    }
    diag->error_nodes = counted;
    return;

oom_root:
    diag->error_nodes = 1;
    diag->first_line = (size_t)ts_node_start_point(root).row + 1;
    diag->first_col = (size_t)ts_node_start_point(root).column + 1;
}

oc_parser *oc_parser_ts_new(oc_arena *arena, oc_status *st) {
    oc_parser *p = (oc_parser *)oc_arena_alloc(arena, sizeof(oc_parser));
    if (!p) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory creating parser");
        return NULL;
    }
    memset(p, 0, sizeof(*p));
    p->backend_name = "tree-sitter";
    p->arena = arena;

    p->parser = ts_parser_new();
    if (!p->parser) {
        oc_status_set(st, OC_ERR_INTERNAL, "ts_parser_new failed");
        return NULL;
    }

    size_t n_desc = sizeof(LANG_DESCRIPTORS) / sizeof(LANG_DESCRIPTORS[0]);
    for (size_t i = 0; i < n_desc && i < OC_PARSER_MAX_LANGS; i++) {
        struct oc_lang_entry *e = &p->langs[p->n_langs];
        e->grammar = LANG_DESCRIPTORS[i].grammar;
        e->lang = LANG_DESCRIPTORS[i].lang_fn();

        if (!ts_parser_set_language(p->parser, e->lang)) {
            oc_status_set(st, OC_ERR_INTERNAL,
                          "grammar ABI mismatch for '%s' (grammar built for a "
                          "different tree-sitter language version?)",
                          e->grammar);
            return NULL;
        }

        uint32_t err_off = 0;
        TSQueryError err_type = TSQueryErrorNone;
        e->q_functions =
            ts_query_new(e->lang, LANG_DESCRIPTORS[i].q_functions,
                         (uint32_t)strlen(LANG_DESCRIPTORS[i].q_functions), &err_off, &err_type);
        if (!e->q_functions) {
            oc_status_set(st, OC_ERR_INTERNAL,
                          "functions query failed to compile for '%s' "
                          "(err_type=%d, offset=%u)",
                          e->grammar, (int)err_type, err_off);
            return NULL;
        }
        const char *q_comments_src =
            LANG_DESCRIPTORS[i].q_comments ? LANG_DESCRIPTORS[i].q_comments : Q_COMMENTS;
        e->q_comments = ts_query_new(e->lang, q_comments_src, (uint32_t)strlen(q_comments_src),
                                     &err_off, &err_type);
        if (!e->q_comments) {
            oc_status_set(st, OC_ERR_INTERNAL, "comments query failed to compile for '%s'",
                          e->grammar);
            return NULL;
        }
        e->q_imports =
            ts_query_new(e->lang, LANG_DESCRIPTORS[i].q_imports,
                         (uint32_t)strlen(LANG_DESCRIPTORS[i].q_imports), &err_off, &err_type);
        if (!e->q_imports) {
            oc_status_set(st, OC_ERR_INTERNAL,
                          "imports query failed to compile for '%s' "
                          "(err_type=%d, offset=%u)",
                          e->grammar, (int)err_type, err_off);
            return NULL;
        }
        e->q_exports =
            ts_query_new(e->lang, LANG_DESCRIPTORS[i].q_exports,
                         (uint32_t)strlen(LANG_DESCRIPTORS[i].q_exports), &err_off, &err_type);
        if (!e->q_exports) {
            oc_status_set(st, OC_ERR_INTERNAL,
                          "exports query failed to compile for '%s' "
                          "(err_type=%d, offset=%u)",
                          e->grammar, (int)err_type, err_off);
            return NULL;
        }
        e->q_exports_cjs = NULL;
        if (LANG_DESCRIPTORS[i].family == OC_LANG_JS) {
            /* BUGFIX N-3: the CommonJS export channel compiles for the
             * whole JS family (javascript / typescript / tsx). */
            e->q_exports_cjs = ts_query_new(
                e->lang, Q_EXPORTS_JS_CJS, (uint32_t)strlen(Q_EXPORTS_JS_CJS), &err_off, &err_type);
            if (!e->q_exports_cjs) {
                oc_status_set(st, OC_ERR_INTERNAL,
                              "CommonJS exports query failed to compile for '%s' "
                              "(err_type=%d, offset=%u)",
                              e->grammar, (int)err_type, err_off);
                return NULL;
            }
        }
        e->q_package = NULL;
        if (LANG_DESCRIPTORS[i].q_package) {
            e->q_package =
                ts_query_new(e->lang, LANG_DESCRIPTORS[i].q_package,
                             (uint32_t)strlen(LANG_DESCRIPTORS[i].q_package), &err_off, &err_type);
            if (!e->q_package) {
                oc_status_set(st, OC_ERR_INTERNAL,
                              "package query failed to compile for '%s' "
                              "(err_type=%d, offset=%u)",
                              e->grammar, (int)err_type, err_off);
                return NULL;
            }
        }
        e->q_main = NULL;
        if (LANG_DESCRIPTORS[i].q_main) {
            e->q_main =
                ts_query_new(e->lang, LANG_DESCRIPTORS[i].q_main,
                             (uint32_t)strlen(LANG_DESCRIPTORS[i].q_main), &err_off, &err_type);
            if (!e->q_main) {
                oc_status_set(st, OC_ERR_INTERNAL,
                              "main-fact query failed to compile for '%s' "
                              "(err_type=%d, offset=%u)",
                              e->grammar, (int)err_type, err_off);
                return NULL;
            }
        }
        e->q_top = NULL;
        if (LANG_DESCRIPTORS[i].q_top) {
            e->q_top =
                ts_query_new(e->lang, LANG_DESCRIPTORS[i].q_top,
                             (uint32_t)strlen(LANG_DESCRIPTORS[i].q_top), &err_off, &err_type);
            if (!e->q_top) {
                oc_status_set(st, OC_ERR_INTERNAL,
                              "top-level-statements query failed to compile for '%s' "
                              "(err_type=%d, offset=%u)",
                              e->grammar, (int)err_type, err_off);
                return NULL;
            }
        }
        e->q_bodyless = NULL;
        if (LANG_DESCRIPTORS[i].q_bodyless) {
            e->q_bodyless =
                ts_query_new(e->lang, LANG_DESCRIPTORS[i].q_bodyless,
                             (uint32_t)strlen(LANG_DESCRIPTORS[i].q_bodyless), &err_off, &err_type);
            if (!e->q_bodyless) {
                oc_status_set(st, OC_ERR_INTERNAL,
                              "bodyless-symbol query failed to compile for '%s' "
                              "(err_type=%d, offset=%u)",
                              e->grammar, (int)err_type, err_off);
                return NULL;
            }
        }
        e->q_accessors = NULL;
        if (LANG_DESCRIPTORS[i].q_accessors) {
            e->q_accessors = ts_query_new(e->lang, LANG_DESCRIPTORS[i].q_accessors,
                                          (uint32_t)strlen(LANG_DESCRIPTORS[i].q_accessors),
                                          &err_off, &err_type);
            if (!e->q_accessors) {
                oc_status_set(st, OC_ERR_INTERNAL,
                              "accessor query failed to compile for '%s' "
                              "(err_type=%d, offset=%u)",
                              e->grammar, (int)err_type, err_off);
                return NULL;
            }
        }
        e->family = LANG_DESCRIPTORS[i].family;
        e->cx_ops_node = LANG_DESCRIPTORS[i].cx_ops_node;
        e->cx_kinds = LANG_DESCRIPTORS[i].cx_kinds;
        e->cx_ops = LANG_DESCRIPTORS[i].cx_ops;
        p->n_langs++;
        ts_parser_reset(p->parser);
    }
    oc_status_clear(st);
    return p;
}

void oc_parser_ts_free(oc_parser *p) {
    if (!p)
        return;
    for (size_t i = 0; i < p->n_langs; i++) {
        if (p->langs[i].q_functions)
            ts_query_delete(p->langs[i].q_functions);
        if (p->langs[i].q_comments)
            ts_query_delete(p->langs[i].q_comments);
        if (p->langs[i].q_imports)
            ts_query_delete(p->langs[i].q_imports);
        if (p->langs[i].q_exports)
            ts_query_delete(p->langs[i].q_exports);
        if (p->langs[i].q_exports_cjs)
            ts_query_delete(p->langs[i].q_exports_cjs);
        if (p->langs[i].q_package)
            ts_query_delete(p->langs[i].q_package);
        if (p->langs[i].q_main)
            ts_query_delete(p->langs[i].q_main);
        if (p->langs[i].q_top)
            ts_query_delete(p->langs[i].q_top);
        if (p->langs[i].q_bodyless)
            ts_query_delete(p->langs[i].q_bodyless);
        if (p->langs[i].q_accessors)
            ts_query_delete(p->langs[i].q_accessors);
    }
    if (p->parser)
        ts_parser_delete(p->parser);
    p->parser = NULL;
}

static struct oc_lang_entry *find_lang(oc_parser *p, const char *grammar) {
    for (size_t i = 0; i < p->n_langs; i++) {
        if (strcmp(p->langs[i].grammar, grammar) == 0)
            return &p->langs[i];
    }
    return NULL;
}

/*
 * Cyclomatic complexity: decisions + operators inside the function body
 * (nested closures count toward the enclosing function -- inclusive by
 * design, so per-function numbers are additive-safe). Same arena-stack
 * DFS pattern as diagnose_tree; an arena OOM yields 0 = "not computed".
 */
static int kind_in_list(const char *type, const char *const *list) {
    for (size_t i = 0; list[i]; i++) {
        if (strcmp(type, list[i]) == 0)
            return 1;
    }
    return 0;
}

/* Forward decl: the Java switch group check needs the anonymous-token
 * helper (defined with the module-surface section below). */
static int node_has_anon_child_text(TSNode n, const char *src, const char *text);

static size_t count_complexity(oc_arena *a, TSNode body, const char *const *kinds,
                               const char *const *ops, const char *ops_node, const char *src,
                               oc_lang_family family) {
    size_t count = 0;
    err_frame *free_list = NULL;
    err_frame *stack = NULL;

    err_frame *root = (err_frame *)oc_arena_alloc(a, sizeof(*root));
    if (!root)
        return 0; /* OOM: report "not computed", never a wrong number */
    root->node = body;
    root->next = NULL;
    stack = root;

    while (stack) {
        err_frame *cur = stack;
        stack = cur->next;
        TSNode n = cur->node;
        cur->next = free_list;
        free_list = cur;

        const char *type = ts_node_type(n);
        /* SPRINT-16 (Ruby) hardening: decision kinds are matched on NAMED
         * nodes only. The pinned Ruby grammar labels the anonymous KEYWORD
         * tokens with the same type strings as the constructs they open
         * (`if`, `when`, `rescue`, ...) -- without the named check every
         * Ruby count would be roughly double. Verified by inspection that
         * no other language's kind list matches an anonymous node type, so
         * the existing tables are unaffected (the ops loop below keeps its
         * own explicit anonymous-token read). */
        if (ts_node_is_named(n) && kind_in_list(type, kinds)) {
            /* Java (SPRINT-12): a classic switch group counts ONLY when
             * it carries a `case` label -- a default-only group is not a
             * decision (docs/LANGUAGES.md Java section). The label is a
             * NAMED `switch_label` child (verified against the pinned
             * grammar's node-types); its `case`/`default` head is an
             * anonymous token. */
            int is_case_group = 1;
            if (family == OC_LANG_JAVA && strcmp(type, "switch_block_statement_group") == 0) {
                is_case_group = 0;
                uint32_t gnc = ts_node_child_count(n);
                for (uint32_t gi = 0; gi < gnc; gi++) {
                    TSNode gc = ts_node_child(n, gi);
                    if (!ts_node_is_named(gc) || strcmp(ts_node_type(gc), "switch_label") != 0)
                        continue;
                    if (node_has_anon_child_text(gc, src, "case")) {
                        is_case_group = 1;
                        break;
                    }
                }
            }
            /* C# (SPRINT-13): a switch_section counts ONLY when it
             * carries a `case` label -- a default-only section is not a
             * decision (docs/LANGUAGES.md C# section 4). The label's
             * `case` head is an ANONYMOUS token directly on the section
             * (verified against the pinned grammar: no label wrapper
             * node). */
            if (family == OC_LANG_CSHARP && strcmp(type, "switch_section") == 0)
                is_case_group = node_has_anon_child_text(n, src, "case");
            /* C and C++ (SPRINT-20/21): a `default:` arm is the SAME
             * case_statement node WITHOUT the `value` field (verified
             * against both pinned grammars; the cpp grammar inlines the
             * C statement nodes) -- not a decision, the gocyclo
             * discipline (docs/LANGUAGES.md C/C++ sections 4). */
            if ((family == OC_LANG_C || family == OC_LANG_CPP) &&
                strcmp(type, "case_statement") == 0)
                is_case_group = !ts_node_is_null(ts_node_child_by_field_name(n, "value", 5));
            /* Scala (SPRINT-24): an infix_expression counts ONLY when
             * its `operator` field (a NAMED operator_identifier node --
             * probed) carries `&&` or `||`; every other infix operator
             * (+, -, ==, ...) is not a decision. The shared ops
             * mechanism reads ANONYMOUS tokens and cannot express this
             * container, so the kind is gated here (the Java
             * switch-group family-special-case pattern). */
            if (family == OC_LANG_SCALA && strcmp(type, "infix_expression") == 0) {
                is_case_group = 0;
                TSNode op = ts_node_child_by_field_name(n, "operator", 8);
                if (!ts_node_is_null(op)) {
                    uint32_t s = ts_node_start_byte(op), e = ts_node_end_byte(op);
                    size_t len = (size_t)(e - s);
                    if ((len == 2 && memcmp(src + s, "&&", 2) == 0) ||
                        (len == 2 && memcmp(src + s, "||", 2) == 0))
                        is_case_group = 1;
                }
            }
            if (is_case_group)
                count++;
        }
        if (ops && strcmp(type, ops_node && ops_node[0] ? ops_node : "binary_expression") == 0) {
            uint32_t nc = ts_node_child_count(n);
            for (uint32_t i = 0; i < nc; i++) {
                TSNode c = ts_node_child(n, i);
                if (ts_node_is_named(c))
                    continue;
                uint32_t s = ts_node_start_byte(c), e = ts_node_end_byte(c);
                size_t len = (size_t)(e - s);
                for (size_t k = 0; ops[k]; k++) {
                    if (strlen(ops[k]) == len && memcmp(src + s, ops[k], len) == 0)
                        count++;
                }
            }
        }

        uint32_t n_children = ts_node_child_count(n);
        for (uint32_t i = 0; i < n_children; i++) {
            err_frame *nf = free_list;
            if (nf) {
                free_list = nf->next;
            } else {
                nf = (err_frame *)oc_arena_alloc(a, sizeof(*nf));
                if (!nf)
                    return count; /* partial count is still >= full-tree minimum (1 added by caller)
                                   */
            }
            nf->node = ts_node_child(n, i);
            nf->next = stack;
            stack = nf;
        }
    }
    return count;
}

/* JS import sources arrive as quoted strings; strip the outer quotes. */
static char *string_payload(oc_arena *a, TSNode n, const char *src) {
    uint32_t start = ts_node_start_byte(n);
    uint32_t end = ts_node_end_byte(n);
    if (end <= start)
        return NULL;
    const char *s = src + start;
    size_t len = (size_t)(end - start);
    if (len >= 2 && (s[0] == '\'' || s[0] == '"' || s[0] == '`') && s[len - 1] == s[0]) {
        s += 1;
        len -= 2;
    }
    return oc_arena_strndup(a, s, len);
}

/* ------------------------------------------------ module surface ------ */

/* --- CommonJS export extraction (BUGFIX N-3) ---------------------------
 * The helpers below classify what the Q_EXPORTS_JS_CJS query matched.
 * Contract: record every HONESTLY readable CommonJS export name; record
 * NOTHING for shapes whose names cannot be known without guessing. */

typedef enum {
    CJS_TARGET_NONE = 0, /* not a CommonJS export target */
    CJS_TARGET_WHOLE,    /* module.exports = rhs / exports = rhs */
    CJS_TARGET_MEMBER    /* module.exports.name = rhs / exports.name = rhs */
} cjs_target_kind;

/* Classifies a CJS assignment LEFT member_expression (by its exact
 * source text): CJS_TARGET_MEMBER sets *name to the exported name,
 * CJS_TARGET_WHOLE means the RHS shape decides, CJS_TARGET_NONE rejects
 * everything else (nested member chains like module.exports.a.b, foreign
 * objects like foo.exports, unwritable text). */
static cjs_target_kind cjs_classify_left(TSNode left, const char *src, char **name, oc_arena *a) {
    *name = NULL;
    char *t = node_text(a, left, src);
    if (!t)
        return CJS_TARGET_NONE;
    if (strcmp(t, "module.exports") == 0 || strcmp(t, "exports") == 0)
        return CJS_TARGET_WHOLE;
    static const char MODP[] = "module.exports.";
    static const char EXPP[] = "exports.";
    const char *rest = NULL;
    if (strncmp(t, MODP, sizeof(MODP) - 1) == 0)
        rest = t + sizeof(MODP) - 1;
    else if (strncmp(t, EXPP, sizeof(EXPP) - 1) == 0)
        rest = t + sizeof(EXPP) - 1;
    if (!rest || rest[0] == '\0' || strchr(rest, '.') != NULL)
        return CJS_TARGET_NONE; /* nested chain: not a one-level export name */
    *name = oc_arena_strdup(a, rest);
    return *name ? CJS_TARGET_MEMBER : CJS_TARGET_NONE;
}

/* Dedup within one file's CJS channel (order-stable, names are
 * arena-owned): 1 = recorded (or already present), 0 = OOM. */
static int cjs_seen_push(char ***seen, size_t *n, size_t *cap, const char *name) {
    for (size_t i = 0; i < *n; i++)
        if (strcmp((*seen)[i], name) == 0)
            return 1; /* duplicate within the file: already recorded */
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 16;
        char **ns = (char **)realloc(*seen, nc * sizeof(*ns));
        if (!ns)
            return 0;
        *seen = ns;
        *cap = nc;
    }
    (*seen)[(*n)++] = (char *)name;
    return 1;
}

/* Extracts the honest names of an object literal used as a CJS export
 * value: pair keys (identifier or quoted -- quoted keys are unquoted
 * via string_payload), method_definition names, shorthand identifiers.
 * Computed keys and numeric keys are skipped, never guessed. Outputs:
 * *n_pushed = names recorded; *opaque = 1 when the literal spreads an
 * unknowable source (spread_element), so the caller can fall back to
 * the "default" pseudo-name. Returns 0 only on allocation failure. */
static int cjs_push_object_keys(oc_arena *a, TSNode obj, const char *src, const char *path,
                                size_t line, oc_export_vec *exports, char ***seen, size_t *n_seen,
                                size_t *cap_seen, int *oom, int *n_pushed, int *opaque) {
    *n_pushed = 0;
    *opaque = 0;
    uint32_t nc = ts_node_child_count(obj);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(obj, i);
        if (!ts_node_is_named(c))
            continue;
        const char *t = ts_node_type(c);
        char *name = NULL;
        if (strcmp(t, "pair") == 0) {
            TSNode key = ts_node_child_by_field_name(c, "key", 3);
            if (ts_node_is_null(key))
                continue;
            const char *kt = ts_node_type(key);
            if (strcmp(kt, "property_identifier") == 0)
                name = node_text(a, key, src);
            else if (strcmp(kt, "string") == 0)
                name = string_payload(a, key, src);
            /* number / computed keys: skip, nothing honest to record */
        } else if (strcmp(t, "method_definition") == 0) {
            TSNode key = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(key))
                key = ts_node_child_by_field_name(c, "key", 3);
            if (ts_node_is_null(key))
                continue;
            const char *kt = ts_node_type(key);
            if (strcmp(kt, "property_identifier") == 0)
                name = node_text(a, key, src);
            else if (strcmp(kt, "string") == 0)
                name = string_payload(a, key, src);
        } else if (strcmp(t, "shorthand_property_identifier") == 0) {
            name = node_text(a, c, src);
        } else if (strcmp(t, "spread_element") == 0) {
            *opaque = 1; /* names from the spread are unknowable */
            continue;
        } else {
            continue; /* comment and anything else: nothing honest here */
        }
        if (!name || name[0] == '\0')
            continue;
        if (!cjs_seen_push(seen, n_seen, cap_seen, name)) {
            *oom = 1;
            return 0;
        }
        if (!oc_surface_export_push(exports, a, path, name, line, 0)) {
            *oom = 1;
            return 0;
        }
        (*n_pushed)++;
    }
    return 1;
}

/* Handles Object.assign(<target>, {...}, ...) -- either as the
 * standalone statement pattern (verify_target: the first argument must
 * itself be exactly a CJS export target) or as the RHS of
 * `module.exports = Object.assign(...)` (target already implied by the
 * assignment's LEFT). Pushes the honest keys of every object-literal
 * argument after the target. Returns 1 = an Object.assign call that was
 * processed (keys pushed, or honestly unknowable), -1 = not an
 * Object.assign call at all (nothing recorded; the caller decides),
 * 0 = allocation failure (*oom set). */
static int cjs_push_object_assign(oc_arena *a, TSNode call, const char *src, const char *path,
                                  size_t line, oc_export_vec *exports, char ***seen, size_t *n_seen,
                                  size_t *cap_seen, int verify_target, int *oom) {
    TSNode fn = ts_node_child_by_field_name(call, "function", 8);
    if (ts_node_is_null(fn) || strcmp(ts_node_type(fn), "member_expression") != 0)
        return -1; /* not Object.assign after all */
    TSNode fobj = ts_node_child_by_field_name(fn, "object", 6);
    TSNode fprop = ts_node_child_by_field_name(fn, "property", 8);
    if (ts_node_is_null(fobj) || ts_node_is_null(fprop))
        return -1;
    char *oi = node_text(a, fobj, src);
    char *op = node_text(a, fprop, src);
    if (!oi || !op || strcmp(oi, "Object") != 0 || strcmp(op, "assign") != 0)
        return -1;
    TSNode args = ts_node_child_by_field_name(call, "arguments", 9);
    if (ts_node_is_null(args))
        return -1;
    int first = 1;
    uint32_t nc = ts_node_child_count(args);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(args, i);
        if (!ts_node_is_named(c))
            continue; /* parens / commas are anonymous */
        if (first) {
            first = 0;
            if (!verify_target)
                continue; /* RHS form: the target is the assignment's LEFT */
            char *tt = node_text(a, c, src);
            if (!tt || (strcmp(tt, "module.exports") != 0 && strcmp(tt, "exports") != 0))
                return 1; /* some other Object.assign: nothing recorded */
            continue;
        }
        if (strcmp(ts_node_type(c), "object") == 0) {
            int np = 0, opq = 0;
            if (!cjs_push_object_keys(a, c, src, path, line, exports, seen, n_seen, cap_seen, oom,
                                      &np, &opq))
                return 0;
            if (np == 0 && opq) {
                /* the merge carries unknowable names: record the
                 * wholesale-change pseudo-name instead of guessing */
                if (!cjs_seen_push(seen, n_seen, cap_seen, "default")) {
                    *oom = 1;
                    return 0;
                }
                if (!oc_surface_export_push(exports, a, path, "default", line, 0)) {
                    *oom = 1;
                    return 0;
                }
            }
        }
        /* non-object sources (identifier, require(...), spread): honest
         * skip -- their names are not statically knowable */
    }
    return 1;
}

/* True when the node sits directly under the Python module (possibly
 * wrapped in a decorated_definition). Nested defs are not exports. */
static int py_is_top_level(TSNode n) {
    TSNode p = ts_node_parent(n);
    if (!ts_node_is_null(p) && strcmp(ts_node_type(p), "decorated_definition") == 0)
        p = ts_node_parent(p);
    return !ts_node_is_null(p) && strcmp(ts_node_type(p), "module") == 0;
}

/* Anonymous child token == text (byte compare against the source). */
static int node_has_anon_child_text(TSNode n, const char *src, const char *text) {
    uint32_t nc = ts_node_child_count(n);
    size_t tl = strlen(text);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(n, i);
        if (ts_node_is_named(c))
            continue;
        uint32_t s = ts_node_start_byte(c), e = ts_node_end_byte(c);
        if ((size_t)(e - s) == tl && memcmp(src + s, text, tl) == 0)
            return 1;
    }
    return 0;
}

static TSNode null_tsnode(void) {
    TSNode n;
    memset(&n, 0, sizeof(n));
    return n;
}

/* Java import shapes (SPRINT-12, docs/LANGUAGES.md Java section): the
 * declaration's path child is an identifier / scoped_identifier whose
 * TEXT is the dotted path as written; `static` is an anonymous
 * token; the wildcard is an `asterisk` node. Shapes:
 *   import a.b.C;            -> spec "a.b.C", binds "C"
 *   import static a.b.C.f;   -> spec "a.b.C" (the qualifier; the member
 *                               segment is stripped), binds "C" (the
 *                               resolved class's export)
 *   import static a.b.C.*;   -> spec "a.b.C" (the CLASS claim), binds "C"
 *   import a.b.*;            -> spec "a.b.*" (package claim), binds NULL
 *                               (fail-open)
 * Returns 0 when the shape cannot be modeled honestly (static path
 * without a qualifier segment) -- nothing is recorded, never guessed. */
static int java_import_shape(oc_arena *a, TSNode ist, const char *src, char **spec_out,
                             char **bind_out) {
    *spec_out = NULL;
    *bind_out = NULL;
    int is_static = node_has_anon_child_text(ist, src, "static");
    int is_wildcard = 0;
    TSNode pathnode = null_tsnode();
    uint32_t nc = ts_node_child_count(ist);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(ist, i);
        if (!ts_node_is_named(c))
            continue;
        const char *t = ts_node_type(c);
        if ((strcmp(t, "identifier") == 0 || strcmp(t, "scoped_identifier") == 0) &&
            ts_node_is_null(pathnode))
            pathnode = c;
        else if (strcmp(t, "asterisk") == 0)
            is_wildcard = 1;
    }
    if (ts_node_is_null(pathnode))
        return 0;
    char *path = node_text(a, pathnode, src);
    if (!path || path[0] == '\0')
        return 0;
    if (is_wildcard && !is_static) {
        /* package wildcard: keep the trailing .* -- a package claim */
        *spec_out = oc_arena_sprintf(a, "%s.*", path);
        return *spec_out != NULL;
    }
    char *qual = path;
    if (is_wildcard && is_static) {
        /* static member wildcard names the CLASS */
        qual = path;
    } else if (is_static) {
        char *dot = strrchr(path, '.');
        if (!dot || dot == path)
            return 0; /* `import static C;`: no qualifier, never guess */
        *dot = '\0';
        qual = path;
        if (qual[0] == '\0')
            return 0;
    }
    *spec_out = oc_arena_strdup(a, qual);
    if (!*spec_out)
        return 0;
    char *last = strrchr(qual, '.');
    *bind_out = oc_arena_strdup(a, last ? last + 1 : qual);
    return *bind_out != NULL;
}

/* Kotlin import shapes (SPRINT-22, docs/LANGUAGES.md Kotlin section 5):
 * the import_header's dotted `identifier` child is the path AS WRITTEN
 * (the pinned grammar keeps the whole dotted chain inside ONE
 * identifier node -- probed); a `wildcard_import` child marks the
 * package claim; an `import_alias` child is local surface detail.
 * Shapes:
 *   import a.b.C      -> spec "a.b.C", binds "C"   (2 segments: the
 *                       Java single-type record; the declared-fact
 *                       resolver reads the last segment as the claim)
 *   import a.b.C.f    -> spec "a.b.C.f", binds NULL (3+ segments: the
 *                       member reading is ambiguous at parse time -- C
 *                       may be an object carrying member f, or a
 *                       package carrying top-level f -- so the record
 *                       is the whole-file claim direction, the C++
 *                       include discipline; the resolver matches BOTH
 *                       readings against the declared facts)
 *   import a.b.*      -> spec "a.b.*", binds NULL  (the Java
 *                       asterisk-marker package claim, fail-open)
 *   import a.b.C as D -> spec "a.b.C", binds "C"   (the ORIGINAL name:
 *                       the reference proves the original declaration
 *                       is used; the alias is never a narrowing
 *                       predicate -- the Go alias note)
 * Returns 0 when no identifier child exists (never guessed). */
static int kotlin_import_shape(oc_arena *a, TSNode ist, const char *src, char **spec_out,
                               char **bind_out) {
    *spec_out = NULL;
    *bind_out = NULL;
    TSNode pathnode = null_tsnode();
    int is_wildcard = 0;
    uint32_t nc = ts_node_child_count(ist);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(ist, i);
        if (!ts_node_is_named(c))
            continue;
        const char *t = ts_node_type(c);
        if (strcmp(t, "identifier") == 0 && ts_node_is_null(pathnode))
            pathnode = c;
        else if (strcmp(t, "wildcard_import") == 0)
            is_wildcard = 1;
    }
    if (ts_node_is_null(pathnode))
        return 0;
    char *path = node_text(a, pathnode, src);
    if (!path || path[0] == '\0')
        return 0;
    if (is_wildcard) {
        *spec_out = oc_arena_sprintf(a, "%s.*", path);
        return *spec_out != NULL;
    }
    *spec_out = oc_arena_strdup(a, path);
    if (!*spec_out)
        return 0;
    /* an import_alias child makes the claim UNAMBIGUOUS (Kotlin cannot
     * alias a member -- only a declaration): bind the ORIGINAL last
     * segment. Without an alias: one/two segments are the unambiguous
     * single-type record (the Java discipline); three or more keep the
     * NULL bind (the ambiguous member shape, whole-file claim). */
    uint32_t ac = ts_node_child_count(ist);
    int has_alias = 0;
    for (uint32_t i = 0; i < ac; i++) {
        TSNode c = ts_node_child(ist, i);
        if (ts_node_is_named(c) && strcmp(ts_node_type(c), "import_alias") == 0) {
            has_alias = 1;
            break;
        }
    }
    const char *last_seg = strrchr(path, '.');
    last_seg = last_seg ? last_seg + 1 : path;
    if (has_alias || strchr(path, '.') == NULL ||
        (strchr(path, '.') != NULL && strrchr(path, '.') == strchr(path, '.'))) {
        *bind_out = oc_arena_strdup(a, last_seg);
    }
    return *spec_out != NULL;
}

/* Kotlin (SPRINT-22): the `private` filter -- the pinned grammar wraps
 * the keyword as modifiers > visibility_modifier > ANONYMOUS `private`
 * (probed; the C# shape's Kotlin spelling). A declaration without the
 * modifiers wrapper is public-by-default (the Kotlin default). */
static int kt_is_private(TSNode n, const char *src) {
    uint32_t nc = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(n, i);
        if (!ts_node_is_named(c) || strcmp(ts_node_type(c), "modifiers") != 0)
            continue;
        uint32_t mc = ts_node_child_count(c);
        for (uint32_t j = 0; j < mc; j++) {
            TSNode vm = ts_node_child(c, j);
            if (!ts_node_is_named(vm) || strcmp(ts_node_type(vm), "visibility_modifier") != 0)
                continue;
            if (node_has_anon_child_text(vm, src, "private"))
                return 1;
        }
    }
    return 0;
}

/* Swift (SPRINT-23): the `private`/`fileprivate` filter -- the pinned
 * grammar wraps the keywords as modifiers > visibility_modifier >
 * ANONYMOUS tokens (probed; the Kotlin shape verbatim). A declaration
 * without the modifiers wrapper is internal-by-default (the Swift
 * default: module-visible, surface). */
static int swift_is_private(TSNode n, const char *src) {
    uint32_t nc = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(n, i);
        if (!ts_node_is_named(c) || strcmp(ts_node_type(c), "modifiers") != 0)
            continue;
        uint32_t mc = ts_node_child_count(c);
        for (uint32_t j = 0; j < mc; j++) {
            TSNode vm = ts_node_child(c, j);
            if (!ts_node_is_named(vm) || strcmp(ts_node_type(vm), "visibility_modifier") != 0)
                continue;
            if (node_has_anon_child_text(vm, src, "private") ||
                node_has_anon_child_text(vm, src, "fileprivate"))
                return 1;
        }
    }
    return 0;
}

/* Swift import shapes (SPRINT-23, docs/LANGUAGES.md Swift section 5):
 * the import_declaration's `identifier` child carries the dotted path
 * as SEPARATE simple_identifier children joined by anonymous `.` tokens
 * (probed -- the C side joins the segment texts, the path AS WRITTEN).
 * The optional import KIND is an anonymous child token (probed:
 * class/func/struct/enum/protocol/typealias/var/let); `@testable` /
 * `@_exported` ride modifiers > attribute and never change the module.
 * Shapes:
 *   import M            -> spec "M", binds NULL   (the whole-module
 *                        claim: importing a module loads ALL of it --
 *                        the Kotlin wildcard fail-open record)
 *   import M.Sub        -> spec "M", binds NULL   (the submodule path:
 *                        the FIRST segment is the module -- the
 *                        declared-fact key; the submodule refine is a
 *                        recorded v1 boundary)
 *   import kind M.T     -> spec "M", binds "T"    (kind-scoped with a
 *                        member: the LAST segment names the member,
 *                        the over-mark direction bounded by the
 *                        declared facts -- the Kotlin single-type
 *                        record's Swift spelling)
 *   import kind M       -> spec "M", binds NULL   (kind without a
 *                        member path: the module claim stands)
 * The pinned grammar has NO import-alias shape (probed boundary:
 * `import struct Kit.Tag as T` does not parse -- nothing to
 * classify, never guessed). Returns 0 when no identifier child exists. */
static int swift_import_shape(oc_arena *a, TSNode ist, const char *src, char **spec_out,
                              char **bind_out) {
    *spec_out = NULL;
    *bind_out = NULL;
    TSNode pathnode = null_tsnode();
    const char *kind_tok = NULL;
    uint32_t nc = ts_node_child_count(ist);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(ist, i);
        if (ts_node_is_named(c)) {
            if (strcmp(ts_node_type(c), "identifier") == 0 && ts_node_is_null(pathnode))
                pathnode = c;
            continue;
        }
        const char *t = ts_node_type(c);
        if (strcmp(t, "import") == 0)
            continue;
        if (strcmp(t, "class") == 0 || strcmp(t, "func") == 0 || strcmp(t, "struct") == 0 ||
            strcmp(t, "enum") == 0 || strcmp(t, "protocol") == 0 || strcmp(t, "typealias") == 0 ||
            strcmp(t, "var") == 0 || strcmp(t, "let") == 0)
            kind_tok = t; /* the LAST kind token stands (never two) */
    }
    if (ts_node_is_null(pathnode))
        return 0;
    /* the dotted path: the identifier's own children, segment texts
     * joined with '.' (the anonymous dots are skipped; the segments are
     * simple_identifier nodes -- probed) */
    uint32_t sc = ts_node_child_count(pathnode);
    size_t cap = 64, len = 0;
    char *path = (char *)oc_arena_alloc(a, cap);
    if (!path)
        return 0;
    path[0] = '\0';
    int n_segs = 0;
    for (uint32_t i = 0; i < sc; i++) {
        TSNode c = ts_node_child(pathnode, i);
        if (!ts_node_is_named(c))
            continue;
        char *seg = node_text(a, c, src);
        if (!seg || seg[0] == '\0')
            return 0;
        size_t sl = strlen(seg);
        if (len + sl + 2 > cap)
            return 0; /* pathological path: record nothing, never guessed */
        if (n_segs > 0)
            path[len++] = '.';
        memcpy(path + len, seg, sl + 1);
        len += sl;
        n_segs++;
    }
    if (n_segs == 0)
        return 0;
    /* the module is the FIRST segment (the compilation target -- the
     * declared-fact key); the full path stays out of the record: the
     * submodule refine is the recorded v1 boundary and the kind-scoped
     * member rides the BIND */
    char *dot = strchr(path, '.');
    if (dot)
        *dot = '\0';
    *spec_out = oc_arena_strdup(a, path);
    if (!*spec_out)
        return 0;
    if (kind_tok && n_segs >= 2) {
        /* kind-scoped member claim: the LAST segment is the member */
        const char *seg = dot + 1;
        const char *tail = strrchr(seg, '.');
        *bind_out = oc_arena_strdup(a, tail ? tail + 1 : seg);
        if (!*bind_out)
            return 0;
    }
    return 1;
}

/* Scala (SPRINT-24): the `private` filter -- the pinned grammar wraps
 * the keyword as modifiers > access_modifier > ANONYMOUS `private`
 * (the C# shape's Kotlin spelling; `protected` is package/superclass-
 * visible and stays surface -- the Kotlin treatment of `internal`). A
 * declaration without the modifiers wrapper is public-by-default (the
 * Scala default). */
static int scala_is_private(TSNode n, const char *src) {
    uint32_t nc = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(n, i);
        if (!ts_node_is_named(c) || strcmp(ts_node_type(c), "modifiers") != 0)
            continue;
        uint32_t mc = ts_node_child_count(c);
        for (uint32_t j = 0; j < mc; j++) {
            TSNode am = ts_node_child(c, j);
            if (!ts_node_is_named(am) || strcmp(ts_node_type(am), "access_modifier") != 0)
                continue;
            if (node_has_anon_child_text(am, src, "private"))
                return 1;
        }
    }
    return 0;
}

/* Scala import expansion (SPRINT-24, docs/LANGUAGES.md Scala section 5):
 * the Rust use-tree / PHP use-declaration channel -- complete in C, ONE
 * import + one bind per leaf pushed here (the selector group expands,
 * everything else is a single record). The dotted `path` field children
 * join with '.' (the path AS WRITTEN); the trailing child classifies
 * the shape:
 *   namespace_wildcard    -> `a.b.*` (bind NULL: the whole-package
 *                            claim, the Java asterisk-marker record);
 *   namespace_selectors   -> one record per selector child (identifier
 *                            -> bind the selector; as/arrow rename ->
 *                            bind the ORIGINAL name, the alias is local
 *                            surface detail -- the Kotlin alias note);
 *   as_renamed_identifier -> the Scala 3 tail rename, binds the
 *                            ORIGINAL name;
 *   plain                 -> bind the LAST segment for one/two-segment
 *                            paths (the canonical class import), NULL
 *                            for three or more (the AMBIGUOUS MEMBER
 *                            shape -- `a.b.C.f` binds `f` would
 *                            under-mark the object `C` the member
 *                            reading actually uses; the whole-file
 *                            claim, the C++ include discipline; the
 *                            resolver matches BOTH declared readings).
 * On failure the records stay out entirely -- nothing recorded, never
 * guessed. */
static int scala_import_expand(oc_arena *a, TSNode ist, const char *src, const char *from_path,
                               oc_import_vec *imports, oc_bind_vec *binds, oc_lang_family family,
                               int *oom) {
    if (*oom)
        return 0;
    /* 1. join the dotted path segments -- only the NAMED children
     * carrying the `path` field, in order (the ANONYMOUS `.` separator
     * tokens carry the path field too -- probed; joining them too
     * fabricates a doubled-dot path), joined with '.' */
    char path[512];
    size_t len = 0;
    int n_segs = 0;
    TSNode tail = null_tsnode(); /* the first named child OUTSIDE the path field */
    uint32_t nc = ts_node_child_count(ist);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(ist, i);
        const char *fname = ts_node_field_name_for_child(ist, i);
        int is_path = fname && strcmp(fname, "path") == 0;
        if (is_path && ts_node_is_named(c)) {
            char *seg = node_text(a, c, src);
            if (!seg)
                continue;
            size_t sl = strlen(seg);
            if (len + sl + 2 > sizeof(path))
                return 0; /* pathological path: record nothing, never guessed */
            if (n_segs > 0)
                path[len++] = '.';
            memcpy(path + len, seg, sl + 1);
            len += sl;
            n_segs++;
        } else if (ts_node_is_named(c) && !is_path) {
            /* the trailing shape child (wildcard / selectors / the Scala 3
             * tail rename); keep the FIRST one -- a plain import has none
             * and falls through to the plain branch */
            if (ts_node_is_null(tail))
                tail = c;
        }
    }
    if (n_segs == 0)
        return 0;
    path[len] = '\0';
    const char *tail_type = ts_node_is_null(tail) ? "" : ts_node_type(tail);
    int push_one = 0;
    char *spec = NULL;
    char *bind = NULL;
    if (strcmp(tail_type, "namespace_wildcard") == 0) {
        spec = oc_arena_sprintf(a, "%s.*", path);
        push_one = 1; /* bind stays NULL */
    } else if (strcmp(tail_type, "as_renamed_identifier") == 0) {
        TSNode nm = ts_node_child_by_field_name(tail, "name", 4);
        if (ts_node_is_null(nm))
            return 0; /* unreadable original name: nothing recorded */
        char *orig = node_text(a, nm, src);
        if (!orig || orig[0] == '\0')
            return 0;
        spec = oc_arena_sprintf(a, "%s.%s", path, orig);
        bind = oc_arena_strdup(a, orig);
        push_one = spec != NULL;
    } else if (strcmp(tail_type, "namespace_selectors") == 0) {
        uint32_t sc = ts_node_child_count(tail);
        for (uint32_t i = 0; i < sc; i++) {
            TSNode c = ts_node_child(tail, i);
            if (!ts_node_is_named(c))
                continue;
            const char *ct = ts_node_type(c);
            char *sel = NULL;
            char *sbind = NULL;
            if (strcmp(ct, "identifier") == 0) {
                sel = node_text(a, c, src);
                if (sel && sel[0] != '\0')
                    sbind = oc_arena_strdup(a, sel);
            } else if (strcmp(ct, "as_renamed_identifier") == 0 ||
                       strcmp(ct, "arrow_renamed_identifier") == 0) {
                TSNode nm = ts_node_child_by_field_name(c, "name", 4);
                sel = ts_node_is_null(nm) ? NULL : node_text(a, nm, src);
                if (sel && sel[0] != '\0')
                    sbind = oc_arena_strdup(a, sel);
            } else {
                continue; /* a typed selector shape records nothing v1 */
            }
            if (!sel || sel[0] == '\0')
                continue;
            char *sspec = oc_arena_sprintf(a, "%s.%s", path, sel);
            if (!sspec) {
                *oom = 1;
                return 0;
            }
            if (imports && !oc_imports_push(imports, a, from_path, sspec, family)) {
                *oom = 1;
                return 0;
            }
            if (binds && !oc_surface_bind_push(binds, a, from_path, sspec, sbind, family)) {
                *oom = 1;
                return 0;
            }
        }
        return 1; /* the selector channel is complete for this match */
    } else {
        /* the plain path: the bind is the last segment for one/two
         * segments, NULL for three or more (the ambiguous member) */
        spec = oc_arena_strdup(a, path);
        push_one = spec != NULL;
        const char *last = strrchr(path, '.');
        last = last ? last + 1 : path;
        int n_dots = 0;
        for (const char *q = path; q && *q; q++) {
            if (*q == '.')
                n_dots++;
        }
        if (n_dots <= 1) {
            bind = oc_arena_strdup(a, last);
            if (!bind)
                push_one = 0;
        }
    }
    if (!push_one) {
        *oom = spec == NULL ? 1 : *oom;
        return 0;
    }
    if (imports && !oc_imports_push(imports, a, from_path, spec, family)) {
        *oom = 1;
        return 0;
    }
    if (binds && !oc_surface_bind_push(binds, a, from_path, spec, bind, family)) {
        *oom = 1;
        return 0;
    }
    return 1;
}

/* Swift (SPRINT-23): the declared NAME inside a property's `name`-field
 * pattern node. The pinned grammar parses the pattern in TWO shapes
 * (probed): `pattern > bound_identifier > simple_identifier` (the
 * stored/value-binding shape) and `pattern > simple_identifier` (the
 * annotated/computed shape) -- the walk takes the FIRST
 * simple_identifier at bounded depth, never guessed. */
static TSNode swift_pattern_ident(TSNode pat) {
    TSNode null_node;
    memset(&null_node, 0, sizeof(null_node));
    if (ts_node_is_null(pat))
        return null_node;
    for (int depth = 0; depth < 3; depth++) {
        uint32_t nc = ts_node_child_count(pat);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode c = ts_node_child(pat, i);
            if (ts_node_is_named(c) && strcmp(ts_node_type(c), "simple_identifier") == 0)
                return c;
        }
        /* descend one level: the bound_identifier wrapper shape */
        TSNode next = null_node;
        for (uint32_t i = 0; i < nc && ts_node_is_null(next); i++) {
            TSNode c = ts_node_child(pat, i);
            if (ts_node_is_named(c))
                next = c;
        }
        if (ts_node_is_null(next))
            return null_node;
        pat = next;
    }
    return null_node;
}

/* Swift (SPRINT-23, the C# accessor precedent): the accessor keyword is
 * the node TYPE itself (computed_getter / computed_setter, the
 * implicit-getter computed_property, the observers willset_clause /
 * didset_clause), and the owning property_declaration is reached by a
 * PARENT CLIMB -- the pinned grammar nests the accessors INSIDE the
 * property (probed: computed_getter sits in a computed_property wrapper
 * under the property's computed_value FIELD; the observers sit in a
 * willset_didset_block; the implicit getter's statements sit directly
 * in the computed_property). The name is the owner property's
 * pattern > bound_identifier > simple_identifier (one level down, the
 * Kotlin variable_declaration shape). The honest qualified names are
 * `Prop.get` / `Prop.set` / `Prop.willSet` / `Prop.didSet`. Returns 0
 * when no owning property exists (never guessed). */
static int swift_accessor_name(oc_arena *a, TSNode aexpr, const char *src, char **out) {
    const char *kw = "get";
    const char *t = ts_node_type(aexpr);
    if (strcmp(t, "computed_setter") == 0)
        kw = "set";
    else if (strcmp(t, "willset_clause") == 0)
        kw = "willSet";
    else if (strcmp(t, "didset_clause") == 0)
        kw = "didSet";
    TSNode decl = aexpr;
    int hops = 0;
    while (!ts_node_is_null(decl) && strcmp(ts_node_type(decl), "property_declaration") != 0) {
        decl = ts_node_parent(decl);
        if (++hops > 6)
            return 0; /* pathological nesting: record nothing */
    }
    if (ts_node_is_null(decl))
        return 0;
    /* the owner's name: the `name` field is a pattern whose identifier
     * sits either directly inside or one wrapper down (both shapes
     * probed) */
    TSNode nm = ts_node_child_by_field_name(decl, "name", 4);
    if (ts_node_is_null(nm))
        return 0;
    TSNode sid = swift_pattern_ident(nm);
    if (ts_node_is_null(sid))
        return 0;
    char *base = node_text(a, sid, src);
    if (!base || base[0] == '\0')
        return 0;
    *out = oc_arena_sprintf(a, "%s.%s", base, kw);
    return *out != NULL;
}

/* Swift (SPRINT-23): the plain string text of a literal argument value.
 * Only line/multi-line string literals whose pieces are ALL plain text
 * count (the probe shape); an interpolated, raw or non-literal value
 * records NOTHING (never guessed). Two-pass exact-size join (paths can
 * be long; a truncated path would silently mis-map a target). */
static char *swift_string_text(oc_arena *a, TSNode val, const char *src) {
    const char *t = ts_node_type(val);
    if (strcmp(t, "line_string_literal") != 0 && strcmp(t, "multi_line_string_literal") != 0)
        return NULL; /* interpolated, raw, or non-literal: not static text */
    uint32_t nc = ts_node_child_count(val);
    size_t total = 0;
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(val, i);
        if (!ts_node_is_named(c))
            continue;
        const char *ct = ts_node_type(c);
        if (strcmp(ct, "line_str_text") != 0 && strcmp(ct, "multi_line_str_text") != 0)
            return NULL; /* an interpolated piece: not static text */
        total += ts_node_end_byte(c) - ts_node_start_byte(c);
    }
    char *out = (char *)oc_arena_alloc(a, total + 1);
    if (!out)
        return NULL;
    size_t w = 0;
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(val, i);
        if (!ts_node_is_named(c))
            continue;
        uint32_t s = ts_node_start_byte(c), e = ts_node_end_byte(c);
        memcpy(out + w, src + s, e - s);
        w += (size_t)(e - s);
    }
    out[w] = '\0';
    return out;
}

/* Swift (SPRINT-23): true when a top-level statement candidate is a
 * `#if` DIRECTIVE CONDITION, not code -- the pinned grammar flattens
 * compilation directives, so `#if compiler(>=5.9)` puts the condition
 * call on the source_file as a SIBLING of the directive node (probed
 * on real SPM manifests). The builtin condition functions are a fixed
 * small set; any other callee IS executable top-level code. */
static int swift_is_directive_condition(oc_arena *a, TSNode stmt, const char *src) {
    if (strcmp(ts_node_type(stmt), "call_expression") != 0)
        return 0;
    static const char *const DIR_BUILTINS[] = {"canImport",
                                               "compiler",
                                               "swift",
                                               "os",
                                               "arch",
                                               "targetEnvironment",
                                               "sourceLocation",
                                               "module",
                                               "_compiler_version",
                                               "_hasFeature",
                                               NULL};
    uint32_t nc = ts_node_child_count(stmt);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(stmt, i);
        if (!ts_node_is_named(c))
            continue;
        const char *t = ts_node_type(c);
        if (strcmp(t, "simple_identifier") == 0 || strcmp(t, "prefix_expression") == 0) {
            char *ct = node_text(a, c, src);
            if (!ct)
                return 0;
            for (size_t k = 0; DIR_BUILTINS[k]; k++)
                if (strcmp(ct, DIR_BUILTINS[k]) == 0)
                    return 1;
            return 0;
        }
        break; /* the first named child is the callee slot */
    }
    return 0;
}

/* Swift (SPRINT-23): the Package.swift declared-target fact. The
 * manifest filename is SPM's FIXED name (not a heuristic: the tooling
 * itself looks for exactly this file). The depth-bounded preorder walk
 * extracts every `.target` / `.executableTarget` / `.testTarget` call's
 * `name:` string and optional `path:` string (the probe verified the
 * shape: the callee is a prefix_expression carrying the leading dot or
 * a bare simple_identifier; the labeled arguments are value_argument
 * nodes with value_argument_label names). Dependencies arrays, the
 * platform clauses and every other call shape never match the callee
 * set. Returns 0 on OOM (the caller degrades to no facts). */
static int swift_is_manifest_path(const char *path) {
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    return strcmp(b, "Package.swift") == 0;
}

static void swift_manifest_walk(TSNode n, oc_arena *a, const char *src, oc_hint_vec *hints,
                                const char *path, int depth, int *fail) {
    if (*fail || depth > 48)
        return;
    if (ts_node_is_named(n) && strcmp(ts_node_type(n), "call_expression") == 0) {
        uint32_t nc = ts_node_child_count(n);
        TSNode callee = null_tsnode();
        TSNode suffix = null_tsnode();
        for (uint32_t i = 0; i < nc; i++) {
            TSNode c = ts_node_child(n, i);
            if (!ts_node_is_named(c))
                continue;
            const char *t = ts_node_type(c);
            if (ts_node_is_null(callee) &&
                (strcmp(t, "simple_identifier") == 0 || strcmp(t, "prefix_expression") == 0))
                callee = c;
            else if (strcmp(t, "call_suffix") == 0)
                suffix = c;
        }
        if (!ts_node_is_null(callee) && !ts_node_is_null(suffix)) {
            char *ct = node_text(a, callee, src);
            if (ct &&
                (strcmp(ct, "target") == 0 || strcmp(ct, ".target") == 0 ||
                 strcmp(ct, "executableTarget") == 0 || strcmp(ct, ".executableTarget") == 0 ||
                 strcmp(ct, "testTarget") == 0 || strcmp(ct, ".testTarget") == 0)) {
                TSNode args = null_tsnode();
                uint32_t sc = ts_node_child_count(suffix);
                for (uint32_t i = 0; i < sc && ts_node_is_null(args); i++) {
                    TSNode c = ts_node_child(suffix, i);
                    if (ts_node_is_named(c) && strcmp(ts_node_type(c), "value_arguments") == 0)
                        args = c;
                }
                if (!ts_node_is_null(args)) {
                    char *nm = NULL, *dp = NULL;
                    uint32_t ac = ts_node_child_count(args);
                    for (uint32_t i = 0; i < ac; i++) {
                        TSNode va = ts_node_child(args, i);
                        if (!ts_node_is_named(va) ||
                            strcmp(ts_node_type(va), "value_argument") != 0)
                            continue;
                        TSNode lbl = ts_node_child_by_field_name(va, "name", 4);
                        TSNode val = ts_node_child_by_field_name(va, "value", 5);
                        if (ts_node_is_null(lbl) || ts_node_is_null(val))
                            continue;
                        char *lt = NULL;
                        uint32_t lc = ts_node_child_count(lbl);
                        for (uint32_t j = 0; j < lc; j++) {
                            TSNode c2 = ts_node_child(lbl, j);
                            if (ts_node_is_named(c2) &&
                                strcmp(ts_node_type(c2), "simple_identifier") == 0) {
                                lt = node_text(a, c2, src);
                                break;
                            }
                        }
                        if (!lt)
                            continue;
                        if (strcmp(lt, "name") == 0 && !nm)
                            nm = swift_string_text(a, val, src);
                        else if (strcmp(lt, "path") == 0 && !dp)
                            dp = swift_string_text(a, val, src);
                    }
                    if (nm && nm[0] != '\0') {
                        if (!oc_surface_hint_push_swift_target(hints, a, path, nm, dp))
                            *fail = 1;
                    }
                }
            }
        }
    }
    uint32_t nc2 = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc2; i++)
        swift_manifest_walk(ts_node_child(n, i), a, src, hints, path, depth + 1, fail);
}

/* C# helper (SPRINT-13): one NAMED child whose type is `modifier` and
 * whose TEXT is the keyword (verified: individual modifier nodes, no
 * wrapper -- unlike Java's anonymous tokens inside a `modifiers`). */
static int node_has_modifier_text(TSNode n, const char *src, const char *text) {
    uint32_t nc = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(n, i);
        if (!ts_node_is_named(c) || strcmp(ts_node_type(c), "modifier") != 0)
            continue;
        uint32_t s = ts_node_start_byte(c), e = ts_node_end_byte(c);
        size_t tl = strlen(text);
        if ((size_t)(e - s) == tl && memcmp(src + s, text, tl) == 0)
            return 1;
    }
    return 0;
}

/* C# top-level walk (SPRINT-13): TRUE when the type declaration sits
 * outside any other TYPE. Namespace nesting (block
 * `namespace_declaration` or file-scoped
 * `file_scoped_namespace_declaration`) and preprocessor wrappers
 * (`preproc_if` / `preproc_elif` / `preproc_else` and the attribute
 * variant) are climbed; a `declaration_list` whose owner is a TYPE
 * declaration means nested -- not module surface (the verified node
 * walk, docs/LANGUAGES.md C# section 6). */
static int csharp_type_is_toplevel(TSNode n) {
    TSNode p = ts_node_parent(n);
    while (!ts_node_is_null(p)) {
        const char *t = ts_node_type(p);
        if (strcmp(t, "compilation_unit") == 0)
            return 1;
        if (strcmp(t, "namespace_declaration") == 0 ||
            strcmp(t, "file_scoped_namespace_declaration") == 0 || strcmp(t, "preproc_if") == 0 ||
            strcmp(t, "preproc_elif") == 0 || strcmp(t, "preproc_else") == 0 ||
            strcmp(t, "preproc_if_in_attribute_list") == 0) {
            p = ts_node_parent(p);
            continue;
        }
        if (strcmp(t, "declaration_list") == 0) {
            TSNode gp = ts_node_parent(p);
            if (!ts_node_is_null(gp) && strcmp(ts_node_type(gp), "namespace_declaration") == 0) {
                p = gp;
                continue;
            }
        }
        return 0; /* nested in a type (or anything else): not top-level */
    }
    return 0;
}

/* C (SPRINT-20): top-level check for module-surface candidates. The
 * parent chain must climb ONLY the three C wrapper kinds -- preprocessor
 * conditionals (the conventional #ifndef include guard wraps EVERYTHING
 * in a header; #if/#else/#elif blocks around declarations are
 * ubiquitous), `extern "C"` linkage blocks, and the declaration_list a
 * BLOCK-form linkage carries its candidates in (the C# namespace
 * declaration_list precedent) -- and then reach translation_unit.
 * Anything else (a compound_statement, a struct's field_declaration_list)
 * is not top level.
 *
 * SPRINT-21 (the C++ adoption, probe-verified): two C++ wrapper kinds
 * join the climb -- named namespace_definition bodies (the PHP namespace
 * precedent: the members of a named namespace are the file's surface;
 * the body is a declaration_list) and template_declaration (the wrapper
 * around class/struct/function templates). An ANONYMOUS namespace (no
 * name field) is C++ internal linkage: every candidate inside it is
 * excluded -- the C `static` rule generalized, applied by the climb
 * itself. NONE of the three node kinds can occur in a tree-sitter-c
 * tree, so C behavior is unchanged byte for byte. */
static int c_is_toplevel(TSNode n) {
    TSNode p = ts_node_parent(n);
    while (!ts_node_is_null(p)) {
        const char *t = ts_node_type(p);
        if (strcmp(t, "translation_unit") == 0)
            return 1;
        if (strcmp(t, "preproc_if") == 0 || strcmp(t, "preproc_ifdef") == 0 ||
            strcmp(t, "preproc_elif") == 0 || strcmp(t, "preproc_else") == 0 ||
            strcmp(t, "linkage_specification") == 0 || strcmp(t, "template_declaration") == 0) {
            p = ts_node_parent(p);
            continue;
        }
        if (strcmp(t, "namespace_definition") == 0) {
            /* anonymous namespace: internal linkage, never surface */
            if (ts_node_is_null(ts_node_child_by_field_name(p, "name", 4)))
                return 0;
            p = ts_node_parent(p);
            continue;
        }
        if (strcmp(t, "declaration_list") == 0) {
            /* only the extern "C" block's list and a named namespace's
             * body are wrappers; a function body is a compound_statement
             * and never reaches here */
            TSNode gp = ts_node_parent(p);
            if (!ts_node_is_null(gp) && (strcmp(ts_node_type(gp), "linkage_specification") == 0 ||
                                         strcmp(ts_node_type(gp), "namespace_definition") == 0)) {
                p = gp;
                continue;
            }
        }
        return 0;
    }
    return 0;
}

/* C (SPRINT-20): does the declaration carry `static` (internal linkage)?
 * The storage-class specifiers are named storage_class_specifier children
 * whose own anonymous token child is the keyword text; `extern` does not
 * narrow (external linkage by definition). */
static int c_has_static(TSNode n, const char *src) {
    uint32_t k = ts_node_child_count(n);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(n, i);
        if (ts_node_is_named(c) && strcmp(ts_node_type(c), "storage_class_specifier") == 0 &&
            node_has_anon_child_text(c, src, "static"))
            return 1;
    }
    return 0;
}

/* C (SPRINT-20) and C++ (SPRINT-21): is this type-field specifier a tag
 * DEFINITION (name AND body fields)? A bare `struct point p;` merely uses
 * the tag -- the tag name is module surface only when the specifier
 * defines the type. class_specifier is the C++ shape of the same node
 * family (it cannot occur in a C tree). */
static int c_specifier_defines_named(TSNode spec) {
    if (ts_node_is_null(spec))
        return 0;
    const char *t = ts_node_type(spec);
    if (strcmp(t, "struct_specifier") != 0 && strcmp(t, "union_specifier") != 0 &&
        strcmp(t, "enum_specifier") != 0 && strcmp(t, "class_specifier") != 0)
        return 0;
    TSNode name = ts_node_child_by_field_name(spec, "name", 4);
    TSNode body = ts_node_child_by_field_name(spec, "body", 4);
    return !ts_node_is_null(name) && !ts_node_is_null(body);
}

/* C# import shapes (SPRINT-13, docs/LANGUAGES.md C# section 5): the
 * directive's children decide the shape; the recorded SPECIFIER keeps
 * the directive's own tokens (everything between `using` and `;`):
 *   using A.B.C;         -> spec "A.B.C" (NAMESPACE claim), binds NULL
 *   using static A.B.C;  -> spec "static A.B.C" (TYPE claim), binds "C"
 *   using X = A.B.C;     -> spec "X = A.B.C" (ALIAS claim), binds NULL
 *                           (the alias never narrows marking)
 *   using global::A.B.C; -> the "global::" prefix is stripped from the
 *                           path (C#'s own root spelling; the verbatim
 *                           mapping IS the root-relative mapping)
 * Any other alias-qualification (X::Y.Z) and non-identifier paths
 * (generics, tuples) stay unresolved -- NULL, never guessed. */
static int csharp_import_shape(oc_arena *a, TSNode ist, const char *src, char **spec_out,
                               char **bind_out) {
    *spec_out = NULL;
    *bind_out = NULL;
    int is_static = node_has_anon_child_text(ist, src, "static");
    TSNode alias = ts_node_child_by_field_name(ist, "name", 4);
    TSNode pathnode = null_tsnode();
    uint32_t nc = ts_node_child_count(ist);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(ist, i);
        if (!ts_node_is_named(c))
            continue;
        const char *t = ts_node_type(c);
        if ((strcmp(t, "identifier") == 0 || strcmp(t, "qualified_name") == 0 ||
             strcmp(t, "alias_qualified_name") == 0) &&
            (ts_node_is_null(alias) || !ts_node_eq(c, alias)) && ts_node_is_null(pathnode))
            pathnode = c;
    }
    if (ts_node_is_null(pathnode))
        return 0;
    char *path = node_text(a, pathnode, src);
    if (!path || path[0] == '\0')
        return 0;
    /* global:: is C#'s own root spelling; strip the prefix, never
     * re-map anything else that carries '::' */
    if (strncmp(path, "global::", 8) == 0)
        path += 8;
    else if (strstr(path, "::") != NULL)
        return 0; /* extern-alias qualification: unresolved, never guessed */
    if (is_static) {
        if (path[0] == '\0')
            return 0;
        *spec_out = oc_arena_sprintf(a, "static %s", path);
        if (!*spec_out)
            return 0;
        char *last = strrchr(path, '.');
        *bind_out = oc_arena_strdup(a, last ? last + 1 : path);
        return *bind_out != NULL;
    }
    if (!ts_node_is_null(alias)) {
        char *aname = node_text(a, alias, src);
        if (!aname || aname[0] == '\0')
            return 0;
        *spec_out = oc_arena_sprintf(a, "%s = %s", aname, path);
        return *spec_out != NULL; /* alias binds NULL: never narrows marking */
    }
    *spec_out = oc_arena_strdup(a, path);
    return *spec_out != NULL; /* plain namespace claim, binds NULL */
}

/* Go (SPRINT-14): a promoted function-value assignment must have exactly
 * ONE name on the left side (`a, b := func(){}` is never promoted --
 * deciding which name carries the function would be a guess). For the
 * var_spec shape the spec must carry exactly one `name` field. */
static int go_promotion_single_name(TSNode fexpr) {
    const char *t = ts_node_type(fexpr);
    if (strcmp(t, "var_spec") == 0) {
        uint32_t nc = ts_node_child_count(fexpr);
        int n_names = 0;
        for (uint32_t i = 0; i < nc; i++) {
            const char *f = ts_node_field_name_for_child(fexpr, i);
            if (f && strcmp(f, "name") == 0)
                n_names++;
        }
        return n_names == 1;
    }
    TSNode left = ts_node_child_by_field_name(fexpr, "left", 4);
    if (ts_node_is_null(left) || strcmp(ts_node_type(left), "expression_list") != 0)
        return 0;
    uint32_t nc = ts_node_child_count(left);
    int n_names = 0;
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(left, i);
        if (ts_node_is_named(c))
            n_names++;
    }
    return n_names == 1;
}

/* Go (SPRINT-14): the plain assignment shape promotes ONLY the plain `=`
 * operator (never `+=` -- `f += func(){}` is not a definition). */
static int go_plain_assign_op(TSNode fexpr, const char *src) {
    TSNode op = ts_node_child_by_field_name(fexpr, "operator", 8);
    if (ts_node_is_null(op))
        return 0;
    return ts_node_end_byte(op) - ts_node_start_byte(op) == 1 && src[ts_node_start_byte(op)] == '=';
}

/* C# (SPRINT-14): the honest accessor name -- the declaring member's
 * simple name joined to the accessor keyword by a dot (`Prop.get`,
 * `Evt.add`). The accessor_declaration's `name` field IS the keyword
 * token (verified); the owner is the accessor_list's parent. Returns 0
 * when either cannot be read -- nothing recorded, never guessed. */
static int csharp_accessor_name(oc_arena *a, TSNode aexpr, const char *src, char **out) {
    TSNode list = ts_node_parent(aexpr);
    if (ts_node_is_null(list) || strcmp(ts_node_type(list), "accessor_list") != 0)
        return 0;
    TSNode decl = ts_node_parent(list);
    if (ts_node_is_null(decl))
        return 0;
    const char *dt = ts_node_type(decl);
    if (strcmp(dt, "property_declaration") != 0 && strcmp(dt, "event_declaration") != 0)
        return 0;
    TSNode kw = ts_node_child_by_field_name(aexpr, "name", 4);
    if (ts_node_is_null(kw))
        return 0;
    TSNode nm = ts_node_child_by_field_name(decl, "name", 4);
    if (ts_node_is_null(nm))
        return 0;
    char *kw_txt = node_text(a, kw, src);
    char *base = node_text(a, nm, src);
    if (!kw_txt || kw_txt[0] == '\0' || !base || base[0] == '\0')
        return 0;
    *out = oc_arena_sprintf(a, "%s.%s", base, kw_txt);
    return *out != NULL;
}

/* Kotlin (SPRINT-22, the C# accessor precedent): the accessor keyword
 * is the node type itself (`getter` / `setter`), and the owning
 * property_declaration is the accessor's nearest PRECEDING SIBLING --
 * the pinned grammar parses the accessor as a sibling of the
 * property_declaration under the same body node (class_body /
 * source_file / enum_class_body), NOT as its child (probed at
 * adoption: ts_node_parent(getter) is class_body). The name is the
 * owner's variable_declaration identifier; the honest qualified name is
 * `Prop.get` / `Prop.set`. Returns 0 when no preceding property exists
 * (never guessed). */
static int kotlin_accessor_name(oc_arena *a, TSNode aexpr, const char *src, char **out) {
    TSNode body = ts_node_parent(aexpr);
    if (ts_node_is_null(body))
        return 0;
    TSNode decl = null_tsnode();
    uint32_t nc = ts_node_child_count(body);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(body, i);
        if (ts_node_is_named(c) && strcmp(ts_node_type(c), "property_declaration") == 0)
            decl = c; /* the LAST property before the accessor wins */
        else if (!ts_node_is_null(decl) && ts_node_eq(c, aexpr))
            break; /* the accessor itself: the tracked property is the owner */
    }
    if (ts_node_is_null(decl))
        return 0;
    /* the accessor KEYWORD (`get` / `set`), not the node type spelling */
    const char *kw_txt = strcmp(ts_node_type(aexpr), "setter") == 0 ? "set" : "get";
    uint32_t dc = ts_node_child_count(decl);
    for (uint32_t i = 0; i < dc; i++) {
        TSNode c = ts_node_child(decl, i);
        if (!ts_node_is_named(c) || strcmp(ts_node_type(c), "variable_declaration") != 0)
            continue;
        uint32_t vc = ts_node_child_count(c);
        for (uint32_t j = 0; j < vc; j++) {
            TSNode v = ts_node_child(c, j);
            if (ts_node_is_named(v) && strcmp(ts_node_type(v), "simple_identifier") == 0) {
                char *base = node_text(a, v, src);
                if (!base || base[0] == '\0')
                    return 0;
                *out = oc_arena_sprintf(a, "%s.%s", base, kw_txt);
                return *out != NULL;
            }
        }
    }
    return 0;
}

/* C# (SPRINT-14): one dotted namespace chain per namespace declaration.
 * `namespace A.B {}` carries the dotted name in ONE node; nested block
 * namespaces join their ancestors with dots (innermost first, reversed
 * on join). Preprocessor wrappers and declaration_list wrappers are
 * skipped while climbing. Depth is bounded -- a pathological chain
 * records nothing, never a wrong name. */
static char *csharp_ns_chain(oc_arena *a, TSNode n, const char *src) {
    char *texts[32];
    size_t depth = 0;
    TSNode cur = n;
    while (!ts_node_is_null(cur)) {
        const char *t = ts_node_type(cur);
        if (strcmp(t, "namespace_declaration") == 0 ||
            strcmp(t, "file_scoped_namespace_declaration") == 0) {
            TSNode nm = ts_node_child_by_field_name(cur, "name", 4);
            if (ts_node_is_null(nm))
                return NULL;
            if (depth >= 32)
                return NULL;
            char *txt = node_text(a, nm, src);
            if (!txt || txt[0] == '\0')
                return NULL;
            texts[depth++] = txt;
            cur = ts_node_parent(cur);
            continue;
        }
        if (strcmp(t, "compilation_unit") == 0)
            break;
        cur = ts_node_parent(cur); /* declaration_list / preproc wrappers */
    }
    if (depth == 0)
        return NULL;
    size_t len = 0;
    for (size_t i = 0; i < depth; i++)
        len += strlen(texts[depth - 1 - i]) + 1;
    char *out = oc_arena_alloc(a, len);
    if (!out)
        return NULL;
    size_t w = 0;
    for (size_t i = 0; i < depth; i++) {
        const char *seg = texts[depth - 1 - i];
        size_t sl = strlen(seg);
        if (w)
            out[w++] = '.';
        memcpy(out + w, seg, sl);
        w += sl;
    }
    out[w] = '\0';
    return out;
}

/* C# (SPRINT-14): record the file's declared dotted namespace(s) as
 * parse hints -- the Go package_clause channel -- for same-namespace
 * OC005 marking. The walk descends everywhere, so preprocessor-wrapped
 * and nested namespaces are all recorded. Returns 0 on OOM. */
static int csharp_record_namespaces(oc_arena *a, TSNode root, const char *src, oc_hint_vec *hints,
                                    const char *path) {
    err_frame *free_list = NULL;
    err_frame *stack = NULL;
    err_frame *rf = (err_frame *)oc_arena_alloc(a, sizeof(*rf));
    if (!rf)
        return 0;
    rf->node = root;
    rf->next = NULL;
    stack = rf;
    while (stack) {
        err_frame *cur = stack;
        stack = cur->next;
        TSNode n = cur->node;
        cur->next = free_list;
        free_list = cur;
        const char *t = ts_node_type(n);
        if (strcmp(t, "namespace_declaration") == 0 ||
            strcmp(t, "file_scoped_namespace_declaration") == 0) {
            char *ns = csharp_ns_chain(a, n, src);
            if (ns && !oc_surface_hint_push_namespace(hints, a, path, ns))
                return 0;
        }
        uint32_t nc = ts_node_child_count(n);
        for (uint32_t i = 0; i < nc; i++) {
            err_frame *nf = free_list;
            if (nf) {
                free_list = nf->next;
            } else {
                nf = (err_frame *)oc_arena_alloc(a, sizeof(*nf));
                if (!nf)
                    return 0;
            }
            nf->node = ts_node_child(n, i);
            nf->next = stack;
            stack = nf;
        }
    }
    return 1;
}

/* Rust (SPRINT-15): TRUE when the bodyless mod_item is preceded by a
 * sibling attribute_item whose text carries `path = ` -- the attribute
 * MOVES the file, so probing the conventional name could fabricate a
 * WRONG edge (docs/LANGUAGES.md Rust section 5). Any other attribute
 * (`#[cfg(...)]`, ...) does not move the file: the conventional probe
 * stands (the C# `#if` keep-discipline: both branches stay analyzed,
 * over-report never hides). */
static int rust_mod_is_path_attributed(TSNode mod_item, const char *src) {
    TSNode prev = ts_node_prev_sibling(mod_item);
    while (!ts_node_is_null(prev)) {
        if (ts_node_is_named(prev)) {
            const char *t = ts_node_type(prev);
            if (strcmp(t, "attribute_item") == 0) {
                uint32_t s = ts_node_start_byte(prev), e = ts_node_end_byte(prev);
                size_t len = (size_t)(e - s);
                if (len >= 6 && memcmp(src + s, "#[path", 6) == 0)
                    return 1;
                return 0; /* first preceding attribute decides; a second
                           * attribute never overrides the first */
            }
            break; /* a non-attribute named sibling ends the walk */
        }
        prev = ts_node_prev_sibling(prev); /* skip anonymous trivia */
    }
    return 0;
}

/* Rust (SPRINT-15): the leaf binding name -- the LAST path segment of
 * the pre-alias leaf (the target's own export name; the alias is a
 * local rename and never a marking predicate). A glob leaf (`::*`)
 * binds NULL (fail-open). Returns an arena copy or NULL (glob / empty /
 * OOM -- the caller pushes the wildcard mark for NULL). */
static char *rust_leaf_bind_name(oc_arena *a, const char *leaf) {
    if (!leaf || leaf[0] == '\0')
        return NULL;
    size_t len = strlen(leaf);
    if (len >= 3 && strcmp(leaf + len - 3, "::*") == 0)
        return NULL; /* glob: fail-open wildcard */
    const char *cut = strrchr(leaf, ':');
    if (cut && cut > leaf && *(cut - 1) == ':')
        return oc_arena_strdup(a, cut + 1);
    return oc_arena_strdup(a, leaf);
}

/* Rust (SPRINT-15): expand one use-tree node into its leaf specifiers,
 * joining the accumulated `prefix` with `::` (the verified node shapes:
 * identifier / scoped_identifier / bare crate-self-super / use_as_clause
 * / use_wildcard / scoped_use_list / nested use_list). Each leaf pushes
 * ONE import record and ONE bind record (the leaf segment pre-alias;
 * globs bind NULL). Returns 0 on OOM or on exceeding the nesting budget
 * (the caller aborts the parse -- fail loud, never silently partial).
 *
 * H-2 (P-2): this walk used to recurse once per nesting level with no
 * depth limit while allocating an ever-longer prefix at every level --
 * a 500 KB Rust file with a 100,000-level use-tree overflowed the C
 * stack (SIGSEGV, ASan: rust_use_expand parser_ts.c:4052/4075, evidence
 * T7) and accumulated O(depth^2) prefix bytes in the arena. The explicit
 * depth budget bounds both; the parse-level tree-depth guard in
 * oc_parser_ts_parse provides the outer defense. */
#define OC_RUST_USE_MAX_DEPTH 256
static int rust_use_expand(oc_arena *a, TSNode n, const char *src, const char *prefix,
                           const char *from_path, oc_import_vec *imports, oc_bind_vec *binds,
                           oc_lang_family family, int *oom, oc_status *st, int depth) {
    if (*oom)
        return 0;
    if (depth >= OC_RUST_USE_MAX_DEPTH) {
        oc_status_set(st, OC_ERR_PARSE,
                      "Rust use-tree in '%s' nests more than %d levels deep -- over the "
                      "expansion budget; refusing (fail loud, never guessed)",
                      from_path, OC_RUST_USE_MAX_DEPTH);
        return 0;
    }
    const char *t = ts_node_type(n);
    if (strcmp(t, "identifier") == 0 || strcmp(t, "scoped_identifier") == 0 ||
        strcmp(t, "crate") == 0 || strcmp(t, "self") == 0 || strcmp(t, "super") == 0 ||
        strcmp(t, "use_wildcard") == 0) {
        char *txt = node_text(a, n, src);
        if (!txt)
            txt = oc_arena_strdup(a, "");
        char *spec;
        if (prefix[0] != '\0')
            spec = oc_arena_sprintf(a, "%s::%s", prefix, txt ? txt : "");
        else
            spec = oc_arena_strdup(a, txt ? txt : "");
        if (!spec) {
            *oom = 1;
            return 0;
        }
        if (imports && !oc_imports_push(imports, a, from_path, spec, family)) {
            *oom = 1;
            return 0;
        }
        if (binds) {
            char *bname = rust_leaf_bind_name(a, spec);
            if (!oc_surface_bind_push(binds, a, from_path, spec, bname, family)) {
                *oom = 1;
                return 0;
            }
        }
        return 1;
    }
    if (strcmp(t, "use_as_clause") == 0) {
        TSNode path = ts_node_child_by_field_name(n, "path", 4);
        if (ts_node_is_null(path))
            return 1; /* unreadable path: nothing recorded, never guessed */
        char *txt = node_text(a, path, src);
        if (!txt)
            return 1;
        char *spec;
        if (prefix[0] != '\0')
            spec = oc_arena_sprintf(a, "%s::%s", prefix, txt);
        else
            spec = oc_arena_strdup(a, txt);
        if (!spec) {
            *oom = 1;
            return 0;
        }
        if (imports && !oc_imports_push(imports, a, from_path, spec, family)) {
            *oom = 1;
            return 0;
        }
        if (binds) {
            char *bname = rust_leaf_bind_name(a, spec);
            if (!oc_surface_bind_push(binds, a, from_path, spec, bname, family)) {
                *oom = 1;
                return 0;
            }
        }
        return 1;
    }
    if (strcmp(t, "use_list") == 0) {
        /* a brace group: every named child is itself a use-tree node
         * (commas are anonymous); nested groups pass the prefix through
         * unchanged (the relative paths join at the leaf emitters) */
        uint32_t nc = ts_node_child_count(n);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode c = ts_node_child(n, i);
            if (!ts_node_is_named(c))
                continue;
            if (!rust_use_expand(a, c, src, prefix, from_path, imports, binds, family, oom, st,
                                 depth + 1))
                return 0;
        }
        return 1;
    }
    if (strcmp(t, "scoped_use_list") == 0) {
        TSNode path = ts_node_child_by_field_name(n, "path", 4);
        TSNode list = ts_node_child_by_field_name(n, "list", 4);
        char *ptxt = ts_node_is_null(path) ? NULL : node_text(a, path, src);
        char *new_prefix;
        if (!ptxt) {
            new_prefix = oc_arena_strdup(a, prefix);
        } else if (prefix[0] != '\0') {
            new_prefix = oc_arena_sprintf(a, "%s::%s", prefix, ptxt);
        } else {
            new_prefix = oc_arena_strdup(a, ptxt);
        }
        if (!new_prefix) {
            *oom = 1;
            return 0;
        }
        if (ts_node_is_null(list))
            return 1;
        return rust_use_expand(a, list, src, new_prefix, from_path, imports, binds, family, oom, st,
                               depth + 1);
    }
    /* metavariable and anything else: nothing recorded, never guessed */
    return 1;
}

/* Ruby (SPRINT-16): the require call shape -- the method text must be
 * `require` or `require_relative` (receiver forms like Kernel.require
 * keep the same method field), and the argument list must carry EXACTLY
 * ONE named child of type `string` whose named children are all
 * `string_content` (Kernel#require takes exactly one STATIC argument; a
 * multi-argument call, a non-string argument, an interpolated or heredoc
 * string would fabricate a path that is not what the code loads -- the
 * guard records NOTHING in those shapes, never guessed). On success *out
 * is the recorded specifier carrying its own channel tag
 * (`require_relative p` / `require p` -- the Rust `mod ` precedent).
 * Returns 0 when the shape is not the honest one (nothing recorded). */
static int ruby_require_shape(oc_arena *a, TSNode meth, TSNode args, const char *src, char **out) {
    *out = NULL;
    if (ts_node_is_null(meth) || ts_node_is_null(args))
        return 0;
    char *mname = node_text(a, meth, src);
    if (!mname)
        return 0;
    int relative;
    if (strcmp(mname, "require_relative") == 0)
        relative = 1;
    else if (strcmp(mname, "require") == 0)
        relative = 0;
    else
        return 0;
    TSNode arg;
    memset(&arg, 0, sizeof(arg));
    uint32_t nc = ts_node_child_count(args);
    int n_named = 0;
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(args, i);
        if (!ts_node_is_named(c))
            continue;
        if (strcmp(ts_node_type(c), "string") != 0)
            return 0; /* a non-string argument: not the honest shape */
        if (++n_named > 1)
            return 0; /* Kernel#require takes exactly one argument */
        arg = c;
    }
    if (n_named != 1)
        return 0;
    /* interpolation / heredoc guard: every NAMED child of the string must
     * be plain string_content (a dynamic payload is never guessed) */
    uint32_t sc = ts_node_child_count(arg);
    for (uint32_t i = 0; i < sc; i++) {
        TSNode c = ts_node_child(arg, i);
        if (!ts_node_is_named(c))
            continue;
        if (strcmp(ts_node_type(c), "string_content") != 0)
            return 0;
    }
    char *payload = string_payload(a, arg, src);
    if (!payload || payload[0] == '\0')
        return 0;
    *out = oc_arena_sprintf(a, "%s %s", relative ? "require_relative" : "require", payload);
    return *out != NULL;
}

/* PHP (SPRINT-19): the require/include expression shape -- the node must
 * be one of the four expression kinds and the argument list must carry
 * EXACTLY ONE named child of type `string` whose named children are all
 * `string_content` (include takes exactly one STATIC argument; a
 * multi-child argument list, a concatenation, an interpolated or
 * variable argument would fabricate a path that is not what the code
 * loads -- the guard records NOTHING in those shapes, never guessed).
 * On success *out is the recorded specifier carrying its own channel
 * tag (`require p` / `require_once p` / `include p` /
 * `include_once p` -- the Rust `mod ` precedent; a use record is a bare
 * qualified name and cannot carry a space, so the two PHP channels
 * cannot collide). Returns 0 when the shape is not the honest one
 * (nothing recorded). */
static int php_include_shape(oc_arena *a, TSNode expr, const char *src, char **out) {
    *out = NULL;
    const char *t = ts_node_type(expr);
    const char *kw;
    if (strcmp(t, "require_expression") == 0)
        kw = "require";
    else if (strcmp(t, "require_once_expression") == 0)
        kw = "require_once";
    else if (strcmp(t, "include_expression") == 0)
        kw = "include";
    else if (strcmp(t, "include_once_expression") == 0)
        kw = "include_once";
    else
        return 0;
    TSNode arg;
    memset(&arg, 0, sizeof(arg));
    uint32_t nc = ts_node_child_count(expr);
    int n_named = 0;
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(expr, i);
        if (!ts_node_is_named(c))
            continue;
        if (strcmp(ts_node_type(c), "string") != 0)
            return 0; /* a non-string argument: not the honest shape */
        if (++n_named > 1)
            return 0; /* exactly ONE static string child (the guard) */
        arg = c;
    }
    if (n_named != 1)
        return 0;
    /* interpolation guard: every NAMED child of the string must be plain
     * string_content (a dynamic payload is never guessed) */
    uint32_t sc = ts_node_child_count(arg);
    for (uint32_t i = 0; i < sc; i++) {
        TSNode c = ts_node_child(arg, i);
        if (!ts_node_is_named(c))
            continue;
        if (strcmp(ts_node_type(c), "string_content") != 0)
            return 0;
    }
    char *payload = string_payload(a, arg, src);
    if (!payload || payload[0] == '\0')
        return 0;
    *out = oc_arena_sprintf(a, "%s %s", kw, payload);
    return *out != NULL;
}

/* C (SPRINT-20): the include directive's honest static shapes. The path
 * child of a preproc_include (verified by the adoption probe) is one of:
 *   - string_literal: the quoted form, payload = the string content ->
 *     specifier `include <p>`;
 *   - system_lib_string: the angle form, a LEAF node whose text carries
 *     the angle brackets -> specifier `system <p>`;
 *   - identifier / call_expression: a macro path (#include HEADER) -- the
 *     payload is not static text, NOTHING is recorded (never guessed).
 * Payload guards shared with the PHP include channel: non-empty, no
 * control characters, no backslash separators (non-portable; also the
 * Ruby/PHP escape-guard discipline). Returns 0 when the shape is not an
 * honest static path (nothing recorded). */
static int c_include_shape(oc_arena *a, TSNode path, const char *src, char **out) {
    *out = NULL;
    if (ts_node_is_null(path))
        return 0;
    const char *t = ts_node_type(path);
    const char *kw;
    if (strcmp(t, "string_literal") == 0) {
        kw = "include";
    } else if (strcmp(t, "system_lib_string") == 0) {
        kw = "system";
    } else {
        return 0; /* a macro path: not a static payload */
    }
    char *payload;
    if (strcmp(t, "string_literal") == 0) {
        payload = string_payload(a, path, src);
    } else {
        char *raw = node_text(a, path, src);
        size_t len = raw ? strlen(raw) : 0;
        if (len >= 2 && raw[0] == '<' && raw[len - 1] == '>') {
            raw[len - 1] = '\0';
            payload = oc_arena_strdup(a, raw + 1);
        } else {
            payload = NULL;
        }
    }
    if (!payload || payload[0] == '\0')
        return 0;
    for (const char *q = payload; *q; q++)
        if (*q == '\\' || (unsigned char)*q < 0x20)
            return 0;
    *out = oc_arena_sprintf(a, "%s %s", kw, payload);
    return *out != NULL;
}

/* PHP (SPRINT-19): the binding name of one use clause -- the LAST
 * backslash segment of the qualified name (the simple name the resolved
 * file's export carries). The `as` alias is the importing file's LOCAL
 * name and never narrows the marking (the C# alias discipline): binding
 * the alias would false-positive OC005 on the declared name. */
static char *php_use_bind_name(oc_arena *a, const char *qname) {
    const char *last = strrchr(qname, '\\');
    return oc_arena_strdup(a, last ? last + 1 : qname);
}

/* PHP (SPRINT-19): the import target of one use clause -- the clause's
 * own `name` or `qualified_name` child (no name FIELD exists on
 * namespace_use_clause; the target comes before the optional
 * function/const keyword tokens and the as-alias, so the FIRST named
 * child of those two types is the qualified name as written). */
static char *php_clause_target(oc_arena *a, TSNode clause, const char *src) {
    uint32_t k = ts_node_child_count(clause);
    for (uint32_t i = 0; i < k; i++) {
        TSNode c = ts_node_child(clause, i);
        if (!ts_node_is_named(c))
            continue;
        if (node_is(c, "name") || node_is(c, "qualified_name"))
            return node_text(a, c, src);
    }
    return NULL;
}

/* PHP (SPRINT-19): expand one namespace_use_declaration into its clause
 * records -- one import + one bind per clause (the Rust use-tree
 * expansion precedent). Shapes (verified at adoption):
 *   - direct clauses as named children (each: an optional function/
 *     const keyword token, a name/qualified_name child, an optional
 *     alias FIELD behind `as`);
 *   - the grouped form: a namespace_name prefix + a body field holding
 *     a namespace_use_group whose clauses are the same shapes (the
 *     group prefix prepends).
 * `use function`/`use const` clauses record the SAME record shape (the
 * resolver's classmap discipline decides resolution). Returns 0 on OOM
 * (the caller aborts the parse -- fail loud, never silently partial). */
static int php_use_expand(oc_arena *a, TSNode decl, const char *src, const char *from_path,
                          oc_import_vec *imports, oc_bind_vec *binds, oc_lang_family family,
                          int *oom) {
    char prefix[512];
    prefix[0] = '\0';
    TSNode group = null_tsnode();
    uint32_t nc = ts_node_child_count(decl);
    /* pass 1: find the grouped form's prefix + body */
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(decl, i);
        if (!ts_node_is_named(c))
            continue;
        if (strcmp(ts_node_type(c), "namespace_name") == 0) {
            char *txt = node_text(a, c, src);
            if (!txt)
                return 1;
            if (snprintf(prefix, sizeof(prefix), "%s", txt) >= (int)sizeof(prefix))
                return 1;
        } else if (strcmp(ts_node_type(c), "namespace_use_group") == 0) {
            group = c;
        }
    }
    if (!ts_node_is_null(group)) {
        uint32_t gc = ts_node_child_count(group);
        for (uint32_t i = 0; i < gc; i++) {
            TSNode clause = ts_node_child(group, i);
            if (!ts_node_is_named(clause) ||
                strcmp(ts_node_type(clause), "namespace_use_clause") != 0)
                continue;
            char *nm = php_clause_target(a, clause, src);
            if (!nm || nm[0] == '\0')
                continue; /* unreadable clause: nothing recorded, never guessed */
            char *full =
                prefix[0] ? oc_arena_sprintf(a, "%s\\%s", prefix, nm) : oc_arena_strdup(a, nm);
            if (!full) {
                *oom = 1;
                return 0;
            }
            if (imports && !oc_imports_push(imports, a, from_path, full, family)) {
                *oom = 1;
                return 0;
            }
            if (binds) {
                /* the alias never narrows marking: the binding is the
                 * clause's own last segment (the C# alias discipline) */
                char *bname = php_use_bind_name(a, full);
                if (!bname || !oc_surface_bind_push(binds, a, from_path, full, bname, family)) {
                    *oom = 1;
                    return 0;
                }
            }
        }
        return 1;
    }
    /* pass 2: direct clauses */
    for (uint32_t i = 0; i < nc; i++) {
        TSNode clause = ts_node_child(decl, i);
        if (!ts_node_is_named(clause) || strcmp(ts_node_type(clause), "namespace_use_clause") != 0)
            continue;
        char *nm = php_clause_target(a, clause, src);
        if (!nm || nm[0] == '\0')
            continue; /* unreadable clause: nothing recorded, never guessed */
        if (imports && !oc_imports_push(imports, a, from_path, nm, family)) {
            *oom = 1;
            return 0;
        }
        if (binds) {
            char *bname = php_use_bind_name(a, nm);
            if (!bname || !oc_surface_bind_push(binds, a, from_path, nm, bname, family)) {
                *oom = 1;
                return 0;
            }
        }
    }
    return 1;
}

/*
 * H-5: syntax-tree depth budget. gprof on a 30,000-deep nested-call
 * chain: 95% of wall time inside ts_query_cursor__advance -- the
 * vendored tree-sitter query engine is quadratic on adversarially deep
 * trees, and every pass in this file (functions, accessors, imports,
 * exports, surface) runs query cursors over the same root. Legitimate
 * code nests orders of magnitude below this budget (the deepest pinned
 * fixture, brace_bomb.js, sits near ~2k raw bracket levels); anything
 * deeper is hostile input and is refused LOUDLY before any cursor runs,
 * per the project contract (scan.c aborts the run with this message).
 * The probe itself is one linear arena-stack DFS (the diagnose_tree /
 * count_complexity pattern) -- no quadratic behavior, no recursion.
 */
#define OC_TS_MAX_TREE_DEPTH 8192

static uint32_t ts_max_depth(oc_arena *a, TSNode root) {
    typedef struct dframe {
        TSNode node;
        uint32_t depth;
        struct dframe *next;
    } dframe;
    dframe *free_list = NULL;
    dframe *stack = NULL;
    uint32_t maxd = 0;

    dframe *rootf = (dframe *)oc_arena_alloc(a, sizeof(*rootf));
    if (!rootf)
        return 0; /* OOM: caller fails loud, never fails open */
    rootf->node = root;
    rootf->depth = 1;
    rootf->next = NULL;
    stack = rootf;

    while (stack) {
        dframe *cur = stack;
        stack = cur->next;
        TSNode n = cur->node;
        uint32_t d = cur->depth;
        cur->next = free_list;
        free_list = cur;

        if (d > maxd)
            maxd = d;
        uint32_t nc = ts_node_child_count(n);
        for (uint32_t i = 0; i < nc; i++) {
            dframe *nf = free_list;
            if (nf) {
                free_list = nf->next;
            } else {
                nf = (dframe *)oc_arena_alloc(a, sizeof(*nf));
                if (!nf)
                    return 0; /* OOM: caller fails loud */
            }
            nf->node = ts_node_child(n, i);
            nf->depth = d + 1;
            nf->next = stack;
            stack = nf;
        }
    }
    return maxd;
}

oc_code oc_parser_ts_parse(oc_parser *p, const oc_parser_input *in, oc_graph *g, oc_parse_out *out,
                           oc_status *st) {
    struct oc_lang_entry *e = find_lang(p, in->grammar);
    if (!e) {
        oc_status_set(st, OC_ERR_PARSE,
                      "no grammar '%s' compiled into this binary (pack "
                      "references an unsupported language)",
                      in->grammar);
        return OC_ERR_PARSE;
    }

    /* The parser is shared across languages: select the grammar for THIS
     * file before parsing (it was last set during backend init). */
    ts_parser_reset(p->parser);
    if (!ts_parser_set_language(p->parser, e->lang)) {
        oc_status_set(st, OC_ERR_INTERNAL, "grammar ABI mismatch for '%s' on parse", in->grammar);
        return OC_ERR_INTERNAL;
    }
    TSTree *tree = ts_parser_parse_string(p->parser, NULL, in->source, (uint32_t)in->len);
    if (!tree) {
        oc_status_set(st, OC_ERR_PARSE, "tree-sitter failed on '%s'", in->path);
        return OC_ERR_PARSE;
    }
    TSNode root = ts_tree_root_node(tree);

    /* H-5 guard: measure the tree depth BEFORE any query cursor runs
     * (diagnose, functions, imports, exports, surface...). One linear
     * pass; OOM fails loud, never fails open. */
    uint32_t tree_depth = ts_max_depth(p->arena, root);
    if (tree_depth == 0) {
        ts_tree_delete(tree);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory measuring syntax-tree depth of '%s'",
                      in->path);
        return OC_ERR_ALLOC;
    }
    if (tree_depth > OC_TS_MAX_TREE_DEPTH) {
        ts_tree_delete(tree);
        oc_status_set(st, OC_ERR_PARSE,
                      "syntax tree of '%s' nests %u levels deep -- over the %d-level "
                      "analysis budget; refusing to scan (query evaluation is quadratic "
                      "on such trees)",
                      in->path, tree_depth, OC_TS_MAX_TREE_DEPTH);
        return OC_ERR_PARSE;
    }

    if (out)
        diagnose_tree(p->arena, root, &out->diag);

    /* Shebang hint for entry-point classification (OC006). A `#!` line
     * prefix is a shebang ONLY when the `!` is not followed by `[`:
     * Rust inner attributes (`#![allow(...)]` -- SPRINT-15, measured on
     * the pinned corpus) are item-ish attributes at file top, not
     * executable-line claims; a kernel shebang interpreter path can
     * never start with `[`. PHP (SPRINT-19) is excluded by contract:
     * outside PHP blocks a `#!` line is template TEXT (not a comment,
     * not a kernel claim the way the CLI conventions use it) and the
     * PHP pack claims its runner scripts by NAME conventions only
     * (index.php and the bin glob) -- docs/LANGUAGES.md PHP section 6. */
    if (out && out->hints && e->family != OC_LANG_PHP && in->len >= 2 && in->source[0] == '#' &&
        in->source[1] == '!' && !(in->len >= 3 && in->source[2] == '[')) {
        if (!oc_surface_hint_push(out->hints, p->arena, in->path, 1)) {
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording file hints");
            return OC_ERR_ALLOC;
        }
    }

    /* Package-clause parse fact (Go, SPRINT-10): recorded as a hint so
     * the surface build can claim `package main` files as entry points
     * (docs/LANGUAGES.md §Go.6). Exactly one package_clause per file. */
    if (out && out->hints && e->q_package) {
        TSQueryCursor *pcur = ts_query_cursor_new();
        ts_query_cursor_exec(pcur, e->q_package, root);
        TSQueryMatch pm;
        int hint_fail = 0;
        if (ts_query_cursor_next_match(pcur, &pm)) {
            TSNode pkg = match_capture(e->q_package, &pm, "pkg");
            if (!ts_node_is_null(pkg)) {
                char *pname = node_text(p->arena, pkg, in->source);
                if (!pname || !oc_surface_hint_push_package(out->hints, p->arena, in->path, pname))
                    hint_fail = 1;
            }
        }
        ts_query_cursor_delete(pcur);
        if (hint_fail) {
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording package hint");
            return OC_ERR_ALLOC;
        }
    }

    /* main-method parse fact (Java SPRINT-12, C# SPRINT-13): recorded
     * as a hint so the surface build can claim main-method files as
     * entry points (docs/LANGUAGES.md Java section / C# section 6).
     * Java: method named `main` with modifiers carrying both `public`
     * and `static`. C#: method named `Main` whose modifier set carries
     * `static` -- accessibility NOT required (C# accepts non-public
     * entry points; the fact fires on the declaration as parsed).
     * First matching method wins; one hint per file. */
    if (out && out->hints && e->q_main) {
        TSQueryCursor *mcur = ts_query_cursor_new();
        ts_query_cursor_exec(mcur, e->q_main, root);
        TSQueryMatch mm;
        int main_fail = 0;
        while (ts_query_cursor_next_match(mcur, &mm)) {
            TSNode jname = match_capture(e->q_main, &mm, "jname");
            if (ts_node_is_null(jname))
                continue;
            char *nm = node_text(p->arena, jname, in->source);
            if (!nm)
                continue;
            if (e->family == OC_LANG_CSHARP) {
                TSNode jdecl = match_capture(e->q_main, &mm, "jdecl");
                if (ts_node_is_null(jdecl))
                    continue;
                if (strcmp(nm, "Main") != 0)
                    continue;
                if (!node_has_modifier_text(jdecl, in->source, "static"))
                    continue;
            } else if (e->family == OC_LANG_RUST) {
                /* Rust (SPRINT-15): the source_file-anchored function_item
                 * named `main` (the query's source_file anchor keeps
                 * impl/trait methods and nested items out -- the fact
                 * fires only on the top-level declaration). */
                if (strcmp(nm, "main") != 0)
                    continue;
            } else if (e->family == OC_LANG_C || e->family == OC_LANG_CPP) {
                /* C (SPRINT-20) and C++ (SPRINT-21): a function DEFINITION
                 * named `main` (the query requires a body, so a lone
                 * prototype claims nothing; the query's identifier-only
                 * inner declarator keeps C++ member functions and
                 * out-of-class definitions out -- probed). C/C++ functions
                 * cannot nest, so no anchor is needed; a main behind an
                 * #ifdef wrapper still matches (the honest over-claim
                 * direction; OC006 severity is INFO and triage decides). */
                if (strcmp(nm, "main") != 0)
                    continue;
            } else if (e->family == OC_LANG_KOTLIN) {
                /* Kotlin (SPRINT-22): the source_file-anchored function
                 * DECLARATION with a body named `main` (the Rust branch
                 * shape; the query's anchor keeps class/object members
                 * out -- probed: a member `fun main` claims nothing).
                 * Both entry spellings match: `fun main()` and
                 * `fun main(args: Array<String>)`. */
                if (strcmp(nm, "main") != 0)
                    continue;
            } else if (e->family == OC_LANG_SCALA) {
                /* Scala (SPRINT-24): BOTH entry spellings carry the
                 * name in the capture -- the object-main shape (a
                 * function_definition named `main` with a parameters
                 * list inside an object_definition) and the Scala 3
                 * `@main` annotation shape (the annotation's name-field
                 * type_identifier). The object need not be top-level --
                 * the honest over-claim direction, the C #ifdef note
                 * (OC006 severity INFO). */
                if (strcmp(nm, "main") != 0)
                    continue;
            } else {
                continue; /* no main-fact semantics for this family */
            }
            if (!oc_surface_hint_push_main(out->hints, p->arena, in->path))
                main_fail = 1;
            break; /* one hint per file is enough to claim it */
        }
        ts_query_cursor_delete(mcur);
        if (main_fail) {
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording main-method hint");
            return OC_ERR_ALLOC;
        }
    }

    /* Top-level-statements parse fact (C# 9, SPRINT-13): a
     * compilation_unit with a `global_statement` direct child IS the
     * program entry point by specification -- recorded as a hint, the
     * same class of cheap parse fact as the Go package_clause
     * (docs/LANGUAGES.md C# section 6). One hint per
     * file; the query matches at most the direct children. */
    if (out && out->hints && e->q_top) {
        TSQueryCursor *tcur = ts_query_cursor_new();
        ts_query_cursor_exec(tcur, e->q_top, root);
        TSQueryMatch tm;
        int top_fail = 0;
        int claimed = 0;
        while (!claimed && ts_query_cursor_next_match(tcur, &tm)) {
            /* Swift (SPRINT-23): a `#if` condition call rides the same
             * query (the grammar flattens directives) -- the directive
             * builtin set keeps the version check out; every other
             * statement shape claims (docs/LANGUAGES.md Swift section
             * 6). */
            if (e->family == OC_LANG_SWIFT) {
                TSNode stmt = match_capture(e->q_top, &tm, "stmt");
                if (!ts_node_is_null(stmt) &&
                    swift_is_directive_condition(p->arena, stmt, in->source))
                    continue;
            }
            if (!oc_surface_hint_push_top(out->hints, p->arena, in->path))
                top_fail = 1;
            claimed = 1;
        }
        ts_query_cursor_delete(tcur);
        if (top_fail) {
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording top-level-statements hint");
            return OC_ERR_ALLOC;
        }
    }

    /* Declared-namespace parse fact (C#, SPRINT-14): the file's dotted
     * namespace(s) recorded as hints for same-namespace OC005 marking --
     * the same class of cheap parse fact as the Go package_clause
     * (docs/LANGUAGES.md C# section 5). */
    if (out && out->hints && e->family == OC_LANG_CSHARP) {
        if (!csharp_record_namespaces(p->arena, root, in->source, out->hints, in->path)) {
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording namespace hints");
            return OC_ERR_ALLOC;
        }
    }

    /* Swift manifest target fact (SPRINT-23): a Package.swift's
     * declared (target, path) pairs recorded as hints -- the resolver
     * v1-swift's module channel, the manifest-declared facts
     * (docs/LANGUAGES.md Swift section 5). The filename check is SPM's
     * own fixed convention, never a heuristic. */
    if (out && out->hints && e->family == OC_LANG_SWIFT && swift_is_manifest_path(in->path)) {
        int sw_fail = 0;
        swift_manifest_walk(root, p->arena, in->source, out->hints, in->path, 0, &sw_fail);
        if (sw_fail) {
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording Swift target hints");
            return OC_ERR_ALLOC;
        }
    }

    oc_node_id file_id = oc_graph_add_file(g, in->path);
    if (file_id == OC_NODE_INVALID) {
        ts_tree_delete(tree);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building graph");
        return OC_ERR_ALLOC;
    }

    /* Functions: (decl_line, DEFINITION-EXTENT span) + CONTAINS edges.
     * OBS-2 fix (Sprint 2): the canonical span is the whole defining
     * construct (fexpr), not the body block -- one semantic for every
     * language. For 1TBS JavaScript this equals the old decl..brace
     * span; for Python it now includes the `def` line, closing the
     * cross-language threshold asymmetry. */
    TSQueryCursor *cur = ts_query_cursor_new();
    ts_query_cursor_exec(cur, e->q_functions, root);
    TSQueryMatch m;
    while (ts_query_cursor_next_match(cur, &m)) {
        TSNode fname = match_capture(e->q_functions, &m, "fname");
        TSNode fbody = match_capture(e->q_functions, &m, "fbody");
        TSNode fexpr = match_capture(e->q_functions, &m, "fexpr");
        if (ts_node_is_null(fbody))
            continue;
        /* Swift (SPRINT-23): the pinned grammar gives init/deinit no
         * readable name node (the `name` FIELD is the anonymous `init`
         * token -- probed), so their query alternatives carry NO fname
         * capture and the C side synthesizes the honest spellings (the
         * C# constructor precedent: the defining construct names it). */
        int synth_name = 0;
        if (e->family == OC_LANG_SWIFT && ts_node_is_null(fname)) {
            const char *ft = ts_node_type(fexpr);
            if (strcmp(ft, "init_declaration") == 0 || strcmp(ft, "deinit_declaration") == 0)
                synth_name = 1;
            else
                continue; /* a named-shape match without a name: unreadable */
        }
        if ((ts_node_is_null(fname) && !synth_name))
            continue;
        /* C and C++ (SPRINT-20/21): the capture carries the whole
         * function_declarator -- the grammar has no name field, so the
         * symbol name is the identifier at the bottom of the declarator
         * chain (pointer returns, parenthesized and fn-pointer-return
         * shapes included; the cpp walk adds field_identifier, the
         * qualified_identifier name field and the as-written
         * destructor/operator spellings), resolved here, never guessed. */
        if (e->family == OC_LANG_C || e->family == OC_LANG_CPP)
            fname = c_declarator_ident(fname);
        if (ts_node_is_null(fname) && !synth_name)
            continue;
        char *name = NULL;
        if (synth_name) {
            name = oc_arena_sprintf(
                p->arena, "%s",
                strcmp(ts_node_type(fexpr), "init_declaration") == 0 ? "init" : "deinit");
        } else {
            name = node_text(p->arena, fname, in->source);
        }
        if (!name || name[0] == '\0')
            continue;
        /* Go (SPRINT-14): promoted function-value assignments must be the
         * honest shapes only -- single-name left side, non-blank name,
         * plain `=` operator for the assignment shape. */
        if (e->family == OC_LANG_GO) {
            if (strcmp(name, "_") == 0)
                continue; /* blank identifier: discarded, not a name */
            const char *ft = ts_node_type(fexpr);
            if (strcmp(ft, "short_var_declaration") == 0 ||
                strcmp(ft, "assignment_statement") == 0 || strcmp(ft, "var_spec") == 0) {
                if (!go_promotion_single_name(fexpr))
                    continue;
                if (strcmp(ft, "assignment_statement") == 0 &&
                    !go_plain_assign_op(fexpr, in->source))
                    continue;
            }
        }
        size_t def_start = (size_t)ts_node_start_point(fexpr).row + 1;
        size_t def_end = (size_t)ts_node_end_point(fexpr).row + 1;
        size_t decl_line =
            ts_node_is_null(fexpr) ? (size_t)ts_node_start_point(fname).row + 1 : def_start;
        size_t comp = 1 + count_complexity(p->arena, fbody, e->cx_kinds, e->cx_ops, e->cx_ops_node,
                                           in->source, e->family);
        oc_node_id sid =
            oc_graph_add_symbol(g, OC_NODE_FUNCTION, name, in->path, decl_line, def_start, def_end);
        if (sid == OC_NODE_INVALID) {
            ts_query_cursor_delete(cur);
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory building graph");
            return OC_ERR_ALLOC;
        }
        g->nodes[sid].complexity = comp;

        /* SPRINT-17 (B3): attach the ordered parameter signature. OOM
         * keeps the honest failure mode of the rest of the graph build
         * (hard error, never a silently-missing signature). */
        oc_param *sig = NULL;
        size_t n_sig = 0;
        if (!extract_params(p->arena, fexpr, e->family, in->source, &sig, &n_sig)) {
            ts_query_cursor_delete(cur);
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory extracting signature");
            return OC_ERR_ALLOC;
        }
        if (!oc_graph_set_params(g, sid, sig, n_sig)) {
            ts_query_cursor_delete(cur);
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory storing signature");
            return OC_ERR_ALLOC;
        }

        /* Python: every top-level function is importable surface unless
         * the file declares __all__ (handled at surface build). */
        if (out && out->exports && e->family == OC_LANG_PY && py_is_top_level(fexpr) &&
            name[0] != '_') {
            if (!oc_surface_export_push(out->exports, p->arena, in->path, name, def_start, 0)) {
                ts_query_cursor_delete(cur);
                ts_tree_delete(tree);
                oc_status_set(st, OC_ERR_ALLOC, "out of memory recording exports");
                return OC_ERR_ALLOC;
            }
        }
    }
    ts_query_cursor_delete(cur);

    /* Java bodyless methods (SPRINT-14): abstract and interface methods
     * ARE symbols -- the declaration itself is the definition extent,
     * complexity exactly 1 (no body, no decisions; JaCoCo never carries
     * counters for them, so OC007 honestly marks them uncovered). The
     * negated-field query cannot match a bodyful method twice. */
    if (e->q_bodyless) {
        TSQueryCursor *bcur = ts_query_cursor_new();
        ts_query_cursor_exec(bcur, e->q_bodyless, root);
        TSQueryMatch bm;
        while (ts_query_cursor_next_match(bcur, &bm)) {
            TSNode bname = match_capture(e->q_bodyless, &bm, "fname");
            TSNode bexpr = match_capture(e->q_bodyless, &bm, "fexpr");
            if (ts_node_is_null(bname) || ts_node_is_null(bexpr))
                continue;
            char *bname_txt = node_text(p->arena, bname, in->source);
            if (!bname_txt || bname_txt[0] == '\0')
                continue;
            size_t bs = (size_t)ts_node_start_point(bexpr).row + 1;
            size_t be = (size_t)ts_node_end_point(bexpr).row + 1;
            oc_node_id sid =
                oc_graph_add_symbol(g, OC_NODE_FUNCTION, bname_txt, in->path, bs, bs, be);
            if (sid == OC_NODE_INVALID) {
                ts_query_cursor_delete(bcur);
                ts_tree_delete(tree);
                oc_status_set(st, OC_ERR_ALLOC, "out of memory building graph");
                return OC_ERR_ALLOC;
            }
            g->nodes[sid].complexity = 1;

            /* SPRINT-17 (B3): bodyless declarations carry caller-visible
             * signatures too (Java abstract/interface, Rust traits, Ruby
             * endless defs) -- extract and attach like bodyful ones. */
            oc_param *bsig = NULL;
            size_t n_bsig = 0;
            if (!extract_params(p->arena, bexpr, e->family, in->source, &bsig, &n_bsig)) {
                ts_query_cursor_delete(bcur);
                ts_tree_delete(tree);
                oc_status_set(st, OC_ERR_ALLOC, "out of memory extracting signature");
                return OC_ERR_ALLOC;
            }
            if (!oc_graph_set_params(g, sid, bsig, n_bsig)) {
                ts_query_cursor_delete(bcur);
                ts_tree_delete(tree);
                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing signature");
                return OC_ERR_ALLOC;
            }
        }
        ts_query_cursor_delete(bcur);
    }

    /* C# accessors (SPRINT-14): property/event accessors with a real body
     * are symbols named `Declaring.get` etc.; their decisions were
     * previously counted toward NOTHING (properties are not symbols and
     * class members have no enclosing function -- the accounting hole the
     * promotion closes). Inclusive discipline unchanged for nested
     * lambdas: they count toward the accessor now, too. */
    if (e->q_accessors) {
        TSQueryCursor *acur = ts_query_cursor_new();
        ts_query_cursor_exec(acur, e->q_accessors, root);
        TSQueryMatch am;
        while (ts_query_cursor_next_match(acur, &am)) {
            TSNode abody = match_capture(e->q_accessors, &am, "fbody");
            TSNode aexpr = match_capture(e->q_accessors, &am, "fexpr");
            if (ts_node_is_null(abody) || ts_node_is_null(aexpr))
                continue;
            char *aname = NULL;
            int acc_ok = 0;
            if (e->family == OC_LANG_KOTLIN)
                acc_ok = kotlin_accessor_name(p->arena, aexpr, in->source, &aname);
            else if (e->family == OC_LANG_SWIFT)
                acc_ok = swift_accessor_name(p->arena, aexpr, in->source, &aname);
            else
                acc_ok = csharp_accessor_name(p->arena, aexpr, in->source, &aname);
            if (!acc_ok)
                continue; /* unreadable declaring name or keyword: nothing recorded */
            size_t as = (size_t)ts_node_start_point(aexpr).row + 1;
            size_t ae = (size_t)ts_node_end_point(aexpr).row + 1;
            size_t acomp = 1 + count_complexity(p->arena, abody, e->cx_kinds, e->cx_ops,
                                                e->cx_ops_node, in->source, e->family);
            oc_node_id sid = oc_graph_add_symbol(g, OC_NODE_FUNCTION, aname, in->path, as, as, ae);
            if (sid == OC_NODE_INVALID) {
                ts_query_cursor_delete(acur);
                ts_tree_delete(tree);
                oc_status_set(st, OC_ERR_ALLOC, "out of memory building graph");
                return OC_ERR_ALLOC;
            }
            g->nodes[sid].complexity = acomp;
        }
        ts_query_cursor_delete(acur);
    }

    /* Raw import records: resolved into edges after ALL files are parsed
     * (imports.c), so edges only point at files inside this analysis.
     * The same loop records named BINDINGS for the module surface
     * (OC005/OC006 usage marking). */
    if (out && (out->imports || out->binds)) {
        TSQueryCursor *icur = ts_query_cursor_new();
        ts_query_cursor_exec(icur, e->q_imports, root);
        while (ts_query_cursor_next_match(icur, &m)) {
            TSNode ical = match_capture(e->q_imports, &m, "ical");
            TSNode isrc = match_capture(e->q_imports, &m, "isrc");
            TSNode iname = match_capture(e->q_imports, &m, "iname");
            TSNode imod = match_capture(e->q_imports, &m, "imod");
            TSNode ist = match_capture(e->q_imports, &m, "ist");
            TSNode est = match_capture(e->q_imports, &m, "est");
            TSNode ifs = match_capture(e->q_imports, &m, "ifs");
            TSNode ispec = match_capture(e->q_imports, &m, "ispec");
            TSNode imeth = match_capture(e->q_imports, &m, "imeth");
            TSNode ial = match_capture(e->q_imports, &m, "ial");
            const char *spec = NULL;
            char *java_bind = NULL;   /* Java: the binding name (NULL = wildcard) */
            char *csharp_bind = NULL; /* C#: the static form binds the type simple name */
            char *kt_bind = NULL;     /* Kotlin (SPRINT-22): the binding name
                                       * (NULL = wildcard or ambiguous member) */
            if (!ts_node_is_null(isrc)) {
                if (ts_node_is_null(ical)) {
                    spec = string_payload(p->arena, isrc, in->source); /* static import/export */
                } else {
                    char *callee = node_text(p->arena, ical, in->source);
                    if (callee && (strcmp(callee, "require") == 0 || strcmp(callee, "import") == 0))
                        spec = string_payload(p->arena, isrc, in->source);
                }
            } else if (!ts_node_is_null(iname)) {
                spec = node_text(p->arena, iname, in->source);
            } else if (!ts_node_is_null(imod)) {
                spec = node_text(p->arena, imod, in->source);
            } else if (!ts_node_is_null(ist) && e->family == OC_LANG_CSHARP) {
                char *cspec = NULL;
                /* on failure cspec stays NULL and spec stays NULL: the
                 * empty-spec check below skips cleanly (nothing
                 * recorded, never guessed) */
                if (csharp_import_shape(p->arena, ist, in->source, &cspec, &csharp_bind))
                    spec = cspec;
            } else if (!ts_node_is_null(ist) && e->family == OC_LANG_JAVA) {
                char *jspec = NULL;
                /* on failure java_bind stays NULL and spec stays NULL:
                 * the empty-spec check below skips cleanly (nothing
                 * recorded, never guessed) */
                if (java_import_shape(p->arena, ist, in->source, &jspec, &java_bind))
                    spec = jspec;
            } else if (!ts_node_is_null(ist) && e->family == OC_LANG_KOTLIN) {
                /* Kotlin (SPRINT-22): the import_header channel is
                 * complete here -- kotlin_import_shape classifies the
                 * four shapes (single / member / wildcard / alias) and
                 * yields one import + one bind per header (both
                 * arena-owned). On failure the spec stays NULL and the
                 * empty-spec check below skips cleanly (nothing
                 * recorded, never guessed). NOTE: this branch must
                 * precede the JS @ist branch -- the capture name is
                 * shared, and the JS clause walk pushes nothing for
                 * Kotlin headers. docs/LANGUAGES.md Kotlin section 5. */
                char *kspec = NULL;
                if (kotlin_import_shape(p->arena, ist, in->source, &kspec, &kt_bind))
                    spec = kspec;
            } else if (!ts_node_is_null(ist) && e->family == OC_LANG_SWIFT) {
                /* Swift (SPRINT-23): the import_declaration channel is
                 * complete here -- swift_import_shape classifies the
                 * shapes (whole-module / submodule / kind-scoped member)
                 * and yields one import + one bind per declaration (both
                 * arena-owned). On failure the spec stays NULL and the
                 * empty-spec check below skips cleanly (nothing
                 * recorded, never guessed). NOTE: this branch must
                 * precede the JS @ist branch -- the capture name is
                 * shared, and the JS clause walk pushes nothing for
                 * Swift declarations. docs/LANGUAGES.md Swift section 5. */
                char *sspec = NULL;
                char *sbind = NULL;
                if (swift_import_shape(p->arena, ist, in->source, &sspec, &sbind)) {
                    spec = sspec;
                    kt_bind = sbind; /* the shared local-bind slot: the
                                      * push below is family-generic */
                }
            } else if (!ts_node_is_null(ist) && e->family == OC_LANG_SCALA) {
                /* Scala (SPRINT-24): the import_declaration channel is
                 * complete in C -- scala_import_expand pushes ONE import
                 * + one bind per leaf (the Rust use-tree / PHP
                 * use-declaration precedent; the selector group expands
                 * to one record per selector), so the match is finished
                 * here. On failure the spec stays NULL and the empty-spec
                 * check below skips cleanly (nothing recorded, never
                 * guessed). NOTE: this branch must precede the JS @ist
                 * branch -- the capture name is shared, and the JS clause
                 * walk pushes nothing for Scala declarations.
                 * docs/LANGUAGES.md Scala section 5. */
                int oom = 0;
                scala_import_expand(p->arena, ist, in->source, in->path, out->imports, out->binds,
                                    e->family, &oom);
                if (oom) {
                    ts_query_cursor_delete(icur);
                    ts_tree_delete(tree);
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory recording import records");
                    return OC_ERR_ALLOC;
                }
                continue; /* the scala channel is complete for this match */
            } else if (!ts_node_is_null(ist) && e->family == OC_LANG_RUBY) {
                /* Ruby (SPRINT-16): the require/require_relative call
                 * channel -- the shape guard (method text + exactly one
                 * static string argument) is applied in C; on failure the
                 * spec stays NULL and the empty-spec check below skips
                 * cleanly (nothing recorded, never guessed). */
                char *rspec = NULL;
                if (ruby_require_shape(p->arena, imeth, ial, in->source, &rspec))
                    spec = rspec;
            } else if (!ts_node_is_null(ist) && e->family == OC_LANG_PHP) {
                /* PHP (SPRINT-19): BOTH channels are complete in C --
                 * the use-declaration expansion pushes one import + one
                 * bind per clause (the Rust use-tree precedent), and the
                 * include channel pushes one import with a NULL bind
                 * (a whole-file claim: requiring a file loads all its
                 * top-level declarations -- the Ruby require discipline).
                 * Either way the match is finished here.
                 * docs/LANGUAGES.md PHP section 5. */
                if (strcmp(ts_node_type(ist), "namespace_use_declaration") == 0) {
                    int oom = 0;
                    php_use_expand(p->arena, ist, in->source, in->path, out->imports, out->binds,
                                   e->family, &oom);
                    if (oom) {
                        ts_query_cursor_delete(icur);
                        ts_tree_delete(tree);
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory recording use records");
                        return OC_ERR_ALLOC;
                    }
                    continue; /* the use channel is complete for this match */
                }
                char *pspec = NULL;
                if (php_include_shape(p->arena, ist, in->source, &pspec)) {
                    if (out->imports &&
                        !oc_imports_push(out->imports, p->arena, in->path, pspec, e->family)) {
                        ts_query_cursor_delete(icur);
                        ts_tree_delete(tree);
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory recording include");
                        return OC_ERR_ALLOC;
                    }
                    if (out->binds && !oc_surface_bind_push(out->binds, p->arena, in->path, pspec,
                                                            NULL, e->family)) {
                        ts_query_cursor_delete(icur);
                        ts_tree_delete(tree);
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory recording include binding");
                        return OC_ERR_ALLOC;
                    }
                }
                continue; /* the include channel is complete for this match */
            } else if (!ts_node_is_null(ist) &&
                       (e->family == OC_LANG_C || e->family == OC_LANG_CPP)) {
                /* C (SPRINT-20) and C++ (SPRINT-21): the include channel is
                 * complete in C -- the shape guard classifies the two static
                 * forms (quoted -> `include p`, angle -> `system p`) and a
                 * macro path records NOTHING. One import + one bind with a
                 * NULL name (a whole-file claim: including a header makes
                 * all its declarations visible -- the Ruby require
                 * discipline). The cpp family shares the shape guard and
                 * the resolver verbatim (docs/LANGUAGES.md C++ section 5). */
                char *pspec = NULL;
                if (c_include_shape(p->arena, match_capture(e->q_imports, &m, "ipath"), in->source,
                                    &pspec)) {
                    if (out->imports &&
                        !oc_imports_push(out->imports, p->arena, in->path, pspec, e->family)) {
                        ts_query_cursor_delete(icur);
                        ts_tree_delete(tree);
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory recording include");
                        return OC_ERR_ALLOC;
                    }
                    if (out->binds && !oc_surface_bind_push(out->binds, p->arena, in->path, pspec,
                                                            NULL, e->family)) {
                        ts_query_cursor_delete(icur);
                        ts_tree_delete(tree);
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory recording include binding");
                        return OC_ERR_ALLOC;
                    }
                }
                continue; /* the include channel is complete for this match */
            }
            if (!ts_node_is_null(imod) && e->family == OC_LANG_RUST) {
                /* the mod-tree channel (SPRINT-15): a bodyless mod_item
                 * records `mod <name>` UNLESS a #[path = ...] attribute
                 * moved the file (the conventional probe would fabricate
                 * a WRONG edge). The record pushes NO bind: declaring a
                 * module is not using any of its items (docs/LANGUAGES.md
                 * Rust section 5, channel 1). */
                TSNode mname = match_capture(e->q_imports, &m, "imodname");
                if (!ts_node_is_null(mname) && !rust_mod_is_path_attributed(imod, in->source)) {
                    char *mn = node_text(p->arena, mname, in->source);
                    if (mn && mn[0] != '\0') {
                        char *mspec = oc_arena_sprintf(p->arena, "mod %s", mn);
                        if (!mspec) {
                            ts_query_cursor_delete(icur);
                            ts_tree_delete(tree);
                            oc_status_set(st, OC_ERR_ALLOC,
                                          "out of memory recording mod declaration");
                            return OC_ERR_ALLOC;
                        }
                        if (out->imports &&
                            !oc_imports_push(out->imports, p->arena, in->path, mspec, e->family)) {
                            ts_query_cursor_delete(icur);
                            ts_tree_delete(tree);
                            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording imports");
                            return OC_ERR_ALLOC;
                        }
                    }
                }
                continue; /* the mod channel is complete for this match */
            }
            if (!ts_node_is_null(ist) && e->family == OC_LANG_RUST) {
                /* the use-tree channel (SPRINT-15): the C side expands
                 * the argument use-tree into leaf specifiers (heads,
                 * groups, aliases, globs), one import + one bind per
                 * leaf (docs/LANGUAGES.md Rust section 5, channel 2). */
                TSNode arg = ts_node_child_by_field_name(ist, "argument", 8);
                if (!ts_node_is_null(arg)) {
                    int oom = 0;
                    if (!rust_use_expand(p->arena, arg, in->source, "", in->path, out->imports,
                                         out->binds, e->family, &oom, st, 0)) {
                        ts_query_cursor_delete(icur);
                        ts_tree_delete(tree);
                        /* OOM and depth-budget failures both abort the
                         * parse loudly; the status already carries the
                         * depth message in the non-OOM case. */
                        if (oom)
                            oc_status_set(st, OC_ERR_ALLOC,
                                          "out of memory recording use-tree records");
                        return oom ? OC_ERR_ALLOC : st->code;
                    }
                }
                continue; /* the use channel is complete for this match */
            }
            if (!spec || spec[0] == '\0')
                continue;
            if (out->imports &&
                !oc_imports_push(out->imports, p->arena, in->path, spec, e->family)) {
                ts_query_cursor_delete(icur);
                ts_tree_delete(tree);
                oc_status_set(st, OC_ERR_ALLOC, "out of memory recording imports");
                return OC_ERR_ALLOC;
            }

            /* ---- named bindings for the module surface ---- */
            if (out->binds) {
                int bind_fail = 0;
                if (!ts_node_is_null(ist) && e->family == OC_LANG_CSHARP) {
                    /* C# (SPRINT-13): the using directive binds the type
                     * simple name for the STATIC form (the resolved
                     * type's export); plain namespace claims and alias
                     * claims bind NULL (fail-open marking; the alias is
                     * surface data in the specifier and never a
                     * narrowing predicate -- docs/LANGUAGES.md C#
                     * section 5/6). NOTE: this branch must precede the
                     * JS @ist branch -- the capture name is shared, and
                     * the JS clause walk pushes nothing for C#
                     * directives. */
                    if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, csharp_bind,
                                              e->family))
                        bind_fail = 1;
                } else if (!ts_node_is_null(ist) && e->family == OC_LANG_JAVA) {
                    /* Java (SPRINT-12): the import declaration binds the
                     * class simple name (single-type and static shapes;
                     * the member segment of a static import is already
                     * stripped from the SPECIFIER, and the binding is
                     * the resolved class's export name); package
                     * wildcards bind NULL (fail-open,
                     * docs/LANGUAGES.md Java section). NOTE: this branch
                     * must precede the JS @ist branch -- the capture
                     * name is shared, and the JS clause walk pushes
                     * nothing for Java declarations. */
                    if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, java_bind,
                                              e->family))
                        bind_fail = 1;
                } else if (!ts_node_is_null(ist) && e->family == OC_LANG_KOTLIN) {
                    /* Kotlin (SPRINT-22): the header binds the ORIGINAL
                     * last segment for the two-segment single shape
                     * (the alias is local surface detail, never a
                     * narrowing predicate -- the Go alias note); the
                     * wildcard and the ambiguous member shape bind NULL
                     * (fail-open whole-file marking, the Ruby require /
                     * C++ include discipline). NOTE: this branch must
                     * precede the JS @ist branch. */
                    if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, kt_bind,
                                              e->family))
                        bind_fail = 1;
                } else if (!ts_node_is_null(ist) && e->family == OC_LANG_SWIFT) {
                    /* Swift (SPRINT-23): the kind-scoped member claim
                     * binds the LAST segment (the member the source
                     * named -- the over-mark direction bounded by the
                     * declared facts); the whole-module and submodule
                     * claims bind NULL (fail-open wholesale marking --
                     * importing a module loads ALL of it, the Kotlin
                     * wildcard discipline). NOTE: this branch must
                     * precede the JS @ist branch. */
                    if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, kt_bind,
                                              e->family))
                        bind_fail = 1;
                } else if (!ts_node_is_null(ist) && e->family == OC_LANG_RUBY) {
                    /* Ruby (SPRINT-16): a require is a WHOLE-FILE claim
                     * (requiring a file loads all its top-level constants
                     * -- the JS side-effect-import fail-open discipline);
                     * the binding name is NULL and the resolved target
                     * file's exports are all marked used
                     * (docs/LANGUAGES.md Ruby section 5). NOTE: this
                     * branch must precede the JS @ist branch -- the
                     * capture name is shared, and the JS clause walk
                     * pushes nothing for Ruby calls. */
                    if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, NULL,
                                              e->family))
                        bind_fail = 1;
                } else if (!ts_node_is_null(ist)) {
                    /* JS import statement: default / named / namespace. */
                    uint32_t nc = ts_node_child_count(ist);
                    for (uint32_t i = 0; i < nc && !bind_fail; i++) {
                        TSNode c = ts_node_child(ist, i);
                        if (!ts_node_is_named(c))
                            continue;
                        const char *t = ts_node_type(c);
                        if (strcmp(t, "import_clause") == 0) {
                            uint32_t k = ts_node_child_count(c);
                            int any = 0;
                            for (uint32_t j = 0; j < k; j++) {
                                TSNode b = ts_node_child(c, j);
                                if (!ts_node_is_named(b))
                                    continue;
                                const char *bt = ts_node_type(b);
                                if (strcmp(bt, "identifier") == 0) {
                                    any = 1;
                                    if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec,
                                                              "default", e->family))
                                        bind_fail = 1;
                                } else if (strcmp(bt, "namespace_import") == 0) {
                                    any = 1;
                                    if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec,
                                                              NULL, e->family))
                                        bind_fail = 1;
                                } else if (strcmp(bt, "named_imports") == 0) {
                                    any = 1;
                                    uint32_t q = ts_node_child_count(b);
                                    for (uint32_t q2 = 0; q2 < q; q2++) {
                                        TSNode sp = ts_node_child(b, q2);
                                        if (!ts_node_is_named(sp))
                                            continue;
                                        TSNode nm = ts_node_child_by_field_name(sp, "name", 4);
                                        if (ts_node_is_null(nm))
                                            continue;
                                        char *n2 = node_text(p->arena, nm, in->source);
                                        if (!n2 ||
                                            !oc_surface_bind_push(out->binds, p->arena, in->path,
                                                                  spec, n2, e->family))
                                            bind_fail = 1;
                                    }
                                }
                            }
                            if (!any) {
                                /* side-effect import: fail-open wildcard */
                                if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec,
                                                          NULL, e->family))
                                    bind_fail = 1;
                            }
                        }
                    }
                } else if (!ts_node_is_null(est)) {
                    /* JS re-export: `export {a, b as c} from './y'` or
                     * `export * from './y'` -- marks SOURCE file usage. */
                    uint32_t nc = ts_node_child_count(est);
                    int any = 0;
                    for (uint32_t i = 0; i < nc && !bind_fail; i++) {
                        TSNode c = ts_node_child(est, i);
                        if (!ts_node_is_named(c) || strcmp(ts_node_type(c), "export_clause") != 0)
                            continue;
                        uint32_t k = ts_node_child_count(c);
                        for (uint32_t j = 0; j < k; j++) {
                            TSNode sp = ts_node_child(c, j);
                            if (!ts_node_is_named(sp))
                                continue;
                            TSNode nm = ts_node_child_by_field_name(sp, "name", 4);
                            if (ts_node_is_null(nm))
                                continue;
                            char *n2 = node_text(p->arena, nm, in->source);
                            if (!n2)
                                continue;
                            any = 1;
                            if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, n2,
                                                      e->family))
                                bind_fail = 1;
                        }
                    }
                    if (!any && !oc_surface_bind_push(out->binds, p->arena, in->path, spec, NULL,
                                                      e->family))
                        bind_fail = 1; /* export * from: wildcard */
                } else if (!ts_node_is_null(ifs)) {
                    /* Python from-import: names are the non-module children. */
                    TSNode modnode = ts_node_child_by_field_name(ifs, "module_name", 11);
                    uint32_t nc = ts_node_child_count(ifs);
                    int any = 0;
                    for (uint32_t i = 0; i < nc && !bind_fail; i++) {
                        TSNode c = ts_node_child(ifs, i);
                        if (!ts_node_is_named(c))
                            continue;
                        const char *t = ts_node_type(c);
                        if (strcmp(t, "dotted_name") == 0) {
                            if (!ts_node_is_null(modnode) && ts_node_eq(c, modnode))
                                continue; /* the module itself, not a name */
                            char *n2 = node_text(p->arena, c, in->source);
                            if (!n2)
                                continue;
                            any = 1;
                            if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, n2,
                                                      e->family))
                                bind_fail = 1;
                        } else if (strcmp(t, "aliased_import") == 0) {
                            TSNode nm = ts_node_child_by_field_name(c, "name", 4);
                            if (ts_node_is_null(nm))
                                continue;
                            char *n2 = node_text(p->arena, nm, in->source);
                            if (!n2)
                                continue;
                            any = 1;
                            if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, n2,
                                                      e->family))
                                bind_fail = 1;
                        } else if (strcmp(t, "wildcard_import") == 0) {
                            any = 1;
                            if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, NULL,
                                                      e->family))
                                bind_fail = 1;
                        }
                    }
                    if (!any && !oc_surface_bind_push(out->binds, p->arena, in->path, spec, NULL,
                                                      e->family))
                        bind_fail = 1; /* `from . import pkg` or bare: fail-open */
                } else if (!ts_node_is_null(ical)) {
                    /* require()/dynamic import(): names unknown at parse
                     * time -- fail-open wildcard. */
                    if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, NULL,
                                              e->family))
                        bind_fail = 1;
                } else if (!ts_node_is_null(ispec) && e->family == OC_LANG_GO) {
                    /* Go (SPRINT-10): the binding is the alias when
                     * aliased, NULL otherwise (plain, blank `_`, dot
                     * `.`). Marking stays package-wide fail-open by
                     * contract; the alias is surface data, never a
                     * narrowing predicate (docs/LANGUAGES.md §Go.5). */
                    TSNode nm = ts_node_child_by_field_name(ispec, "name", 4);
                    char *bname = NULL;
                    if (!ts_node_is_null(nm) && strcmp(ts_node_type(nm), "package_identifier") == 0)
                        bname = node_text(p->arena, nm, in->source);
                    if (!oc_surface_bind_push(out->binds, p->arena, in->path, spec, bname,
                                              e->family))
                        bind_fail = 1;
                }
                if (bind_fail) {
                    ts_query_cursor_delete(icur);
                    ts_tree_delete(tree);
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory recording import bindings");
                    return OC_ERR_ALLOC;
                }
            }
        }
        ts_query_cursor_delete(icur);
    }

    /* Module surface: exports. JS: every export_statement node (default,
     * clauses, re-exports, declarations) handled in C. Python: classes +
     * __all__ lists (functions were recorded in the function loop). */
    if (out && out->exports) {
        int export_fail = 0;
        if (e->family == OC_LANG_JS) {
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode es = match_capture(e->q_exports, &m, "es");
                if (ts_node_is_null(es))
                    continue;
                size_t line = (size_t)ts_node_start_point(es).row + 1;
                int is_default = node_has_anon_child_text(es, in->source, "default");

                TSNode clause = null_tsnode();
                TSNode src = ts_node_child_by_field_name(es, "source", 6);
                uint32_t nc = ts_node_child_count(es);
                for (uint32_t i = 0; i < nc; i++) {
                    TSNode c = ts_node_child(es, i);
                    if (ts_node_is_named(c) && strcmp(ts_node_type(c), "export_clause") == 0) {
                        clause = c;
                        break;
                    }
                }
                if (!ts_node_is_null(clause)) {
                    /* export {a, b as c} [from './y'] */
                    uint32_t k = ts_node_child_count(clause);
                    for (uint32_t j = 0; j < k && !export_fail; j++) {
                        TSNode sp = ts_node_child(clause, j);
                        if (!ts_node_is_named(sp))
                            continue;
                        TSNode nm = ts_node_child_by_field_name(sp, "name", 4);
                        if (ts_node_is_null(nm))
                            continue;
                        char *local = node_text(p->arena, nm, in->source);
                        if (!local)
                            continue;
                        TSNode alias = ts_node_child_by_field_name(sp, "alias", 5);
                        char *pub =
                            ts_node_is_null(alias) ? local : node_text(p->arena, alias, in->source);
                        if (!pub)
                            continue;
                        if (!oc_surface_export_push(out->exports, p->arena, in->path, pub, line, 0))
                            export_fail = 1;
                        if (!ts_node_is_null(src)) {
                            /* re-export marks the SOURCE file's export used */
                            char *yspec = string_payload(p->arena, src, in->source);
                            if (yspec && out->binds &&
                                !oc_surface_bind_push(out->binds, p->arena, in->path, yspec, local,
                                                      e->family))
                                export_fail = 1;
                        }
                    }
                } else if (is_default) {
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, "default", line,
                                                0))
                        export_fail = 1;
                } else {
                    /* export <declaration>: every declared name is surface. */
                    uint32_t k = ts_node_child_count(es);
                    int any = 0;
                    for (uint32_t j = 0; j < k && !export_fail; j++) {
                        TSNode c = ts_node_child(es, j);
                        if (!ts_node_is_named(c))
                            continue;
                        const char *t = ts_node_type(c);
                        if (strcmp(t, "export_clause") == 0)
                            continue;
                        /* declaration/value child: pull its declared names */
                        TSNode decl = c;
                        TSNode direct = ts_node_child_by_field_name(decl, "name", 4);
                        if (!ts_node_is_null(direct)) {
                            char *n2 = node_text(p->arena, direct, in->source);
                            if (n2) {
                                any = 1;
                                if (!oc_surface_export_push(out->exports, p->arena, in->path, n2,
                                                            line, 0))
                                    export_fail = 1;
                            }
                            continue;
                        }
                        /* lexical/variable declaration: all declarators */
                        uint32_t q = ts_node_child_count(decl);
                        for (uint32_t q2 = 0; q2 < q; q2++) {
                            TSNode d2 = ts_node_child(decl, q2);
                            if (!ts_node_is_named(d2))
                                continue;
                            TSNode nm = ts_node_child_by_field_name(d2, "name", 4);
                            if (ts_node_is_null(nm))
                                continue;
                            char *n2 = node_text(p->arena, nm, in->source);
                            if (!n2)
                                continue;
                            any = 1;
                            if (!oc_surface_export_push(out->exports, p->arena, in->path, n2, line,
                                                        0))
                                export_fail = 1;
                        }
                    }
                    if (!any && ts_node_is_null(src)) {
                        /* anonymous default or opaque export: keep honest
                         * with the pseudo-name only when default-marked;
                         * otherwise nothing is recordable. */
                        if (is_default && !oc_surface_export_push(out->exports, p->arena, in->path,
                                                                  "default", line, 0))
                            export_fail = 1;
                    }
                }
            }
            ts_query_cursor_delete(xcur);

            /* CommonJS channel (BUGFIX N-3): module.exports / exports
             * assignments and Object.assign export merges, extracted on
             * a second cursor pass AFTER the ESM pass so the two
             * channels never interleave. The ESM pass above only saw
             * export_statement nodes, so every CommonJS module exported
             * NOTHING as far as the module surface was concerned -- the
             * v25.0.0 -> HEAD lib/ compat gate reported SAFE while
             * functions were disappearing from lib/. Recorded names:
             * object-literal keys (quoted keys unquoted), the member
             * name of `exports.x = ...` / `module.exports.x = ...`, the
             * keys of Object.assign export merges, and the pseudo-name
             * "default" for an opaque wholesale reassignment. Never
             * guessed: computed keys, spreads, nested member chains and
             * opaque RHS shapes are recorded as NOTHING. */
            if (!export_fail && e->q_exports_cjs) {
                TSQueryCursor *ccur = ts_query_cursor_new();
                if (!ccur) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory creating query cursor");
                    return OC_ERR_ALLOC;
                }
                ts_query_cursor_exec(ccur, e->q_exports_cjs, root);
                char **seen = NULL; /* per-file CJS dedup (arena-owned names) */
                size_t n_seen = 0, cap_seen = 0;
                int seen_oom = 0;
                while (ts_query_cursor_next_match(ccur, &m) && !export_fail) {
                    TSNode cjasg = match_capture(e->q_exports_cjs, &m, "cjasg");
                    if (!ts_node_is_null(cjasg)) {
                        TSNode cjsleft = match_capture(e->q_exports_cjs, &m, "cjsleft");
                        TSNode cjsrhs = match_capture(e->q_exports_cjs, &m, "cjsrhs");
                        if (ts_node_is_null(cjsleft) || ts_node_is_null(cjsrhs))
                            continue;
                        size_t line = (size_t)ts_node_start_point(cjasg).row + 1;
                        char *lname = NULL;
                        cjs_target_kind kind =
                            cjs_classify_left(cjsleft, in->source, &lname, p->arena);
                        if (kind == CJS_TARGET_NONE)
                            continue; /* foreign member chain: not an export */
                        if (kind == CJS_TARGET_MEMBER && lname) {
                            if (!cjs_seen_push(&seen, &n_seen, &cap_seen, lname)) {
                                seen_oom = 1;
                                break;
                            }
                            if (!oc_surface_export_push(out->exports, p->arena, in->path, lname,
                                                        line, 0)) {
                                export_fail = 1;
                                break;
                            }
                            continue;
                        }
                        /* whole-module form: the RHS shape decides what
                         * is honestly recordable */
                        int handled = 0;
                        if (strcmp(ts_node_type(cjsrhs), "object") == 0) {
                            int np = 0, opq = 0;
                            if (!cjs_push_object_keys(p->arena, cjsrhs, in->source, in->path, line,
                                                      out->exports, &seen, &n_seen, &cap_seen,
                                                      &seen_oom, &np, &opq)) {
                                export_fail = 1;
                                break;
                            }
                            /* keys recorded (np > 0) or an honestly EMPTY
                             * literal (np == 0, no spread) are final; a
                             * spread-only literal is an unknowable
                             * wholesale change */
                            handled = (np > 0 || !opq);
                        } else if (strcmp(ts_node_type(cjsrhs), "call_expression") == 0) {
                            int rr = cjs_push_object_assign(p->arena, cjsrhs, in->source, in->path,
                                                            line, out->exports, &seen, &n_seen,
                                                            &cap_seen, 0, &seen_oom);
                            if (rr == 0) {
                                export_fail = 1;
                                break;
                            }
                            handled = (rr == 1); /* -1: not Object.assign at all */
                        }
                        if (!handled) {
                            /* opaque wholesale reassignment (identifier,
                             * function, class, require(...), non-assign
                             * call, spread-only literal): the module
                             * surface is the module object itself */
                            if (!cjs_seen_push(&seen, &n_seen, &cap_seen, "default")) {
                                seen_oom = 1;
                                break;
                            }
                            if (!oc_surface_export_push(out->exports, p->arena, in->path, "default",
                                                        line, 0)) {
                                export_fail = 1;
                                break;
                            }
                        }
                        continue;
                    }
                    TSNode oacall = match_capture(e->q_exports_cjs, &m, "oacall");
                    if (!ts_node_is_null(oacall)) {
                        size_t line = (size_t)ts_node_start_point(oacall).row + 1;
                        if (!cjs_push_object_assign(p->arena, oacall, in->source, in->path, line,
                                                    out->exports, &seen, &n_seen, &cap_seen, 1,
                                                    &seen_oom)) {
                            export_fail = 1;
                            break;
                        }
                    }
                }
                ts_query_cursor_delete(ccur);
                free(seen);
                if (export_fail || seen_oom) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory recording CommonJS exports");
                    return OC_ERR_ALLOC;
                }
            }
        } else if (e->family == OC_LANG_GO) {
            /* Go: package-level declarations with an exported (upper-case
             * first rune) name; the query constrains every capture to
             * direct source_file children, so no parent check here
             * (docs/LANGUAGES.md §Go.6). Usage marking cannot happen this
             * sprint (no import bindings until resolver v1-go), which is
             * why OC005 is disabled at pack level. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                static const char *const GO_EXPORT_CAPS[] = {"fname", "tname", "cname", "vname"};
                for (size_t ci = 0; ci < 4 && !export_fail; ci++) {
                    TSNode nm = match_capture(e->q_exports, &m, GO_EXPORT_CAPS[ci]);
                    if (ts_node_is_null(nm))
                        continue;
                    char *name = node_text(p->arena, nm, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    if (name[0] < 'A' || name[0] > 'Z')
                        continue; /* unexported: not package surface */
                    size_t line = (size_t)ts_node_start_point(nm).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                }
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_JAVA) {
            /* Java: top-level type declarations (the query constrains
             * every capture to direct program children); the `public`
             * modifier decides exportedness (docs/LANGUAGES.md Java
             * section). Members are not module surface v1. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode jmods = match_capture(e->q_exports, &m, "jmods");
                TSNode tname = match_capture(e->q_exports, &m, "tname");
                if (ts_node_is_null(jmods) || ts_node_is_null(tname))
                    continue;
                if (!node_has_anon_child_text(jmods, in->source, "public"))
                    continue; /* package-private: not module surface */
                char *name = node_text(p->arena, tname, in->source);
                if (!name || name[0] == '\0')
                    continue;
                size_t line = (size_t)ts_node_start_point(tname).row + 1;
                if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                    export_fail = 1;
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_RUST) {
            /* Rust (SPRINT-15): top-level source_file items with PLAIN
             * `pub` visibility (the modifier node's exact text decides:
             * pub(crate)/pub(super)/pub(in path) carry extra tokens and
             * are NOT module surface -- restricted visibility does not
             * cross the crate boundary, Rust's own rule). The query
             * anchors every capture to direct source_file children, so
             * nested items never become candidates.
             * docs/LANGUAGES.md Rust section 6. */
            static const char *const RUST_EXPORT_CAPS[] = {"rvis", "rname"};
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode rvis = match_capture(e->q_exports, &m, RUST_EXPORT_CAPS[0]);
                TSNode rname = match_capture(e->q_exports, &m, RUST_EXPORT_CAPS[1]);
                if (ts_node_is_null(rvis) || ts_node_is_null(rname))
                    continue;
                char *vis = node_text(p->arena, rvis, in->source);
                if (!vis || strcmp(vis, "pub") != 0)
                    continue; /* restricted visibility: not module surface */
                char *name = node_text(p->arena, rname, in->source);
                if (!name || name[0] == '\0')
                    continue;
                size_t line = (size_t)ts_node_start_point(rname).row + 1;
                if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                    export_fail = 1;
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_RUBY) {
            /* Ruby (SPRINT-16): top-level class/module declarations (the
             * name node's text AS WRITTEN: constant or scope_resolution)
             * plus top-level constant assignments (the query constrains
             * the assignment's left side to a plain `constant`). Ruby has
             * no file-scope visibility modifier, so there is NO
             * exportedness filter -- the query anchors every capture to
             * direct `program` children, so nested classes/modules and
             * member constants never become candidates.
             * docs/LANGUAGES.md Ruby section 6. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode cname = match_capture(e->q_exports, &m, "cname");
                if (ts_node_is_null(cname))
                    continue;
                char *name = node_text(p->arena, cname, in->source);
                if (!name || name[0] == '\0')
                    continue;
                size_t line = (size_t)ts_node_start_point(cname).row + 1;
                if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                    export_fail = 1;
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_PHP) {
            /* PHP (SPRINT-19): top-level class / interface / trait /
             * enum declarations and top-level function names AS WRITTEN,
             * plus the enum's case members and top-level const_element
             * names -- the query anchors every capture to a direct
             * `program` child or a braced namespace's body, so nested
             * classes and members never become candidates. NO
             * exportedness filter: PHP namespace members are all public
             * surface (the Ruby precedent).
             * docs/LANGUAGES.md PHP section 6. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode cname = match_capture(e->q_exports, &m, "cname");
                if (ts_node_is_null(cname))
                    continue;
                char *name = node_text(p->arena, cname, in->source);
                if (!name || name[0] == '\0')
                    continue;
                size_t line = (size_t)ts_node_start_point(cname).row + 1;
                if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                    export_fail = 1;
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_CSHARP) {
            /* C# (SPRINT-13): top-level type declarations; the modifier
             * set carrying `public` decides exportedness (individual
             * named `modifier` children -- verified). The top-level
             * walk climbs namespace (block or file-scoped) and
             * preprocessor wrappers; type nesting is not module
             * surface. Every public top-level type of a file is a
             * candidate (C# has no one-public-type-per-file rule).
             * docs/LANGUAGES.md C# section 6. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode tdecl = match_capture(e->q_exports, &m, "tdecl");
                TSNode tname = match_capture(e->q_exports, &m, "tname");
                if (ts_node_is_null(tdecl) || ts_node_is_null(tname))
                    continue;
                if (!node_has_modifier_text(tdecl, in->source, "public"))
                    continue; /* internal/package-private: not module surface */
                if (!csharp_type_is_toplevel(tdecl))
                    continue; /* nested in a type: not module surface */
                char *name = node_text(p->arena, tname, in->source);
                if (!name || name[0] == '\0')
                    continue;
                size_t line = (size_t)ts_node_start_point(tname).row + 1;
                if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                    export_fail = 1;
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_CPP) {
            /* C++ (SPRINT-21): top-level declarations with C++'s own
             * internal-linkage filter -- the C `static` rule (shared
             * helper) PLUS the anonymous-namespace rule applied by the
             * extended climb (cpp candidates inside an anonymous
             * namespace are internal linkage and never surface). The
             * toplevel climb (c_is_toplevel, extended SPRINT-21) passes
             * preprocessor guards, `extern "C"` blocks (both forms),
             * NAMED namespace bodies and template_declaration wrappers;
             * class/field bodies are never top level. Per capture kind:
             *   - fdef: the declarator-chain identifier (inline methods
             *     are fdef captures but their climb FAILS inside the
             *     class body -- the class is the surface unit, the C#
             *     type-nesting precedent);
             *   - decl: the tag of a DEFINED specifier in the type field
             *     + EVERY declarator-field child separately;
             *   - tdef: the declarator-chain name;
             *   - sspec/uspec/espec/cspec: direct-child tag definitions
             *     (c_specifier_defines_named covers class_specifier);
             *   - adef: the alias_declaration's name field (`using X =
             *     Y;` -- C++'s typedef spelling);
             *   - pdef/pfdef: the macro's name field.
             * using_declaration/using_directive/namespace_alias are not
             * captures (never surface); enumerator constants stay the
             * documented C boundary. docs/LANGUAGES.md C++ section 6. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode fdef = match_capture(e->q_exports, &m, "fdef");
                TSNode decl = match_capture(e->q_exports, &m, "decl");
                TSNode tdef = match_capture(e->q_exports, &m, "tdef");
                TSNode sspec = match_capture(e->q_exports, &m, "sspec");
                TSNode uspec = match_capture(e->q_exports, &m, "uspec");
                TSNode espec = match_capture(e->q_exports, &m, "espec");
                TSNode cspec = match_capture(e->q_exports, &m, "cspec");
                TSNode adef = match_capture(e->q_exports, &m, "adef");
                TSNode pdef = match_capture(e->q_exports, &m, "pdef");
                TSNode pfdef = match_capture(e->q_exports, &m, "pfdef");
                TSNode cand = null_tsnode();
                const char *kind = NULL;
                if (!ts_node_is_null(fdef)) {
                    cand = fdef;
                    kind = "fdef";
                } else if (!ts_node_is_null(decl)) {
                    cand = decl;
                    kind = "decl";
                } else if (!ts_node_is_null(tdef)) {
                    cand = tdef;
                    kind = "tdef";
                } else if (!ts_node_is_null(sspec) || !ts_node_is_null(uspec) ||
                           !ts_node_is_null(espec) || !ts_node_is_null(cspec)) {
                    cand =
                        !ts_node_is_null(sspec)
                            ? sspec
                            : (!ts_node_is_null(uspec) ? uspec
                                                       : (!ts_node_is_null(espec) ? espec : cspec));
                    kind = "spec";
                } else if (!ts_node_is_null(adef)) {
                    cand = adef;
                    kind = "adef";
                } else if (!ts_node_is_null(pdef)) {
                    cand = pdef;
                    kind = "pdef";
                } else if (!ts_node_is_null(pfdef)) {
                    cand = pfdef;
                    kind = "pdef";
                } else {
                    continue;
                }
                if (!c_is_toplevel(cand))
                    continue;
                if (strcmp(kind, "fdef") == 0) {
                    if (c_has_static(cand, in->source))
                        continue; /* internal linkage: not module surface */
                    TSNode dn = ts_node_child_by_field_name(cand, "declarator", 10);
                    TSNode id = ts_node_is_null(dn) ? dn : c_declarator_ident(dn);
                    if (ts_node_is_null(id))
                        continue;
                    char *name = node_text(p->arena, id, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    size_t line = (size_t)ts_node_start_point(id).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                } else if (strcmp(kind, "decl") == 0) {
                    if (c_has_static(cand, in->source))
                        continue;
                    /* the tag of a defined specifier in the type field */
                    TSNode ty = ts_node_child_by_field_name(cand, "type", 4);
                    if (c_specifier_defines_named(ty)) {
                        TSNode tag = ts_node_child_by_field_name(ty, "name", 4);
                        char *name = node_text(p->arena, tag, in->source);
                        if (name && name[0] != '\0' &&
                            !oc_surface_export_push(out->exports, p->arena, in->path, name,
                                                    (size_t)ts_node_start_point(tag).row + 1, 0))
                            export_fail = 1;
                    }
                    /* every declarator separately (multi-declarator lines) */
                    uint32_t dc = ts_node_child_count(cand);
                    for (uint32_t di = 0; di < dc && !export_fail; di++) {
                        TSNode d = ts_node_child(cand, di);
                        if (!ts_node_is_named(d))
                            continue;
                        const char *fname = ts_node_field_name_for_child(cand, di);
                        if (!fname || strcmp(fname, "declarator") != 0)
                            continue;
                        TSNode id = c_declarator_ident(d);
                        if (ts_node_is_null(id))
                            continue;
                        char *name = node_text(p->arena, id, in->source);
                        if (!name || name[0] == '\0')
                            continue;
                        size_t line = (size_t)ts_node_start_point(id).row + 1;
                        if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line,
                                                    0))
                            export_fail = 1;
                    }
                } else if (strcmp(kind, "tdef") == 0) {
                    if (c_has_static(cand, in->source))
                        continue;
                    TSNode dn = ts_node_child_by_field_name(cand, "declarator", 10);
                    TSNode id = ts_node_is_null(dn) ? dn : c_declarator_ident(dn);
                    if (ts_node_is_null(id))
                        continue;
                    char *name = node_text(p->arena, id, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    size_t line = (size_t)ts_node_start_point(id).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                } else if (strcmp(kind, "adef") == 0) {
                    /* `using X = Y;` -- the alias name is the surface
                     * name (the typedef discipline; the RHS type is not
                     * module surface) */
                    TSNode name_node = ts_node_child_by_field_name(cand, "name", 4);
                    if (ts_node_is_null(name_node))
                        continue;
                    char *name = node_text(p->arena, name_node, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    size_t line = (size_t)ts_node_start_point(name_node).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                } else if (strcmp(kind, "spec") == 0) {
                    /* a direct translation_unit tag definition */
                    if (!c_specifier_defines_named(cand))
                        continue;
                    TSNode tag = ts_node_child_by_field_name(cand, "name", 4);
                    char *name = node_text(p->arena, tag, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    size_t line = (size_t)ts_node_start_point(tag).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                } else { /* pdef: object-like or function-like macro */
                    TSNode name_node = ts_node_child_by_field_name(cand, "name", 4);
                    if (ts_node_is_null(name_node))
                        continue;
                    char *name = node_text(p->arena, name_node, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    size_t line = (size_t)ts_node_start_point(name_node).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                }
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_C) {
            /* C (SPRINT-20): top-level declarations with C's own
             * internal-linkage exportedness filter -- a `static` storage
             * class specifier narrows to the file, everything else
             * (including `extern`) is external. The toplevel check climbs
             * preprocessor guards and `extern "C"` blocks
             * (c_is_toplevel). Per capture kind:
             *   - fdef: the declarator-chain identifier (a prototype is a
             *     declaration capture, not a symbol);
             *   - decl: the tag of a DEFINED struct/union/enum in the type
             *     field, then EVERY declarator-field child separately
             *     (`int a, b = 2, c[4];` is three names -- the probe
             *     verified the multi-declarator shape);
             *   - tdef: the declarator-chain name (type_identifier or
             *     deeper for function-pointer typedefs);
             *   - sspec/uspec/espec: direct-child tag definitions;
             *   - pdef/pfdef: the macro's name field.
             * Enum constant names (enumerator) are NOT surface v1 (the
             * documented boundary). docs/LANGUAGES.md C section 6. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode fdef = match_capture(e->q_exports, &m, "fdef");
                TSNode decl = match_capture(e->q_exports, &m, "decl");
                TSNode tdef = match_capture(e->q_exports, &m, "tdef");
                TSNode sspec = match_capture(e->q_exports, &m, "sspec");
                TSNode uspec = match_capture(e->q_exports, &m, "uspec");
                TSNode espec = match_capture(e->q_exports, &m, "espec");
                TSNode pdef = match_capture(e->q_exports, &m, "pdef");
                TSNode pfdef = match_capture(e->q_exports, &m, "pfdef");
                TSNode cand = null_tsnode();
                const char *kind = NULL;
                if (!ts_node_is_null(fdef)) {
                    cand = fdef;
                    kind = "fdef";
                } else if (!ts_node_is_null(decl)) {
                    cand = decl;
                    kind = "decl";
                } else if (!ts_node_is_null(tdef)) {
                    cand = tdef;
                    kind = "tdef";
                } else if (!ts_node_is_null(sspec)) {
                    cand = sspec;
                    kind = "spec";
                } else if (!ts_node_is_null(uspec)) {
                    cand = uspec;
                    kind = "spec";
                } else if (!ts_node_is_null(espec)) {
                    cand = espec;
                    kind = "spec";
                } else if (!ts_node_is_null(pdef)) {
                    cand = pdef;
                    kind = "pdef";
                } else if (!ts_node_is_null(pfdef)) {
                    cand = pfdef;
                    kind = "pdef";
                } else {
                    continue;
                }
                if (!c_is_toplevel(cand))
                    continue;
                if (strcmp(kind, "fdef") == 0) {
                    if (c_has_static(cand, in->source))
                        continue; /* internal linkage: not module surface */
                    TSNode dn = ts_node_child_by_field_name(cand, "declarator", 10);
                    TSNode id = ts_node_is_null(dn) ? dn : c_declarator_ident(dn);
                    if (ts_node_is_null(id))
                        continue;
                    char *name = node_text(p->arena, id, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    size_t line = (size_t)ts_node_start_point(id).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                } else if (strcmp(kind, "decl") == 0) {
                    if (c_has_static(cand, in->source))
                        continue;
                    /* the tag of a defined specifier in the type field */
                    TSNode ty = ts_node_child_by_field_name(cand, "type", 4);
                    if (c_specifier_defines_named(ty)) {
                        TSNode tag = ts_node_child_by_field_name(ty, "name", 4);
                        char *name = node_text(p->arena, tag, in->source);
                        if (name && name[0] != '\0' &&
                            !oc_surface_export_push(out->exports, p->arena, in->path, name,
                                                    (size_t)ts_node_start_point(tag).row + 1, 0))
                            export_fail = 1;
                    }
                    /* every declarator separately (multi-declarator lines) */
                    uint32_t dc = ts_node_child_count(cand);
                    for (uint32_t di = 0; di < dc && !export_fail; di++) {
                        TSNode d = ts_node_child(cand, di);
                        if (!ts_node_is_named(d))
                            continue;
                        const char *fname = ts_node_field_name_for_child(cand, di);
                        if (!fname || strcmp(fname, "declarator") != 0)
                            continue;
                        TSNode id = c_declarator_ident(d);
                        if (ts_node_is_null(id))
                            continue;
                        char *name = node_text(p->arena, id, in->source);
                        if (!name || name[0] == '\0')
                            continue;
                        size_t line = (size_t)ts_node_start_point(id).row + 1;
                        if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line,
                                                    0))
                            export_fail = 1;
                    }
                } else if (strcmp(kind, "tdef") == 0) {
                    if (c_has_static(cand, in->source))
                        continue;
                    TSNode dn = ts_node_child_by_field_name(cand, "declarator", 10);
                    TSNode id = ts_node_is_null(dn) ? dn : c_declarator_ident(dn);
                    if (ts_node_is_null(id))
                        continue;
                    char *name = node_text(p->arena, id, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    size_t line = (size_t)ts_node_start_point(id).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                } else if (strcmp(kind, "spec") == 0) {
                    /* a direct translation_unit tag definition */
                    if (!c_specifier_defines_named(cand))
                        continue;
                    TSNode tag = ts_node_child_by_field_name(cand, "name", 4);
                    char *name = node_text(p->arena, tag, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    size_t line = (size_t)ts_node_start_point(tag).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                } else { /* pdef: object-like or function-like macro */
                    TSNode name_node = ts_node_child_by_field_name(cand, "name", 4);
                    if (ts_node_is_null(name_node))
                        continue;
                    char *name = node_text(p->arena, name_node, in->source);
                    if (!name || name[0] == '\0')
                        continue;
                    size_t line = (size_t)ts_node_start_point(name_node).row + 1;
                    if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                        export_fail = 1;
                }
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_KOTLIN) {
            /* Kotlin (SPRINT-22): the named top-level declarations.
             * Kotlin has NO internal-linkage filter beyond `private` --
             * a private top-level declaration is file-visible only and
             * never surface (the C `static` rule's Kotlin spelling);
             * public (the default) and internal (module-visible)
             * surface. The top-level check is the DIRECT source_file
             * parent (Kotlin has no wrapper nodes -- no climb needed;
             * class members and nested declarations fail it -- the
             * type-nesting precedent). Every exported name is ALSO
             * recorded as a top-level-symbol parse hint (the C#
             * namespace-hint channel): the resolver v1-kotlin keys the
             * declared-fact import matching on it
             * (docs/LANGUAGES.md Kotlin section 5/6). Per capture kind:
             *   - fdef: the direct simple_identifier (a top-level
             *     function always has a body; extensions included --
             *     the receiver lives inside the receiver type);
             *   - cdecl: the direct type_identifier (class, interface
             *     AND enum class are ALL class_declaration nodes in the
             *     pinned grammar -- probed);
             *   - odecl: the direct type_identifier (named objects;
             *     the anonymous companion object carries no name and
             *     never matches);
             *   - pdecl: the variable_declaration's simple_identifier
             *     (the val/var member shape);
             *   - talias: the direct type_identifier.
             * Enum entries and class members are not module surface
             * (the type-nesting precedent). docs/LANGUAGES.md Kotlin
             * section 6. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode fdef = match_capture(e->q_exports, &m, "fdef");
                TSNode cdecl_ = match_capture(e->q_exports, &m, "cdecl");
                TSNode odecl = match_capture(e->q_exports, &m, "odecl");
                TSNode pdecl = match_capture(e->q_exports, &m, "pdecl");
                TSNode talias = match_capture(e->q_exports, &m, "talias");
                TSNode cand = null_tsnode();
                if (!ts_node_is_null(fdef))
                    cand = fdef;
                else if (!ts_node_is_null(cdecl_))
                    cand = cdecl_;
                else if (!ts_node_is_null(odecl))
                    cand = odecl;
                else if (!ts_node_is_null(pdecl))
                    cand = pdecl;
                else if (!ts_node_is_null(talias))
                    cand = talias;
                else
                    continue;
                TSNode par = ts_node_parent(cand);
                if (ts_node_is_null(par) || strcmp(ts_node_type(par), "source_file") != 0)
                    continue; /* class members / nested declarations */
                if (kt_is_private(cand, in->source))
                    continue; /* file-private: not module surface */
                /* the declaration's name: the first direct
                 * simple_identifier / type_identifier child; the
                 * property shape keeps it one level down (inside the
                 * variable_declaration) */
                TSNode id = null_tsnode();
                uint32_t nc = ts_node_child_count(cand);
                for (uint32_t i = 0; i < nc && ts_node_is_null(id); i++) {
                    TSNode c = ts_node_child(cand, i);
                    if (!ts_node_is_named(c))
                        continue;
                    const char *t = ts_node_type(c);
                    if (strcmp(t, "simple_identifier") == 0 || strcmp(t, "type_identifier") == 0) {
                        id = c;
                    } else if (strcmp(t, "variable_declaration") == 0) {
                        uint32_t vc = ts_node_child_count(c);
                        for (uint32_t j = 0; j < vc; j++) {
                            TSNode v = ts_node_child(c, j);
                            if (ts_node_is_named(v) &&
                                (strcmp(ts_node_type(v), "simple_identifier") == 0 ||
                                 strcmp(ts_node_type(v), "type_identifier") == 0)) {
                                id = v;
                                break;
                            }
                        }
                    }
                }
                if (ts_node_is_null(id))
                    continue;
                char *name = node_text(p->arena, id, in->source);
                if (!name || name[0] == '\0')
                    continue;
                size_t line = (size_t)ts_node_start_point(id).row + 1;
                if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                    export_fail = 1;
                if (out->hints &&
                    !oc_surface_hint_push_symbol(out->hints, p->arena, in->path, name))
                    export_fail = 1;
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_SCALA) {
            /* Scala (SPRINT-24): the named top-level declarations. Scala
             * has NO internal-linkage filter beyond `private` -- a
             * private top-level declaration is package-scoped and never
             * surface v1 (the C `static` rule's Kotlin spelling; the
             * package-scope nuance is the recorded boundary); public
             * (the default) and protected (package/superclass-visible,
             * the Kotlin treatment of `internal`) surface. The top-level
             * check is the DIRECT compilation_unit parent (Scala has no
             * wrapper node; class members and nested declarations fail
             * it -- the type-nesting precedent). Every exported name is
             * ALSO recorded as a top-level-symbol parse hint (the C#
             * namespace-hint channel): the resolver v1-scala keys the
             * declared-fact import matching on it
             * (docs/LANGUAGES.md Scala section 5/6). Per capture kind:
             *   - fdef: the name-field identifier (a top-level `def`
             *     always has a body);
             *   - cdecl: the name-field identifier (class, and the
             *     case/sealed/abstract spellings are anonymous modifier
             *     tokens on the SAME class_definition -- probed);
             *   - odecl: the name-field identifier (named objects; the
             *     anonymous companion/case object carries no name field
             *     and never matches);
             *   - tdef: the name-field identifier (trait);
             *   - edef: the name-field identifier (Scala 3 enum);
             *   - talias: the name-field identifier (`type X = ...`);
             *   - pdecl: the pattern-field identifier (the simple
             *     `val x` / `var x` shape; a tuple pattern like
             *     `val (a, b) = ...` records NOTHING -- the honest
             *     simple-name shape, never guessed).
             * given/extension definitions are NOT surface (unnamed by
             * construction or container nodes whose member defs are the
             * surface -- the Swift extension precedent). Enum cases and
             * class members are not module surface (the type-nesting
             * precedent). docs/LANGUAGES.md Scala section 6. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode fdef = match_capture(e->q_exports, &m, "fdef");
                TSNode cdecl_ = match_capture(e->q_exports, &m, "cdecl");
                TSNode odecl = match_capture(e->q_exports, &m, "odecl");
                TSNode tdef = match_capture(e->q_exports, &m, "tdef");
                TSNode edef = match_capture(e->q_exports, &m, "edef");
                TSNode talias = match_capture(e->q_exports, &m, "talias");
                TSNode pdecl = match_capture(e->q_exports, &m, "pdecl");
                TSNode cand = null_tsnode();
                if (!ts_node_is_null(fdef))
                    cand = fdef;
                else if (!ts_node_is_null(cdecl_))
                    cand = cdecl_;
                else if (!ts_node_is_null(odecl))
                    cand = odecl;
                else if (!ts_node_is_null(tdef))
                    cand = tdef;
                else if (!ts_node_is_null(edef))
                    cand = edef;
                else if (!ts_node_is_null(talias))
                    cand = talias;
                else if (!ts_node_is_null(pdecl))
                    cand = pdecl;
                else
                    continue;
                TSNode par = ts_node_parent(cand);
                if (ts_node_is_null(par) || strcmp(ts_node_type(par), "compilation_unit") != 0)
                    continue; /* class members / nested declarations */
                if (scala_is_private(cand, in->source))
                    continue; /* package-private: not module surface v1 */
                /* the declaration's name: the `name` field identifier;
                 * the property shape keeps it in the `pattern` field */
                TSNode id = null_tsnode();
                if (!ts_node_is_null(pdecl)) {
                    TSNode pat = ts_node_child_by_field_name(pdecl, "pattern", 7);
                    if (!ts_node_is_null(pat) && strcmp(ts_node_type(pat), "identifier") == 0)
                        id = pat;
                } else {
                    id = ts_node_child_by_field_name(cand, "name", 4);
                }
                if (ts_node_is_null(id))
                    continue;
                char *name = node_text(p->arena, id, in->source);
                if (!name || name[0] == '\0')
                    continue;
                size_t line = (size_t)ts_node_start_point(id).row + 1;
                if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                    export_fail = 1;
                if (out->hints &&
                    !oc_surface_hint_push_symbol(out->hints, p->arena, in->path, name))
                    export_fail = 1;
            }
            ts_query_cursor_delete(xcur);
        } else if (e->family == OC_LANG_SWIFT) {
            /* Swift (SPRINT-23): the named top-level declarations.
             * Swift has NO internal-linkage filter below
             * `private`/`fileprivate` -- a private or fileprivate
             * top-level declaration is file-visible only and never
             * surface (the C `static` rule's Swift spelling);
             * public/open and internal (the DEFAULT) surface. The
             * top-level check is the DIRECT source_file parent (Swift
             * code files keep every declaration flat -- probed; type
             * members and nested declarations fail it -- the
             * type-nesting precedent). Every exported name is ALSO
             * recorded as a top-level-symbol parse hint (the C#
             * namespace-hint channel): the resolver v1-swift keys the
             * declared-fact import matching on it
             * (docs/LANGUAGES.md Swift section 5/6). Per capture kind:
             *   - fdef: the `name`-field simple_identifier (a top-level
             *     function always has a body; extensions included --
             *     the receiver is the extension declaration, not part
             *     of the function);
             *   - cdecl: the `name`-field child (class, struct, enum,
             *     extension AND actor are ALL class_declaration nodes in
             *     the pinned grammar -- the anonymous declaration_kind
             *     token carries the keyword; the extension's name field
             *     is a user_type and its TEXT is the extended type name
             *     as written);
             *   - pdecl: the `name`-field type_identifier (protocols);
             *   - prop: the pattern's bound_identifier (the let/var
             *     shape, one level down -- the Kotlin
             *     variable_declaration shape);
             *   - talias: the `name`-field type_identifier.
             * Enum entries and type members are not module surface (the
             * type-nesting precedent). docs/LANGUAGES.md Swift
             * section 6. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode fdef = match_capture(e->q_exports, &m, "fdef");
                TSNode cdecl_ = match_capture(e->q_exports, &m, "cdecl");
                TSNode pdecl = match_capture(e->q_exports, &m, "pdecl");
                TSNode prop = match_capture(e->q_exports, &m, "prop");
                TSNode talias = match_capture(e->q_exports, &m, "talias");
                TSNode cand = null_tsnode();
                if (!ts_node_is_null(fdef))
                    cand = fdef;
                else if (!ts_node_is_null(cdecl_))
                    cand = cdecl_;
                else if (!ts_node_is_null(pdecl))
                    cand = pdecl;
                else if (!ts_node_is_null(prop))
                    cand = prop;
                else if (!ts_node_is_null(talias))
                    cand = talias;
                else
                    continue;
                TSNode par = ts_node_parent(cand);
                if (ts_node_is_null(par) || strcmp(ts_node_type(par), "source_file") != 0)
                    continue; /* type members / nested declarations */
                if (swift_is_private(cand, in->source))
                    continue; /* file-private: not module surface */
                /* the declaration's name: the `name`-field child (the
                 * FIRST `name`-field match is the declaration's name --
                 * the return-type field reuses the label in the
                 * generated parser, a quirk the probe caught and the
                 * first-match rule disarms); the property shape keeps
                 * it in the pattern (both wrapper shapes probed) */
                TSNode id = null_tsnode();
                if (strcmp(ts_node_type(cand), "property_declaration") == 0) {
                    /* both pattern shapes (probed): the identifier sits
                     * directly in the pattern or one wrapper down */
                    TSNode pat = ts_node_child_by_field_name(cand, "name", 4);
                    id = swift_pattern_ident(pat);
                } else {
                    id = ts_node_child_by_field_name(cand, "name", 4);
                }
                if (ts_node_is_null(id))
                    continue;
                char *name = node_text(p->arena, id, in->source);
                if (!name || name[0] == '\0')
                    continue;
                size_t line = (size_t)ts_node_start_point(id).row + 1;
                if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line, 0))
                    export_fail = 1;
                if (out->hints &&
                    !oc_surface_hint_push_symbol(out->hints, p->arena, in->path, name))
                    export_fail = 1;
            }
            ts_query_cursor_delete(xcur);
        } else {
            /* Python classes + __all__. */
            TSQueryCursor *xcur = ts_query_cursor_new();
            ts_query_cursor_exec(xcur, e->q_exports, root);
            while (ts_query_cursor_next_match(xcur, &m) && !export_fail) {
                TSNode cdef = match_capture(e->q_exports, &m, "cdef");
                TSNode cname = match_capture(e->q_exports, &m, "cname");
                TSNode asg = match_capture(e->q_exports, &m, "asg");
                TSNode alname = match_capture(e->q_exports, &m, "alname");
                if (!ts_node_is_null(cdef)) {
                    char *name = node_text(p->arena, cname, in->source);
                    if (name && name[0] != '_' && py_is_top_level(cdef)) {
                        size_t line = (size_t)ts_node_start_point(cdef).row + 1;
                        if (!oc_surface_export_push(out->exports, p->arena, in->path, name, line,
                                                    0))
                            export_fail = 1;
                    }
                } else if (!ts_node_is_null(asg)) {
                    char *left = node_text(p->arena, alname, in->source);
                    if (!left || strcmp(left, "__all__") != 0 || !py_is_top_level(asg))
                        continue;
                    TSNode val = ts_node_child_by_field_name(asg, "value", 5);
                    if (ts_node_is_null(val) || strcmp(ts_node_type(val), "list") != 0)
                        continue;
                    size_t line = (size_t)ts_node_start_point(asg).row + 1;
                    uint32_t q = ts_node_child_count(val);
                    for (uint32_t q2 = 0; q2 < q; q2++) {
                        TSNode el = ts_node_child(val, q2);
                        if (!ts_node_is_named(el) || strcmp(ts_node_type(el), "string") != 0)
                            continue;
                        char *n2 = string_payload(p->arena, el, in->source);
                        if (!n2 || n2[0] == '\0')
                            continue;
                        if (!oc_surface_export_push(out->exports, p->arena, in->path, n2, line, 1))
                            export_fail = 1;
                    }
                }
            }
            ts_query_cursor_delete(xcur);
        }
        if (export_fail) {
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording exports");
            return OC_ERR_ALLOC;
        }
    }

    /* Comments -> per-file doc (consumed by cheap passes such as OC002). */
    TSQueryCursor *ccur = ts_query_cursor_new();
    ts_query_cursor_exec(ccur, e->q_comments, root);
    oc_doc *doc = oc_graph_doc(g, file_id);
    while (ts_query_cursor_next_match(ccur, &m)) {
        TSNode cnode = match_capture(e->q_comments, &m, "comment");
        if (ts_node_is_null(cnode) || !doc)
            continue;
        uint32_t start = ts_node_start_byte(cnode);
        uint32_t end = ts_node_end_byte(cnode);
        size_t line = (size_t)ts_node_start_point(cnode).row + 1;
        size_t col = (size_t)ts_node_start_point(cnode).column + 1;
        if (!oc_doc_add_comment(doc, p->arena, line, col, in->source + start,
                                (size_t)(end - start))) {
            ts_query_cursor_delete(ccur);
            ts_tree_delete(tree);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory storing comments");
            return OC_ERR_ALLOC;
        }
    }
    ts_query_cursor_delete(ccur);

    ts_tree_delete(tree);
    oc_status_clear(st);
    return OC_OK;
}

/* Mutation engine seam (WBS 4.5): the TSLanguage* behind a grammar id,
 * so operator-set queries can be compiled against the same pinned
 * grammars the analysis pipeline uses. NULL for unknown ids. */
const TSLanguage *oc_ts_language_for(const char *grammar) {
    for (size_t i = 0; i < sizeof(LANG_DESCRIPTORS) / sizeof(LANG_DESCRIPTORS[0]); i++) {
        if (strcmp(LANG_DESCRIPTORS[i].grammar, grammar) == 0)
            return LANG_DESCRIPTORS[i].lang_fn();
    }
    return NULL;
}

/* SPRINT-21: the C/C++ header-routing sniff (the approved architecture
 * decision; contract in include/oc/parser.h and docs/LANGUAGES.md C++
 * section 9). Two real parses, parse facts only, never guessed:
 *   1. the cpp parse: has_error -> the content is NOT C++ (go to 2);
 *      clean -> walk the tree for ONE C++-distinctive node kind
 *      (namespace_definition, class_specifier, template_declaration,
 *      alias_declaration, using_declaration, concept_definition,
 *      catch_clause, for_range_loop -- shapes a C header cannot carry);
 *      found -> OC_SNIFF_CPP_POSITIVE, none -> inconclusive (a
 *      C-compatible header: the project-root default decides);
 *   2. the C parse: clean -> OC_SNIFF_C_POSITIVE (positively C: it fails
 *      as C++), errors -> inconclusive (broken under both; the root
 *      default claims it and the parse errors surface in the report).
 * The C-positive class is the K&R old-style definition (probed at
 * adoption: the cpp grammar ERRORS on it, the C grammar parses it
 * clean) -- keyword identifiers do NOT work: tree-sitter's error
 * recovery parses `int class = 3;` clean under cpp. */
static int sniff_has_distinctive(TSNode root) {
    static const char *const kinds[] = {
        "namespace_definition", "class_specifier",   "template_declaration",
        "alias_declaration",    "using_declaration", "concept_definition",
        "catch_clause",         "for_range_loop",    NULL};
    /* bounded iterative DFS over an arena stack (the count_complexity
     * err_frame pattern); arena exhaustion falls back to 0 -- the root
     * default then decides, never a fabricated positive */
    oc_arena a;
    oc_arena_init(&a);
    typedef struct frame {
        TSNode node;
        struct frame *next;
    } frame;
    frame *stack = NULL;
    int found = 0;
    frame *f0 = (frame *)oc_arena_alloc(&a, sizeof(*f0));
    if (!f0) {
        oc_arena_destroy(&a);
        return 0;
    }
    f0->node = root;
    f0->next = NULL;
    stack = f0;
    while (stack && !found) {
        frame *fr = stack;
        stack = fr->next;
        TSNode n = fr->node;
        const char *t = ts_node_type(n);
        for (size_t i = 0; kinds[i]; i++) {
            if (strcmp(t, kinds[i]) == 0) {
                found = 1;
                break;
            }
        }
        uint32_t nc = ts_node_child_count(n);
        for (uint32_t i = 0; i < nc && !found; i++) {
            TSNode c = ts_node_child(n, i);
            if (!ts_node_is_named(c))
                continue;
            frame *nf = (frame *)oc_arena_alloc(&a, sizeof(*nf));
            if (!nf)
                break;
            nf->node = c;
            nf->next = stack;
            stack = nf;
        }
    }
    oc_arena_destroy(&a);
    return found;
}

oc_sniff_verdict oc_parser_sniff_header(const char *src, size_t len) {
    TSParser *p = ts_parser_new();
    if (!p || !ts_parser_set_language(p, tree_sitter_cpp())) {
        ts_parser_delete(p);
        return OC_SNIFF_INCONCLUSIVE;
    }
    TSTree *t = ts_parser_parse_string(p, NULL, src, (uint32_t)len);
    if (!t) {
        ts_parser_delete(p);
        return OC_SNIFF_INCONCLUSIVE;
    }
    int cpp_err = ts_node_has_error(ts_tree_root_node(t));
    int cpp_positive = 0;
    if (!cpp_err)
        cpp_positive = sniff_has_distinctive(ts_tree_root_node(t));
    ts_tree_delete(t);
    ts_parser_delete(p);
    if (cpp_positive)
        return OC_SNIFF_CPP_POSITIVE;
    if (cpp_err) {
        /* the cpp grammar rejected it: is it positively C? */
        TSParser *p2 = ts_parser_new();
        if (p2 && ts_parser_set_language(p2, tree_sitter_c())) {
            TSTree *t2 = ts_parser_parse_string(p2, NULL, src, (uint32_t)len);
            if (t2) {
                int c_clean = !ts_node_has_error(ts_tree_root_node(t2));
                ts_tree_delete(t2);
                ts_parser_delete(p2);
                return c_clean ? OC_SNIFF_C_POSITIVE : OC_SNIFF_INCONCLUSIVE;
            }
        }
        ts_parser_delete(p2);
    }
    return OC_SNIFF_INCONCLUSIVE;
}
