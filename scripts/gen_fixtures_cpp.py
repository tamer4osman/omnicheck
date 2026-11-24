#!/usr/bin/env python3
"""Generates tests/fixtures/cpp + tests/fixtures/cpp_resolver +
tests/fixtures_sprint21/gcovr_cobertura_cpp.xml (the SPRINT-21
structural corpora).

Hand-counted fixtures, the SPRINT-20 corpus-A discipline: every count is
pinned by construction and asserted here AND by tests/test_sprint21.c.
"""
import os

ROOT = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "cpp")
RES_ROOT = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "cpp_resolver")
FIX21 = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures_sprint21")

# ---------------------------------------------------------------- rollup.cpp
# comp(rollup) = 1 + 13 decisions: if + nested else-if (2 if_statement) +
# while + do + for + for_range_loop (the C++11 range-for is its OWN node
# type) + case-with-value x3 (default counts 0) + the ternary +
# catch_clause + && + || = 13. comp(one) = 1. File rollup = 15.
rollup = """\
// The OC004 rollup fixture (SPRINT-21): the hand-counted decision
// inventory of docs/LANGUAGES.md C++ section 4 -- every decision kind in
// one function, counted by construction and pinned by test_sprint21:
// comp(rollup) = 1 + 13 (if + else-if nested if_statement + while + do +
// for + the C++11 range-for + three case arms with a value + the ternary
// + one catch handler + && + ||; the default arm and the bare else count
// 0). The whole definition is the span; prototypes are NOT symbols.
#include <string>

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
    int arr[4] = {1, 2, 3, 4};
    for (int v : arr) {
        n += v;
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
    try {
        t = t + n;
    } catch (const int &e) {
        t = e;
    }
    (void)sizeof(std::string);
    return t || n;
}

int one(void) {
    return 1;
}
"""
code = "\n".join(l for l in rollup.splitlines() if not l.startswith("//"))
assert code.count("if (") == 2  # if + else if (nested)
assert code.count("\n    case ") == 3
assert code.count("\n    default:") == 1
assert code.count("&&") == 1 and code.count("||") == 1
assert code.count("? 1 : 0") == 1
assert code.count("while (") == 2  # while_statement + do-while's while
assert code.count("for (int i") == 1 and code.count("for (int v : arr)") == 1
assert code.count("catch (") == 1
decisions = 2 + 1 + 1 + 1 + 1 + 3 + 1 + 1 + 1 + 1
assert decisions == 13, decisions

# ---------------------------------------------------------------- mathx.cpp
# comp(sum26) = 1 + 34 decisions (the comp-35 OC004/OC007 fixture): nine
# if/else-if pairs (18 if_statements) + two && guard ifs = 20
# if_statements, four while, two do, two for, ONE for_range_loop, two
# ternaries, ONE catch handler, plus the two && operator tokens of the
# guard ifs -- all else/defaults 0. The definition extent stays under the
# OC001.max_lines default (60) so OC001 fires exactly once (longfn.cpp).
parts = ["// The comp-35 fixture (SPRINT-21): the hand-counted decision\n",
         "// inventory -- 34 decisions, one per recorded kind (docs/LANGUAGES.md\n",
         "// C++ section 4): nine if/else-if pairs and two guard ifs (20\n",
         "// if_statements), four while, two do, two for, one C++11 range-for,\n",
         "// two ternaries, one catch handler, and the two && tokens of the\n",
         "// guard ifs; every else_clause and the default arm count 0. The\n",
         "// definition extent stays under the OC001 default (60) so OC001 fires\n",
         "// exactly once on this corpus -- the longfn.cpp fixture.\n",
         "#include <string>\n",
         "int sum26(int n) {\n"]
for i in range(9):
    parts.append(f"    if (n == {i}) {{ n = {i} + 1; }} else if (n == -{i}) {{ n = -{i} - 1; }}\n")
for i in range(4):
    parts.append("    while (n > 100) { n--; }\n")
for i in range(2):
    parts.append("    do { n++; } while (n < -100);\n")
for i in range(2):
    parts.append(f"    for (int k{i} = 0; k{i} < 2; k{i}++) {{ n += k{i}; }}\n")
parts.append("    int arr[2] = {1, 2};\n")
parts.append("    for (int v : arr) { n += v; }\n")
parts.append("    n = n > 0 ? 1 : n;\n")
parts.append("    n = n == 0 ? 2 : n;\n")
parts.append("    try { n = n + 1; } catch (const long &e) { n = (int)e; }\n")
parts.append("    if (n > 0 && n < 10) { n = 10; }\n")
parts.append("    if (n > -10 && n < 0) { n = -10; }\n")
parts.append("    (void)sizeof(std::string);\n")
parts.append("    return n;\n}\n")
mathx = "".join(parts)
mcode = "\n".join(l for l in mathx.splitlines() if not l.startswith("//"))
ifs = mcode.count("if (n ==") + mcode.count("if (n > 0 &&") + mcode.count("if (n > -10 &&")
assert ifs == 20, ifs  # 9 pairs (18 ifs) + 2 && guard ifs
assert mcode.count("} else if (") == 9
assert mcode.count("while (n > 100)") == 4
assert mcode.count("} while (n < -100)") == 2
assert mcode.count("for (int k") == 2
assert mcode.count("for (int v : arr)") == 1
assert mcode.count(" ? ") == 2
assert mcode.count("catch (") == 1
assert mcode.count("&&") == 2
decisions = (18 + 2) + 4 + 2 + 2 + 1 + 2 + 1 + 2
assert decisions == 34, decisions
def_extent = len("\n".join(l for l in mathx.splitlines() if not l.startswith("//")).splitlines())
assert def_extent < 60, def_extent

# ---------------------------------------------------------------- longfn.cpp
# The OC001 long function: a 63-line definition extent (three lines over
# the default 60-line threshold), body decision-light (comp 1).
lines = ["// The OC001 long function (SPRINT-21): a 63-line definition extent\n",
         "// (three lines over the default 60). The body stays decision-light\n",
         "// (plain assignments) so OC004 fires exactly once on this corpus --\n",
         "// on sum26 (comp 35), not here.\n"]
sig = "int long_runner(int seed) {\n"
lines.append(sig)  # decl line = 5
body = []
for i in range(1, 61):
    body.append(f"    seed = seed + {i} * 2;\n")
body.append("    return seed;\n")
body.append("}\n")
longfn = "".join(lines + body)
extent = longfn.split(sig, 1).count("\n") if False else longfn.split(sig, 1)[1].count("\n")
total_def_lines = 1 + extent
assert total_def_lines == 63, total_def_lines

# ---------------------------------------------------------------- decls.cpp
# Prototypes are NOT symbols; member declarations are NOT symbols; the
# multi-declarator line names each declarator; the static helper is a
# symbol but never module surface.
decls = """\
// The OC005/OC001 boundary fixture (SPRINT-21): the prototype is NOT a
// symbol (the definition lives elsewhere), the member declaration inside
// the class is NOT a symbol (no body), the multi-declarator line names
// EACH declarator separately, and the static helper is a symbol but
// never module surface (internal linkage).
int add3(int a, int b, int c);

struct Gauge {
    int read(void);   // member declaration: not a symbol
    int level;
};

int a, b = 2, c[4];

int add3(int a, int b, int c) {
    return a + b + c;
}

static int hidden_helper(void) {
    return 7;
}
"""

# ---------------------------------------------------------------- types.h
# The SNIFFED header (SPRINT-21): a C++-distinctive .h -- the cpp parse is
# clean and carries namespace_definition / class_specifier shapes, so the
# routing sniffs it to the cpp pack (the report's header_routing block
# pins sniffed_cpp = 1 on this corpus). A conventional guard wraps
# everything; the named namespace body is surface; members are not.
types_h = """\
// The routing-sniff fixture (SPRINT-21): C++-distinctive content in a
// .h file -- the content sniff (clean cpp parse + a C++-distinctive
// node) routes it to the cpp pack. The named namespace body IS surface
// (the class, the enum class, the alias, the macro); class members and
// enumerator constants are not (the documented boundaries).
#ifndef TYPES_H
#define TYPES_H

namespace app {

class Engine {
public:
    int start(int v);   // member declaration: never surface
    int id = 0;
};

enum class Mode { READ, WRITE };

using Word = unsigned long;

}

#define LIMIT 16

#endif
"""

# ---------------------------------------------------------------- local_util.h
# The ROOT-DEFAULT header (SPRINT-21): plain C-compatible content (no
# C++-distinctive node parses clean under BOTH grammars) -- the sniff is
# inconclusive and the project-root default routes it to cpp (the corpus
# carries .cpp files). The report pins default_cpp = 1.
local_util_h = """\
// The root-default fixture (SPRINT-21): C-compatible header content --
// the sniff is inconclusive (clean under both grammars, no C++
// -distinctive node) and the project-root default (cpp sources in the
// discovered set) claims it for the cpp pack. The guard-wrapped
// declarations stay surface through the climb.
#ifndef LOCAL_UTIL_H
#define LOCAL_UTIL_H

int local_util(int n);

static inline int local_util_sq(int n) {
    return n * n;
}

#endif
"""

# ---------------------------------------------------------------- cfg/secret.hpp
_akia = ("A" "K" "I" "A" "I" "O" "S" "F" "O" "D" "N" "N" "7"
         "E" "X" "A" "M" "P" "L" "E")
assert len(_akia) == 20
secret_hpp = ("// The OC011 corpus file (SPRINT-21): exactly one credential-shaped\n"
              "// literal (the AWS docs example access key id). A header on purpose:\n"
              "// the secret scan is language-blind raw text.\n"
              '#define DEMO_AWS_ACCESS_KEY_ID "' + _akia + '"\n')

# ---------------------------------------------------------------- dup pair
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
dup_head = ("// The OC008 duplicate-code pair (SPRINT-21): the same normalized block\n"
            "// in two files (one group, two locations -- the SPRINT-20 convention).\n")
dup_a = dup_head + dup_body
dup_b = dup_head.replace("pair (SPRINT-21)", "pair, second copy (SPRINT-21)") + dup_body

# ---------------------------------------------------------------- todo.cpp
todo = """\
// The OC002 fixture (SPRINT-21): exactly one marker comment, as written
// below.

// TODO: swap the scratch buffer for the arena once it lands in tree.
int scratch_ready(void) {
    return 0;
}
"""

# ------------------------------------------------------- include_user.cpp
include_user = """\
// The import-channel fixture (SPRINT-21): the quoted include resolves
// (include local_util.h -- the root-default routed header, an edge to
// the analyzed file), the angle include stays system-unresolved
// (recorded, no edge), the macro path records NOTHING (not a static
// payload), the second quoted include is unresolved (missing.h is not
// analyzed).
#include "local_util.h"

#include <string>
#include HEADER_PATH
#include "missing.h"

int user_main(void) {
    return local_util(4);
}
"""

# -------------------------------------------------------- main_app.cpp
main_app = """\
// The entry-point parse-fact fixture (SPRINT-21): a free-standing
// function DEFINITION named main is the entry claim (the Q_CPP_MAIN
// fact) -- OC006 stays silent on this file.
#include "local_util.h"

int main() {
    return local_util(1);
}
"""

# -------------------------------------------------------- app_test.cpp
app_test = """\
// The runner-shaped file (SPRINT-21): the cpp pack registers NO test
// NAME conventions in v1 (a *_test.cpp name claims nothing), so the
// expected zero in-degree here IS OC006 material.
int check_app(void) {
    return 1;
}
"""

# -------------------------------------------------------- engine.cpp
engine = """\
// The symbol-shape fixture (SPRINT-21): the inline method is a symbol
// (field_identifier), the out-of-class definition is a symbol through
// the qualified_identifier name field, and the class members never
// become module surface (the class is the surface unit).
struct Widget {
    int size(void) { return 4; }
};

int Widget::size(void) { return 4; }

int engine_user(void) {
    Widget w;
    return w.size();
}
"""

files = {
    "rollup.cpp": rollup,
    "mathx.cpp": mathx,
    "longfn.cpp": longfn,
    "decls.cpp": decls,
    "types.h": types_h,
    "local_util.h": local_util_h,
    "cfg/secret.hpp": secret_hpp,
    "dup_a.cpp": dup_a,
    "dup_b.cpp": dup_b,
    "todo.cpp": todo,
    "include_user.cpp": include_user,
    "main_app.cpp": main_app,
    "app_test.cpp": app_test,
    "engine.cpp": engine,
}
# 14 files: local_util.h (in-degree 2) and main_app.cpp (main fact) stay
# out of OC006 -> OC006 == 12. Wait: 14 - 2 = 12. The C corpus counted 13
# files with OC006 == 11; this corpus carries 14 files, OC006 == 12.
assert len(files) == 14

# ---------------------------------------------------------------- cpp_resolver
def w(root, rel, content):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)

res_files = {
    "src/main.cpp": """\
// The resolver fixture (SPRINT-21): one file, every include channel
// (the v1-cpp semantics are v1-c shared).
//   - "util/log.hpp" resolves (relative to THIS file's directory);
//   - "../third/p.hpp" resolves (the lexical .. pops across src/);
//   - <vector> is the system channel: recorded, NEVER resolves;
//   - HEADER_PATH is a macro path: nothing is recorded;
//   - "missing.hpp" is analyzed-set-absent: recorded, unresolved.
#include "util/log.hpp"
#include "../third/p.hpp"
#include <vector>
#include HEADER_PATH
#include "missing.hpp"

int app_run(void) {
    return log_line("boot") + p_val(2);
}
""",
    "src/util/log.hpp": """\
// The resolved include target: a conventional guard wraps everything.
#ifndef LOG_HPP
#define LOG_HPP

int log_line(const char *msg);

#endif
""",
    "src/util/log.cpp": """\
// The same-directory include: log.cpp -> log.hpp.
#include "log.hpp"

int log_line(const char *msg) {
    (void)msg;
    return 1;
}
""",
    "third/p.hpp": """\
#ifndef P_HPP
#define P_HPP
int p_val(int n);
#endif
""",
    "third/p.cpp": """\
#include "p.hpp"

int p_val(int n) {
    return n * 2;
}
""",
    "include/outer.hpp": """\
// The nested include anchors at the INCLUDING FILE's directory.
#ifndef OUTER_HPP
#define OUTER_HPP
#include "sub/inner.hpp"
#endif
""",
    "include/sub/inner.hpp": """\
#ifndef INNER_HPP
#define INNER_HPP
int inner_api(void);
#endif
""",
    "cyc/a.hpp": """\
#ifndef A_HPP
#define A_HPP
#include "b.hpp"
int a_api(void);
#endif
""",
    "cyc/b.hpp": """\
#ifndef B_HPP
#define B_HPP
#include "a.hpp"
int b_api(void);
#endif
""",
    "cyc/cycle_driver.cpp": """\
// Drives the a.hpp/b.hpp include cycle: one edge into each header.
#include "a.hpp"
#include "b.hpp"

int cycle_driver(void) {
    return a_api() + b_api();
}
""",
}
assert len(res_files) == 10

# ------------------------------------------------------- cobertura fixture
# Repo-relative class filenames over the corpus A files; the line hits
# are pinned against the generated contents (computed, not guessed).
rollup_lines = rollup.splitlines()
one_start = next(i for i, l in enumerate(rollup_lines) if l.startswith("int one(void)")) + 1
one_end = one_start + 2  # int one(void) { / return 1; / }
add3_lines = decls.splitlines()
add3_start = next(i for i, l in enumerate(add3_lines) if l.startswith("int add3(int a, int b, int c) {")) + 1
add3_end = add3_start + 2

cov = f"""\
<?xml version="1.0" encoding="UTF-8"?>
<!-- The SPRINT-21 OC007 fixture: a gcovr-shaped Cobertura XML over the
     cpp corpus A (the same language-blind reader as SPRINT-20). Paths
     are repo-relative to the analysis root, the corpus convention. -->
<coverage branch-rate="0.0" branches-covered="0" branches-valid="4"
          complexity="0" line-rate="0.8" lines-covered="6" lines-valid="8"
          timestamp="1764100000" version="gcovr 7.2">
  <sources>
    <source>.</source>
  </sources>
  <packages>
    <package name="tests.fixtures.cpp" line-rate="0.8">
      <classes>
        <class name="one" filename="tests/fixtures/cpp/rollup.cpp" line-rate="1.0"
               branch-rate="0.0" complexity="0.0">
          <methods/>
          <lines>
            <line number="{one_start}" hits="4" branch="False"/>
            <line number="{one_start + 1}" hits="4" branch="False"/>
            <line number="{one_end}" hits="4" branch="False"/>
          </lines>
        </class>
        <class name="add3" filename="tests/fixtures/cpp/decls.cpp" line-rate="0.5"
               branch-rate="0.0" complexity="0.0">
          <methods/>
          <lines>
            <line number="{add3_start}" hits="2" branch="False"/>
            <line number="{add3_start + 1}" hits="2" branch="False"/>
            <line number="{add3_end}" hits="0" branch="False"/>
          </lines>
        </class>
      </classes>
    </package>
  </packages>
</coverage>
"""

for rel, content in files.items():
    w(ROOT, rel, content)
    print(f"wrote {rel}: {len(content.splitlines())} lines")
for rel, content in res_files.items():
    w(RES_ROOT, rel, content)
    print(f"wrote cpp_resolver/{rel}: {len(content.splitlines())} lines")
os.makedirs(FIX21, exist_ok=True)
with open(os.path.join(FIX21, "gcovr_cobertura_cpp.xml"), "w") as f:
    f.write(cov)
print(f"wrote fixtures_sprint21/gcovr_cobertura_cpp.xml "
      f"(one() lines {one_start}-{one_end}, add3() lines {add3_start}-{add3_end})")
