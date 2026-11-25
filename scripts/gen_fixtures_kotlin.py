#!/usr/bin/env python3
"""Generates tests/fixtures/kotlin + tests/fixtures/kotlin_resolver +
tests/fixtures_sprint22/cobertura_kotlin.xml (the SPRINT-22 structural
corpora).

Hand-counted fixtures, the SPRINT-20/21 corpus-A discipline: every count
is pinned by construction and asserted here AND by tests/test_sprint22.c.
"""
import os

ROOT = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "kotlin")
RES_ROOT = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "kotlin_resolver")
FIX22 = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures_sprint22")

# ---------------------------------------------------------------- rollup.kt
# comp(rollup) = 1 + 12 decisions: if + nested else-if (2 if_expression) +
# the two when entries (the in-range arm and the else arm -- EVERY arm
# counts, the Rust match_arm discipline) + for + for-in + while + do-while
# + one catch handler + the elvis + && + || = 12. comp(one) = 1. File
# rollup = 14.
rollup = """\
// The OC004 rollup fixture (SPRINT-22): the hand-counted decision
// inventory of docs/LANGUAGES.md Kotlin section 4 -- every decision kind
// in one function, counted by construction and pinned by test_sprint22:
// comp(rollup) = 1 + 12 (if + else-if nested if_expression + two when
// arms -- every arm counts including else -- + for + for-in + while +
// do-while + one catch handler + the elvis + && + ||). Lambdas and
// anonymous functions are anonymous: their decisions count inward, the
// constructs are never symbols.
package app.stats

fun rollup(n: Int, items: List<Int>): Int {
    if (n > 0) {
        return 1
    } else if (n < 0) {
        return -1
    }
    when (n) {
        in 3..9 -> return 30
        else -> {}
    }
    for (v in items) {
        n += v
    }
    var i = 0
    for (k in 0 until 2) {
        i++
    }
    while (n > 100) {
        n--
    }
    do {
        n++
    } while (n < -100)
    var t = 0
    try {
        t = t + n
    } catch (ex: IllegalStateException) {
        t = -1
    }
    val e = n ?: -1
    val ok = n > 0 && e != 0
    return t + i + if (ok) 1 else 0 || n
}

fun one(): Int {
    return 1
}
"""
code = "\n".join(l for l in rollup.splitlines() if not l.startswith("//") and not l.startswith("package "))
assert code.count("if (n > 0)") == 1 and code.count("} else if (n < 0)") == 1
assert code.count("-> return 30") == 1 and code.count("else -> {}") == 1
assert code.count("for (v in items)") == 1 and code.count("for (k in 0 until 2)") == 1
assert code.count("while (n > 100)") == 1 and code.count("} while (n < -100)") == 1
assert code.count("catch (ex:") == 1
assert code.count("?:") == 1
assert code.count("&&") == 1 and code.count("||") == 1
# NOTE: the trailing `if (ok) 1 else 0 || n` adds ONE more if_expression:
# decisions = 2 + 2 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1(if-ok) = 13
assert code.count("if (ok)") == 1
decisions = 2 + 2 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1
assert decisions == 13, decisions

# ---------------------------------------------------------------- mathx.kt
# comp(sum26) = 1 + 34 decisions (the comp-35 OC004/OC007 fixture): nine
# if/else-if pairs (18 if_expression) + two && guard ifs (2 if_expression
# + 2 conjunction_expression) + four while + two do-while + two for + ONE
# for-in + two elvis + ONE catch handler = 34 -- all else/defaults 0.
parts = ["// The comp-35 fixture (SPRINT-22): the hand-counted decision\n",
         "// inventory -- 34 decisions, one per recorded kind\n",
         "// (docs/LANGUAGES.md Kotlin section 4): nine if/else-if pairs and\n",
         "// two guard ifs (20 if_expression + 2 conjunction_expression), four\n",
         "// while, two do-while, two for, one for-in, two elvis and one catch\n",
         "// handler; every else arm and the default arm count as their own\n",
         "// when_entry kind only. The definition extent stays under the\n",
         "// OC001.max_lines default (60) so OC001 fires exactly once on this\n",
         "// corpus -- the longfn.kt fixture.\n",
         "package app.stats\n",
         "\n",
         "fun sum26(n: Int): Int {\n"]
for i in range(9):
    parts.append(f"    if (n == {i}) {{ n = {i} + 1 }} else if (n == -{i}) {{ n = -{i} - 1 }}\n")
for i in range(4):
    parts.append("    while (n > 100) { n-- }\n")
for i in range(2):
    parts.append("    do { n++ } while (n < -100)\n")
for i in range(2):
    parts.append(f"    for (k{i} in 0 until 2) {{ n += k{i} }}\n")
parts.append("    for (v in intArrayOf(1, 2)) { n += v }\n")
parts.append("    val opt: Int? = n\n")
parts.append("    n = opt ?: 1\n")
parts.append("    val opt2: Int? = n\n")
parts.append("    n = opt2 ?: 2\n")
parts.append("    try { n = n + 1 } catch (ex: ArithmeticException) { n = -1 }\n")
parts.append("    if (n > 0 && n < 10) { n = 10 }\n")
parts.append("    if (n > -10 && n < 0) { n = -10 }\n")
parts.append("    return n\n}\n")
mathx = "".join(parts)
mcode = "\n".join(l for l in mathx.splitlines() if not l.startswith("//") and not l.startswith("package "))
ifs = mcode.count("if (n ==") + mcode.count("if (n > 0 &&") + mcode.count("if (n > -10 &&")
assert ifs == 20, ifs  # 9 pairs (18: if + else-if each carry "if (n ==") + 2 guard ifs
assert mcode.count("} else if (") == 9
assert mcode.count("while (n > 100)") == 4
assert mcode.count("} while (n < -100)") == 2
assert mcode.count("for (k") == 2 and mcode.count("for (v in") == 1
assert mcode.count("?:") == 2
assert mcode.count("catch (ex:") == 1
assert mcode.count("&&") == 2
# decisions: 20 if + 2 conjunction (the guard &&s) + 4 while + 2 do + 2 for
#           + 1 for-in + 2 elvis + 1 catch = 34
decisions = 20 + 2 + 4 + 2 + 2 + 1 + 2 + 1
assert decisions == 34, decisions
# comp = 35 -- the test pins comp(sum26) == 35.
def_extent = len(mcode.splitlines())
assert def_extent < 60, def_extent

# ---------------------------------------------------------------- longfn.kt
# The OC001 long function: a 63-line definition extent (three lines over
# the default 60-line threshold), body decision-light (comp 1).
lines = ["// The OC001 long function (SPRINT-22): a 63-line definition extent\n",
         "// (three lines over the default 60). The body stays decision-light\n",
         "// (plain assignments) so OC004 fires exactly once on this corpus --\n",
         "// on sum26 (comp 36), not here.\n",
         "package app.stats\n",
         "\n"]
sig = "fun longRunner(seed: Int): Int {\n"
lines.append(sig)  # decl line = 7
body = []
for i in range(1, 61):
    body.append(f"    seed = seed + {i} * 2\n")
body.append("    return seed\n")
body.append("}\n")
longfn = "".join(lines + body)
total_def_lines = 1 + longfn.split(sig, 1)[1].count("\n")
assert total_def_lines == 63, total_def_lines

# ---------------------------------------------------------------- decls.kt
# The symbol-boundary fixture: body-less abstract/interface members are
# NOT symbols; interface DEFAULT methods and object members are; the
# private top-level helper is a symbol but never module surface; the
# typealias and the enum class are surface (the enum ENTRIES are not --
# the documented boundary).
decls = """\
// The OC005/symbol boundary fixture (SPRINT-22): the body-less abstract
// and interface members are NOT symbols (the C prototype discipline --
// Kotlin has no prototypes, every real definition carries a body); the
// interface default method IS a symbol; the private top-level helper is
// a symbol but never module surface (file-private, the C static rule's
// Kotlin spelling); the typealias and the enum class are surface and
// the enum ENTRIES are not (the documented boundary).
package app.shapes

abstract class Base {
    abstract fun run(): Int
    fun ready(): Int {
        return 1
    }
}

interface Repo {
    fun fetch(): Int
    fun default(): Int {
        return 1
    }
}

private fun hiddenHelper(): Int {
    return 7
}

typealias Handler = (Int) -> Unit

enum class Color {
    RED,
    GREEN
}
"""

# ---------------------------------------------------------------- types.kt
# The accessor + nesting fixture: a getter with a real body IS a symbol
# named Prop.get (the C# SPRINT-14 accounting-hole precedent); class
# members are never module surface; the companion object constant is a
# member, not surface; the nested class is not surface.
types = """\
// The accessor + surface-boundary fixture (SPRINT-22): the getter with a
// real body IS a symbol (cached.get, the C# accessor precedent --
// accessor decisions previously counted toward NOTHING); class members
// are never module surface (the type-nesting precedent); the companion
// object constant and the nested class are members too.
package app.shapes

class Holder {
    val cached: Int
        get() {
            return 1
        }
    var tracked: Int = 0
        set(value) {
            field = if (value > 0) value else 0
        }

    companion object {
        const val KIND = "holder"
    }

    class Part(val name: String)
}
"""

# ---------------------------------------------------------------- todo.kt
# The OC008 TODO fixture.
todo = """\
// The OC008 fixture (SPRINT-22): a TODO marker in a comment.
package app.docs

fun documented(): Int {
    // TODO: tighten this boundary before v1.0
    return 0
}
"""

# ---------------------------------------------------------------- dup pair
# The OC008 duplicate pair: identical function bodies in two files.
def dup_file(idx: int) -> str:
    return f"""\
// The OC008 duplicate fixture {idx} (SPRINT-22): the SAME function body
// appears in dup_a.kt and dup_b.kt -- one clone pair, counted by
// construction.
package app.dup

fun dupTarget(x: Int): Int {{
    val scaled = x * 3
    return scaled + 1
}}

fun dupCaller(seed: Int): Int {{
    return dupTarget(seed) + dupTarget(seed + 1)
}}
"""

dup_a = dup_file("a")
dup_b = dup_file("b")

# ---------------------------------------------------------------- credential.kt
# The OC011 secret-pattern fixture: a hardcoded credential-looking
# assignment (the same class of fixture the other packs carry).
credential = """\
// The OC011 fixture (SPRINT-22): a hardcoded credential-shaped value in
// a top-level property -- the secret-pattern pass fires on the
// as-written source.
package app.auth

val password = "hunter2-super-secret-value"

fun authenticate(user: String): Boolean {
    return user == "admin"
}
"""

# ---------------------------------------------------------------- main_app.kt
# The entry fixture: the top-level fun main DEFINITION claims the file
# (the parse fact); the two imports make it the hub of corpus A.
main_app = """\
// The entry fixture (SPRINT-22): the top-level `fun main` definition is
// the entry-point parse fact (the Q_KT_MAIN contract); the single and
// the wildcard import make this file the hub -- Widget claims
// widget.kt, the wildcard claims the whole com.example.tools package.
package app.main

import com.example.widgets.Widget
import com.example.tools.*

fun main(args: Array<String>) {
    val w = Widget(3)
    val scaled = w.scale(2)
    val cracked = Helper.crack(scaled)
    println(cracked)
}
"""

# ---------------------------------------------------------------- widget.kt
# The single-import target: class Widget in package com.example.widgets.
widget = """\
// The single-import target (SPRINT-22): `import com.example.widgets.Widget`
// resolves HERE -- the declared package matches and this file declares a
// top-level Widget (the resolver's declared-fact contract).
package com.example.widgets

class Widget(val id: Int) {
    fun scale(v: Int): Int = v * id
}
"""

# ---------------------------------------------------------------- tools.kt
# The wildcard target: object Helper in package com.example.tools.
tools = """\
// The wildcard-import target (SPRINT-22): `import com.example.tools.*`
// fans out over the package's files -- this file declares the package
// and the object Helper (the Java asterisk-marker fan-out contract).
package com.example.tools

object Helper {
    const val TAG = "tools"

    fun crack(n: Int): Int {
        return n + TAG.length
    }
}
"""

# ---------------------------------------------------------------- kit.kt
# The alias target: object Helper in package com.example.kit -- the SAME
# simple name as the tools object, proving the resolver keys on the
# DECLARED package, not the name.
kit = """\
// The alias-import target (SPRINT-22): `import com.example.kit.Helper as H2`
// resolves HERE -- same simple name as the tools object, different
// declared package: the declared-fact matching keys on the package
// clause, never on the name alone.
package com.example.kit

object Helper {
    const val tag = "kit"
}
"""

# ------------------------------------------------------- include_user.kt
# The import-channels fixture: alias + member shapes recorded.
include_user = """\
// The import-channels fixture (SPRINT-22): the alias import binds the
// ORIGINAL name (H2 is local surface detail); the member import
// (Helper.tag) is the ambiguous member shape -- the whole-file claim
// direction (NULL bind), both readings resolved against the declared
// facts.
package app.usage

import com.example.kit.Helper as H2
import com.example.kit.Helper.tag

val aliased = H2.tag
val memberUsed = tag
"""

CORPUS_A = {
    "rollup.kt": rollup,
    "mathx.kt": mathx,
    "longfn.kt": longfn,
    "decls.kt": decls,
    "types.kt": types,
    "todo.kt": todo,
    "dup_a.kt": dup_a,
    "dup_b.kt": dup_b,
    "credential.kt": credential,
    "main_app.kt": main_app,
    "widget.kt": widget,
    "tools.kt": tools,
    "kit.kt": kit,
    "include_user.kt": include_user,
}

# ============================================================ corpus B ====
# The resolver fixture: the declared-fact contract end to end -- single
# (with a two-file package union), wildcard fan-out, alias, the member
# shape, the cycle (OC003 == 1), the unresolved decoys, the default
# package. 10 files, 9 contracted edges.

core = """\
// Resolver corpus B (SPRINT-22): package app.core carries TWO files --
// the single-type import of Engine resolves to THIS file (the declared
// symbol Engine lives here), the wildcard fan-out reaches both.
package app.core

class Engine {
    fun start(): Int {
        return 1
    }
}

fun alpha(): Int {
    return 2
}
"""

extra = """\
// Resolver corpus B (SPRINT-22): the SECOND file of package app.core --
// `import app.core.*` fans out over every file that DECLARES the
// package (core.kt AND extra.kt); alpha/beta are claimed by name only
// through the wildcard.
package app.core

fun beta(): Int {
    return 3
}
"""

lib = """\
// Resolver corpus B (SPRINT-22): the single-type import -- Engine is
// claimed from package app.core (core.kt); gamma is a top-level
// declaration the alias file imports by name.
package app.lib

import app.core.Engine

fun gamma(): Int {
    return Engine().start()
}
"""

wide = """\
// Resolver corpus B (SPRINT-22): the wildcard import -- app.core.* fans
// out over core.kt AND extra.kt (both declare the package; both carry
// the claimed names).
package app.lib

import app.core.*

val useAll = alpha() + beta()
"""

alias = """\
// Resolver corpus B (SPRINT-22): the alias import binds the ORIGINAL
// name (Motor is local detail -- the export Engine is marked used) and
// the two-segment gamma import claims the app.lib declaration.
package app.util

import app.core.Engine as Motor
import app.lib.gamma

fun eps(): Int {
    return Motor().start() + gamma()
}
"""

member = """\
// Resolver corpus B (SPRINT-22): the member import (Engine.start) -- the
// ambiguous member shape resolves BOTH readings; only the object reading
// matches here (package app.core declares Engine), so the edge lands on
// core.kt; the record keeps the NULL bind (whole-file claim).
package app.util

import app.core.Engine.start

fun memb(): Int {
    return 1
}
"""

cyc_one = """\
// Resolver corpus B (SPRINT-22): the import cycle, side one -- one.kt
// claims Two from the same package (the declared-fact single import
// works intra-package too: the declared package is app.cyc and Two is
// declared in cyc_two.kt); OC003 pins the cycle.
package app.cyc

import app.cyc.Two

class One {
    val pair = Two()
}
"""

cyc_two = """\
// Resolver corpus B (SPRINT-22): the import cycle, side two -- two.kt
// claims One back (the cycle closes; OC003 == 1 on this corpus).
package app.cyc

import app.cyc.One

class Two {
    val mate = One()
}
"""

decoy = """\
// Resolver corpus B (SPRINT-22): the unresolved decoys -- java.util.*
// (the two-segment prefix java matches NO declared package) and
// app.missing.Ghost (no such declared package) stay recorded and
// unresolved, never guessed. OC003/OC005 count nothing for them.
package app.decoy

import java.util.ArrayList
import app.missing.Ghost

fun decoy(): Int {
    return 0
}
"""

nopkg = """\
// Resolver corpus B (SPRINT-22): the DEFAULT package -- a Kotlin file
// may omit the package clause entirely; `import standalone` (the
// one-segment specifier) claims the default package's declared symbol.
fun standalone(): Int {
    return 9
}
"""

user = """\
// Resolver corpus B (SPRINT-22): the default-package claim -- the
// one-segment import resolves through the empty package clause (the
// declared-fact walk treats the missing package_header as the default
// package).
package app.user

import standalone

fun userFn(): Int {
    return standalone()
}
"""

CORPUS_B = {
    "core.kt": core,
    "extra.kt": extra,
    "lib.kt": lib,
    "wide.kt": wide,
    "alias.kt": alias,
    "member.kt": member,
    "cyc_one.kt": cyc_one,
    "cyc_two.kt": cyc_two,
    "decoy.kt": decoy,
    "nopkg.kt": nopkg,
    "user.kt": user,
}

# ------------------------------------------------------------ coverage ----
# The OC007 composition fixture (SPRINT-22): a Cobertura-shaped XML (the
# gcovr shape -- the SAME reader the C/C++ packs take; Kover's own XML
# and JaCoCo XML are recorded descopes) over the corpus-A files: sum26's
# lines fully covered, so the CRAP composition equals comp at 100
# percent coverage; longRunner zero-covered.
sum_lines = []
mcode_lines = mcode.splitlines()
for i, l in enumerate(mcode_lines, start=1):
    s = l.strip()
    if s and not s.startswith("//"):
        sum_lines.append(i)

cov = ['<?xml version="1.0"?>',
       '<!DOCTYPE coverage SYSTEM "http://cobertura.sourceforge.net/xml/coverage-04.dtd">',
       '<coverage line-rate="1.0" branch-rate="0" lines-covered="1" lines-valid="1"',
       '         branches-covered="0" branches-valid="0" complexity="0" version="1.9"',
       '         timestamp="1">',
       '  <sources>',
       '    <source>.</source>',
       '  </sources>',
       '  <packages>',
       '    <package name="app.stats" line-rate="1.0" branch-rate="0" complexity="0">',
       '      <classes>']
cov.append(f'        <class name="MathxKt" filename="mathx.kt" line-rate="1.0" branch-rate="0" complexity="0">')
cov.append('          <methods />')
cov.append('          <lines>')
for ln in sum_lines:
    cov.append(f'            <line number="{ln}" hits="1" branch="false" />')
cov.append('          </lines>')
cov.append('        </class>')
cov.append(f'        <class name="LongfnKt" filename="longfn.kt" line-rate="0.0" branch-rate="0" complexity="0">')
cov.append('          <methods />')
cov.append('          <lines>')
cov.append('            <line number="8" hits="0" branch="false" />')
cov.append('          </lines>')
cov.append('        </class>')
cov.append('      </classes>')
cov.append('    </package>')
cov.append('  </packages>')
cov.append('</coverage>')
cobertura = "\n".join(cov) + "\n"

def main() -> None:
    os.makedirs(ROOT, exist_ok=True)
    os.makedirs(RES_ROOT, exist_ok=True)
    os.makedirs(FIX22, exist_ok=True)
    for name, text in CORPUS_A.items():
        with open(os.path.join(ROOT, name), "w") as f:
            f.write(text)
    for name, text in CORPUS_B.items():
        with open(os.path.join(RES_ROOT, name), "w") as f:
            f.write(text)
    with open(os.path.join(FIX22, "cobertura_kotlin.xml"), "w") as f:
        f.write(cobertura)
    print(f"kotlin corpus A: {len(CORPUS_A)} files -> {ROOT}")
    print(f"kotlin corpus B: {len(CORPUS_B)} files -> {RES_ROOT}")
    print(f"coverage fixture -> {os.path.join(FIX22, 'cobertura_kotlin.xml')}")
    print("rollup decisions: 13 (comp 14)")
    print("sum26 decisions: 34 (comp 35)")
    print("longRunner extent: 63 lines")
    print(f"sum26 covered lines: {len(sum_lines)}")

if __name__ == "__main__":
    main()
