#!/usr/bin/env python3
"""Generates the committed adversarial corpus for WBS 5.1 (fuzz targets +
corpus hardening). Deterministic: every byte is a fixed function of the
spec below -- no randomness, no network. Run once when the corpus needs
extension; the corpus itself is committed so the test suite never
regenerates (the tests run on-disk fixtures, byte-stable across cells).

Corpus design (each file attacks one parser/pipeline assumption):

  js/  attacks the JavaScript grammar + JS-family passes
  py/  attacks the Python grammar + Python passes
  json/ attacks the strict JSON parser and the coverage/OSV loaders

Bounded by design: every file stays under the OC008 oversize cap
(1,000,000 bytes) and below nesting sizes that would make tree-sitter
error recovery quadratic-slow; the fuzz SUITE (test_fuzz.c) additionally
generates seeded mutations on top of the regular fixtures at test time.
"""
import os

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tests", "fixtures_fuzz")


def put(rel, data: bytes):
    path = os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    print(f"{rel}: {len(data)} bytes")


# ----------------------------------------------------------------- js ---
put("js/nul_fuzz.js", b"function ok() { return 1; }\n\x00\x00function broken() { return \x002; }\n")
put("js/bom.js", b"\xef\xbb\xbf" + "export function bom() { return '\u00e9\u4e2d'; }\n".encode("utf-8"))
put("js/only_comments.js", b"// just a comment\n/* and a block\n   comment */\n")
put("js/no_newline.js", b"function last() { return 42; }")
put("js/empty.js", b"")
put("js/unicode_ident.js",
    ("const \u00e9\u00e8\u4e2d\u6587_\u03bb = 3;\n"
     "function \u03bbcalc(x) { return x * \u00e9\u00e8\u4e2d\u6587_\u03bb; }\n"
     "const emoji_in_str = \"\U0001f600 \U0001f914\";\n"
     "module.exports = { \u03bbcalc };\n").encode("utf-8"))
put("js/deep_nest.js",
    b"function deep() {\n  return " + b"(" * 900 + b"1" + b")" * 900 + b";\n}\n")
put("js/brace_bomb.js",
    b"function bomb() {\n" + b"  { // unclosed\n" * 2000 + b"}\n")
put("js/huge_line.js",
    b"function huge() {\n  const s = \"" + b"a" * 600000 + b"\";\n  return s.length;\n}\n")
put("js/many_functions.js",
    b"".join(b"function f%d(x) { if (x > 0) { return x; } return -x; }\n" % i
             for i in range(2000)))
put("js/mutated_report.js",
    # a file that LOOKS like a report -- findings inside analyzed sources
    b'const report = {"findings":[{"rule":"OC001"}]};\n'
    b"export function meta() { return report; }\n")

# ----------------------------------------------------------------- py ---
put("py/invalid_utf8.py", b"def f():\n    return \"\xff\xfe broken\"\n")
put("py/crlf.py", b"def crlf():\r\n    x = 1\r\n    return x\r\n")
put("py/mixed_endings.py", b"def a():\n    return 1\r\ndef b():\r    return 2\n")
put("py/unterminated.py", b"def broken(:\n    s = \"never closes\n    return s\n")
put("py/empty.py", b"")
put("py/long_ident.py",
    ("def " + "x" * 100000 + "():\n    return 1\n").encode("utf-8"))
put("py/many_functions.py",
    b"".join(b"def f%d(x):\n    if x > 0:\n        return x\n    return -x\n" % i
             for i in range(2000)))
put("py/cr_only.py", b"def cr():\r    return 1\r")

# ---------------------------------------------------------------- json --
# Malformed/truncated inputs for the strict JSON parser + loaders.
put("json/coverage_truncated.json",
    b'{"coverage-final": {"a.js": {"statementMap": {"0": {"start": {"line": 1}')
put("json/coverage_huge_numbers.json",
    b'{"a.js": {"statementMap": {"0": {"start": {"line": 1e999, "column": -1}}},'
    b'"s": {"0": 18446744073709551616}}}')
put("json/coverage_wrong_types.json",
    b'{"a.js": {"statementMap": [1, 2, 3], "s": "no", "fnMap": null}}')
put("json/coverage_empty.json", b"")
put("json/osv_truncated.json", b'{"schema": "omnicheck.osv_snapshot/v0", "snapshot_date": "')
put("json/osv_wrong_schema.json",
    b'{"schema": "totally/other/v9", "snapshot_date": "2026-01-01", "records": []}')
put("json/osv_records_not_array.json",
    b'{"schema": "omnicheck.osv_snapshot/v0", "snapshot_date": "2026-01-01", "records": 7}')
put("json/nested_bomb.json", b"[" * 5000 + b"]" * 5000)
put("json/dup_keys.json",
    b'{"schema": "omnicheck.osv_snapshot/v0", "schema": "omnicheck.osv_snapshot/v0"}')
put("json/nul_fuzz.json", b'{"schema": "omnicheck.osv_snap\x00shot/v0"}')

print("corpus complete")
