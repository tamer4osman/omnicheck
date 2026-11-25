#!/usr/bin/env python3
"""Generates tests/fixtures/c (SPRINT-20 structural corpus A).

Hand-counted fixtures, the PHP corpus-A discipline: every count is
pinned by construction and asserted by tests/test_sprint20.c.
"""
import os

ROOT = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "c")

# ---------------------------------------------------------------- rollup.c
# comp(rollup) = 1 + 11 decisions:
#   if_statement x2 (if + nested else-if)
#   while_statement x1, do_statement x1, for_statement x1
#   case_statement-with-value x3 (default arm counts 0)
#   conditional_expression x1
#   ops: && x1, || x1
# comp(one) = 1. File rollup = 12.
rollup = """\
// The OC001 rollup fixture (SPRINT-20): the hand-counted decision
// inventory of docs/LANGUAGES.md C section 4 -- every decision kind in
// one function, counted by construction and pinned by test_sprint20:
// comp(rollup) = 1 + 11 (if + else-if nested if_statement + while + do +
// for + three case arms with a value + the ternary + && + ||; the
// default arm and the bare else_clause count 0). The whole definition
// is the span; prototypes are NOT symbols.
int rollup(int n) {
    if (n > 0) {
        return 1;
    } else if (n < 0) {
        return -1;
    } else {
        return 0;
    }
    while (n > 0) {
        n--;
    }
    do {
        n++;
    } while (n < 5);
    for (int i = 0; i < n && i < 10; i++) {
        n--;
    }
    switch (n) {
    case 1:
        break;
    case 2:
    case 3:
        break;
    default:
        break;
    }
    int t = n > 0 ? 1 : 0;
    return t || n;
}

int one(void) {
    return 1;
}
"""
# verify the decision count by construction (code lines only, the header
# comment mentions the operators too)
code = "\n".join(l for l in rollup.splitlines() if not l.startswith("//"))
assert code.count("if (") == 2  # if + else if (nested)
assert code.count("\n    case ") == 3
assert code.count("\n    default:") == 1
assert code.count("&&") == 1 and code.count("||") == 1
assert code.count("? 1 : 0") == 1
assert code.count("while (") == 2  # while_statement + do-while's while

# ---------------------------------------------------------------- mathx.c
# comp(sum26) = 1 + 34 decisions (the comp-35 OC004/OC007 fixture, the
# mathx.php comp-35 analogue): 9 if/else-if pairs (18 if_statements) + 2
# && guard ifs = 20 if_statements, 4 while, 2 do, 3 for, 3 ternaries,
# ops && x2 -- all else/defaults 0.
parts = ["// The comp-35 fixture (SPRINT-20): the hand-counted decision\n",
         "// inventory -- 34 decisions, one per recorded kind (docs/LANGUAGES.md\n",
         "// C section 4): nine if/else-if pairs and two guard ifs (20\n",
         "// if_statements), four while, two do, three for, three ternaries, two\n",
         "// &&; every else_clause and the default arm count 0. The definition\n",
         "// extent stays under the OC001.max_lines default (60) so OC001 fires\n",
         "// exactly once on this corpus -- the longfn.c fixture.\n",
         "int sum26(int n) {\n"]
for i in range(9):
    parts.append(f"    if (n == {i}) {{ n = {i} + 1; }} else if (n == -{i}) {{ n = -{i} - 1; }}\n")
for i in range(4):
    parts.append("    while (n > 100) { n--; }\n")
for i in range(2):
    parts.append("    do { n++; } while (n < -100);\n")
for i in range(3):
    parts.append(f"    for (int k{i} = 0; k{i} < 2; k{i}++) {{ n += k{i}; }}\n")
parts.append("    n = n > 0 ? 1 : n;\n")
parts.append("    n = n < 0 ? -1 : n;\n")
parts.append("    n = n == 0 ? 2 : n;\n")
parts.append("    if (n > 0 && n < 10) { n = 10; }\n")
parts.append("    if (n > -10 && n < 0) { n = -10; }\n")
parts.append("    return n;\n}\n")
mathx = "".join(parts)
# hand-count verification of the generated inventory (code lines only)
mcode = "\n".join(l for l in mathx.splitlines() if not l.startswith("//"))
ifs = mcode.count("if (n ==") + mcode.count("if (n > 0 &&") + mcode.count("if (n > -10 &&")
assert ifs == 20, ifs  # 9 pairs (18 ifs) + 2 && guard ifs
assert mcode.count("} else if (") == 9
assert mcode.count("while (n > 100)") == 4
assert mcode.count("} while (n < -100)") == 2
assert mcode.count("for (int k") == 3
assert mcode.count(" ? ") == 3
assert mcode.count("&&") == 2
decisions = (18 + 2) + 4 + 2 + 3 + 3 + 2
assert decisions == 34, decisions
# the definition extent must stay under the OC001 default (60)
def_extent = len(mcode.splitlines())
assert def_extent < 60, def_extent

# ---------------------------------------------------------------- longfn.c
# The OC001 long function: a 63-line definition extent (three lines over
# the default 60-line threshold), body decision-light (comp 1).
lines = ["// The OC001 long function (SPRINT-20): a 63-line definition extent\n",
         "// (three lines over the default 60). The body stays decision-light\n",
         "// (plain assignments) so OC004 fires exactly once on this corpus --\n",
         "// on sum26 (comp 35), not here.\n"]
sig = "int long_runner(int seed) {\n"
lines.append(sig)  # line 5 = decl line
body = []
for i in range(1, 61):
    body.append(f"    seed = seed + {i} * 2;\n")
body.append("    return seed;\n")
body.append("}\n")
longfn = "".join(lines + body)
extent = longfn.split(sig, 1)[1].count("\n")  # body lines
total_def_lines = 1 + extent  # decl line + body through closing brace
assert total_def_lines == 63, total_def_lines

# ---------------------------------------------------------------- decls.c
# Prototypes are NOT symbols; multi-declarator lines name each declarator;
# static helpers are symbols but NOT module surface.
decls = """\
// The OC005/OC001 boundary fixture (SPRINT-20): three prototypes are NOT
// symbols (the definition lives elsewhere; counting both would double
// every function and give OC007 two anchors), the multi-declarator line
// names EACH declarator separately, and the static helper is a symbol
// but never module surface (internal linkage).
int add3(int a, int b, int c);
extern unsigned flags;
int a, b = 2, c[4];

int add3(int a, int b, int c) {
    return a + b + c;
}

static int hidden_helper(void) {
    return 7;
}
"""

# ---------------------------------------------------------------- types.c
# Tag definitions (struct/union/enum with name AND body), typedefs and
# macros are module surface; the anonymous struct var names only its
# declarator; a bare `struct point p;` use records nothing.
types = """\
// The OC005 surface fixture (SPRINT-20): tag DEFINITIONS (name AND body),
// typedefs and macros are module surface; the anonymous struct variable
// names only its declarator; `struct point pt;` below merely USES the
// tag and records nothing; enum constants (enumerator) are NOT surface
// v1 -- the documented boundary, pinned by test_sprint20.
struct point {
    int x;
    int y;
};

struct {
    int a;
} anon_var;

typedef struct point point_t;
typedef int (*handler)(void *, size_t);

enum color {
    RED,
    GREEN = 2,
    BLUE
};

#define MAX_TAGS 16
#define TAG(x) ((x) * 2)

union raw {
    unsigned u;
    void *p;
};

struct point pt;
"""

# ---------------------------------------------------------------- dup_a/dup_b
dup_body = """\
int page_sum(const int *rows, int n, int *audit, int *seen) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (rows[i] > 0 && seen[i] == 0) {
            total += rows[i];
            audit[i] = 1;
            seen[i] = 1;
        }
    }
    return total;
}
"""
dup_head = ("// The OC008 duplicate-code pair (SPRINT-20): the same normalized block\n"
            "// in two files (one group, two locations -- the php corpus convention).\n")
dup_a = dup_head + dup_body
dup_b = dup_head.replace("pair (SPRINT-20)", "pair, second copy (SPRINT-20)") + dup_body

# ---------------------------------------------------------------- todo.c
todo = """\
// The OC002 fixture (SPRINT-20): exactly one marker comment, as written
// below.

// TODO: swap the scratch buffer for the arena once it lands in tree.
int scratch_ready(void) {
    return 0;
}
"""

# ------------------------------------------------------- include_user.c
include_user = """\
// The import-channel fixture (SPRINT-20): the quoted include resolves
// (include local_util.h, an edge to the analyzed header), the angle
// include stays system-unresolved (recorded, no edge), the macro path
// records NOTHING (not a static payload), the second quoted include is
// unresolved (missing.h is not analyzed).
#include "local_util.h"

#include <stdio.h>
#include HEADER_PATH
#include "missing.h"

int user_main(void) {
    return local_util(4);
}
"""

local_util_h = """\
// The resolved include target (SPRINT-20): a conventional include guard
// wraps every declaration -- the top-level climb must see through it
// (the exports query matches candidates anywhere; the parent chain
// climbs preproc wrappers to translation_unit).
#ifndef LOCAL_UTIL_H
#define LOCAL_UTIL_H

int local_util(int n);

static inline int local_util_sq(int n) {
    return n * n;
}

#endif
"""

# -------------------------------------------------------- main_app.c
main_app = """\
// The entry-point parse-fact fixture (SPRINT-20): a function DEFINITION
// named main is the entry claim (the Q_C_MAIN fact, the Go package-main
// class) -- OC006 stays silent on this file.
#include "local_util.h"

int main(void) {
    return local_util(1);
}
"""

# -------------------------------------------------------- app_test.c
app_test = """\
// The runner-shaped file (SPRINT-20): the C pack registers NO test NAME
// conventions in v1 (a *_test.c name claims nothing), so the expected
// zero in-degree here IS OC006 material -- the honest contract of
// docs/LANGUAGES.md C section 6.
int check_app(void) {
    return 1;
}
"""

# -------------------------------------------------------- cfg/secret.h
# The canonical AWS DOCS example access key id, assembled one character
# at a time so no credential-shaped literal ever appears in tooling text
# (the bytes are what the scanner pins on; the shape is the AWS
# documentation's own example, not a real credential).
_akia = ("A" "K" "I" "A" "I" "O" "S" "F" "O" "D" "N" "N" "7"
         "E" "X" "A" "M" "P" "L" "E")
assert len(_akia) == 20
secret_h = ("// The OC011 corpus file (SPRINT-20): exactly one credential-shaped\n"
            "// literal (the AWS docs example access key id). A header on purpose:\n"
            "// the secret scan is language-blind raw text.\n"
            '#define DEMO_AWS_ACCESS_KEY_ID "' + _akia + '"\n')

files = {
    "rollup.c": rollup,
    "mathx.c": mathx,
    "longfn.c": longfn,
    "decls.c": decls,
    "types.c": types,
    "dup_a.c": dup_a,
    "dup_b.c": dup_b,
    "todo.c": todo,
    "include_user.c": include_user,
    "local_util.h": local_util_h,
    "main_app.c": main_app,
    "app_test.c": app_test,
    "cfg/secret.h": secret_h,
}

for rel, content in files.items():
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)
    print(f"wrote {rel}: {len(content.splitlines())} lines")
