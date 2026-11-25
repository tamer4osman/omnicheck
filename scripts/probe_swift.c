/* Probe tree-sitter-swift 0.7.1 shape facts (adoption-time verification,
 * the C probe pattern: every contract shape guess is probed against the
 * PINNED grammar with an actual parse before any adoption code exists).
 * The grammar carries an external scanner (src/scanner.c): block comments,
 * raw string parts, implicit/explicit semicolons, the operator tokens
 * (&&, ||, ?? disambiguation), directive if/endif. LANGUAGE_VERSION 14,
 * accepted by the vendored runtime (ABI window 13..15). */
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
extern const TSLanguage *tree_sitter_swift(void);

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
    ts_parser_set_language(p, tree_sitter_swift());
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
    run("declarations: top-level func/class/struct/enum/extension/protocol/typealias/let/var",
        "func topFn(x: Int) -> Int { return x }\n"
        "class Top {\n}\n"
        "struct Point { var x = 0 }\n"
        "enum Color { case red, green }\n"
        "extension Int { func doubled() -> Int { return self * 2 } }\n"
        "protocol Repo {\n    func fetch() -> Int\n}\n"
        "typealias Handler = (Int) -> Void\n"
        "let greeting = \"hi\"\n"
        "var counter = 0\n");

    run("functions: params (plain, external name, default, inout, variadic), throws/async, generic, static/class, init/deinit",
        "func plain(a: Int, b: String) {}\n"
        "func extern(external local: Int, rest: Int = 2) {}\n"
        "func inoutp(v: inout Int) { v += 1 }\n"
        "func variadic(items: Int..., tail: String) {}\n"
        "func throwing() throws -> Int { return 1 }\n"
        "func loading() async throws -> Int { return 1 }\n"
        "func generic<T: Comparable>(_ a: T, _ b: T) -> T { return a }\n"
        "class Widget {\n"
        "    static func make() -> Widget { return Widget() }\n"
        "    class func kind() -> String { return \"w\" }\n"
        "    init(id: Int) { }\n"
        "    convenience init() { self.init(id: 0) }\n"
        "    deinit { }\n"
        "    func scale(v: Int) -> Int { return v }\n"
        "}\n"
        "protocol Spec {\n    func run() -> Int\n    func withBody() -> Int { return 1 }\n}\n");

    run("decisions: if/else-if, guard, switch(+default), for(+where), while, repeat-while, do/catch(+where), ternary, ??, &&, ||",
        "func branches(a: Int, items: [String]) -> Int {\n"
        "    if a > 0 { return 1 } else if a < 0 { return -1 }\n"
        "    guard a != 0 else { return 0 }\n"
        "    switch a {\n"
        "    case 1, 2: return 20\n"
        "    case let x where x > 100: return 30\n"
        "    default: return 10\n"
        "    }\n"
        "    for it in items where !it.isEmpty { if it.count > 2 { continue } }\n"
        "    var n = a\n"
        "    while n > 0 { n -= 1 }\n"
        "    repeat { n += 1 } while n < 3\n"
        "    do {\n        try throwing()\n    } catch let err as KeyError where err.code == 1 {\n        n = -1\n"
        "    } catch {\n        n = 0\n    }\n"
        "    let t = a > 0 ? 1 : -1\n"
        "    let e: Int? = nil\n"
        "    let r = e ?? -1\n"
        "    let ok = a > 0 && !items.isEmpty || n > 3\n"
        "    return n + t\n"
        "}\n"
        "func throwing() throws -> Int { return 1 }\n"
        "class KeyError: Error { var code = 0 }\n");

    run("accessors: computed get/set, willSet/didSet, property owner shape",
        "class Tracked {\n"
        "    var plain = 0\n"
        "    var computed: Int {\n"
        "        get { return 1 }\n"
        "        set(v) { plain = v }\n"
        "    }\n"
        "    var observed: Int = 0 {\n"
        "        willSet { plain = newValue }\n"
        "        didSet { }\n"
        "    }\n"
        "    var readOnly: Int { return 2 }\n"
        "}\n");

    run("imports: plain, kind-scoped member, submodule path, @testable, @_exported, kind-only module",
        "import Foundation\n"
        "import class Foundation.NSString\n"
        "import func Mod.Helpers.make\n"
        "import Mod.Submodule\n"
        "@testable import StoreKit\n"
        "@_exported import Core\n"
        "import typealias Core.Logger\n");

    run("main fact: top-level statements are the program entry parse fact",
        "let configured = 1\n"
        "print(\"run\")\n"
        "run_loop()\n");

    run("Package.swift manifest shape: target declarations with name/path labels",
        "// swift-tools-version:5.9\n"
        "import PackageDescription\n"
        "let package = Package(\n"
        "    name: \"App\",\n"
        "    targets: [\n"
        "        .target(name: \"Core\"),\n"
        "        .executableTarget(name: \"Cli\", path: \"Sources/Cli\"),\n"
        "        .testTarget(name: \"CoreTests\", dependencies: [\"Core\"]),\n"
        "    ]\n"
        ")\n");

    run("scanner shapes: interpolation, raw strings, multiline strings, nested comments, semicolon-free lines",
        "let tmpl = \"a\\(x + 1)b\\(q)\"\n"
        "let raw = #\"line \\#(v) raw\"#\n"
        "let multi = \"\"\"\nline1\nline2 \\(q)\n\"\"\"\n"
        "let nested = /* outer /* inner */ still outer */ 1\n"
        "// line comment\n"
        "/* block */\n"
        "let a = 1\nlet b = 2\n");

    run("surface shapes: visibility modifiers, nested types, private/fileprivate, public/open/internal, static members",
        "public class Api {\n"
        "    public private(set) var ready = false\n"
        "    private func secret() {}\n"
        "    fileprivate func inner() {}\n"
        "    open func overridable() {}\n"
        "    internal func moduleVisible() {}\n"
        "    class Nested {\n        func deep() {}\n    }\n"
        "    struct Item { let id = 0 }\n"
        "    enum Mode { case on, off }\n"
        "}\n"
        "fileprivate class Hidden {}\n"
        "struct Free { static let kind = \"free\" }\n");

    run("error recovery boundary: a broken token stream still records what parsed",
        "func broken(: Int {\n"
        "func fine(a: Int) -> Int { return a }\n");

    printf("TOTAL has_error count: %u\n", g_errs);
    return 0;
}
