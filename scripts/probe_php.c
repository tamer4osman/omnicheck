/* Probe tree-sitter-php 0.24.2 shape facts (adoption-time verification,
 * the C# / Rust / Ruby probe pattern: every contract shape guess is probed
 * against the PINNED grammar with an actual parse before any adoption
 * code exists). OmniCheck compiles the MIXED php+html grammar (php/), so
 * the probe parses both a pure-PHP file and an HTML-mixed file through
 * tree_sitter_php(). */
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
extern const TSLanguage *tree_sitter_php(void);

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
    ts_parser_set_language(p, tree_sitter_php());
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
    run("pure php: decls, params, namespace, use forms",
        "<?php\n"
        "declare(strict_types=1);\n"
        "namespace App\\Store;\n"
        "use ArrayObject;\n"
        "use App\\Store\\Contracts\\Mapper as MapperI;\n"
        "use function App\\f1;\n"
        "use const App\\LIMIT;\n"
        "use App\\Store\\{Product, Order as O, function map, const MAX};\n"
        "// a line comment\n"
        "/* block */\n"
        "/** doc */\n"
        "interface HasId { public function id(): int; }\n"
        "trait Log { protected int $level = 0; }\n"
        "enum Suit: string implements HasId { case Hearts = 'H';\n"
        "  public function id(): int { return 1; } }\n"
        "abstract class Base extends Root implements HasId, Log {\n"
        "  public const K = 1;\n"
        "  private static ?Base $inst = null;\n"
        "  public function __construct(private int $id,\n"
        "      public string $name = 'x', ...$rest) {}\n"
        "  public static function make(int $a, bool &$ok,\n"
        "      string|int $b = PHP_INT_MAX): ?self { return null; }\n"
        "  abstract protected function tick(): void;\n"
        "}\n"
        "function top(string $s, int ...$nums): int|false {\n"
        "  return strlen($s) + array_sum($nums);\n"
        "}\n"
        "$fn = function (int $x) use ($fn, $y): int { return $x; };\n"
        "$af = static fn(int $v) => $v * 2;\n"
        "$obj = new class(1) implements HasId {\n"
        "  public function __construct(private int $i) {}\n"
        "  public function id(): int { return $this->i; }\n"
        "};\n");

    run("decisions",
        "<?php\n"
        "function d($x, $y, $items) {\n"
        "  if ($x > 0) { $a = 1; } elseif ($x == 0) { $a = 2; } else { $a = 3; }\n"
        "  if ($y) $a += 1;\n"
        "  while ($x < 10) { $x += 1; }\n"
        "  do { $x += 1; } while ($x < 20);\n"
        "  for ($i = 0; $i < 3; $i++) { $a += $i; }\n"
        "  foreach ($items as $k => $v) { $a += $v; }\n"
        "  switch ($x) {\n"
        "    case 0:\n"
        "    case 1:\n"
        "      $a += 1;\n"
        "      break;\n"
        "    default:\n"
        "      $a += 2;\n"
        "  }\n"
        "  $m = match(true) {\n"
        "    $x > 100 => 'big',\n"
        "    $x > 10 => 'mid',\n"
        "    default => 'small',\n"
        "  };\n"
        "  try { $a += intdiv($a, $x); }\n"
        "  catch (TypeError | ValueError $e) { $a = 0; }\n"
        "  catch (Throwable) { $a = -1; }\n"
        "  finally { $a += 0; }\n"
        "  $b = $x && $y || $a ? 1 : 2;\n"
        "  $c = $x and $y or $a;\n"
        "  $e2 = $x ?: 'd';\n"
        "  $f = $x ?? $y;\n"
        "  throw new RuntimeException('x');\n"
        "}\n");

    run("include/require constructs + unquoted decls",
        "<?php\n"
        "require 'vendor/autoload.php';\n"
        "require_once __DIR__ . '/x.php';\n"
        "include 'a.php';\n"
        "include_once 'b.php';\n"
        "class WithHtml {};\n");

    run("html-mixed file",
        "<html><body>\n"
        "<p><?= $x ?></p>\n"
        "<?php if ($ok) { echo '<b>yes</b>'; } ?>\n"
        "</body></html>\n");

    printf("TOTAL files with parse errors: %u\n", g_errs);
    return 0;
}
