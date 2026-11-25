#!/usr/bin/env python3
"""Generates tests/fixtures/scala + tests/fixtures/scala_resolver +
tests/fixtures_sprint24/cobertura_scala.xml (the SPRINT-24 structural
corpora).

Hand-counted fixtures, the SPRINT-20/21/22 corpus-A discipline: every
count is pinned by construction and asserted here AND by
tests/test_sprint24.c.
"""
import os

ROOT = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "scala")
RES_ROOT = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "scala_resolver")
FIX24 = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures_sprint24")


def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


# ---------------------------------------------------------------- rollup.scala
# comp(rollup) = 1 + 13 decisions: if + nested else-if (2 if_expression) +
# three match arms (every arm counts including the wildcard -- the Rust
# match_arm discipline) + for + while + do-while + TWO catch arms (the
# catch's case_block arms ARE the catch decision -- no separate catch
# kind) + && + || + the trailing if (13). comp(one) = 1. File rollup =
# 15 (the sum of the comps, the Kotlin convention).
rollup = """\
// The OC004 rollup fixture (SPRINT-24): the hand-counted decision
// inventory of docs/LANGUAGES.md Scala section 4 -- every decision kind
// in one function, counted by construction and pinned by
// test_sprint24: comp(rollup) = 1 + 13 (if + else-if nested
// if_expression + three match arms -- every arm counts including the
// wildcard -- + for + while + do-while + TWO catch arms -- the catch
// decision IS its case_block arms, no separate catch kind -- + && + ||
// + the trailing if, the infix kind gated to the boolean operators in
// C). Lambdas are anonymous: their decisions count inward, the
// construct is never a symbol.
package app.stats

object Roller {
  def rollup(n: Int, items: List[Int]): Int = {
    if (n > 0) {
      return 1
    } else if (n < 0) {
      return -1
    }
    n match {
      case 1 | 2 => return 20
      case k if k > 3 => return 30
      case _ => {}
    }
    for (v <- items) {
      n + v
    }
    var i = n
    while (i > 100) {
      i -= 1
    }
    do {
      i += 1
    } while (i < 3)
    try {
      i = i / n
    } catch {
      case ex: ArithmeticException => i = -1
      case _ => i = 0
    }
    val ok = n > 0 && items.nonEmpty || i == 0
    if (ok) i else 0
  }
  def one: Int = 1
}
"""

# ---------------------------------------------------------------- mathx.scala
# The comp-38 fixture (SPRINT-24): the hand-counted decision inventory
# -- 37 decisions, one per recorded kind (docs/LANGUAGES.md Scala
# section 4): twelve if_expression (five if/else pairs + four single
# ifs + the three boolean-guarded tail ifs), four while, two do-while,
# two for, twelve match arms across two matches (every arm counts --
# the Rust match_arm discipline), two catch arms, two && and one ||
# (the gated infix kind). 12+4+2+2+12+2+3 = 37 -> comp(sum26) = 38.
# The definition extent stays under the OC001.max_lines default (60) so
# OC001 fires exactly once on this corpus -- the longfn.scala fixture.
mathx = """\
// The comp-38 fixture (SPRINT-24): the hand-counted decision inventory
// -- 37 decisions, one per recorded kind (docs/LANGUAGES.md Scala
// section 4): twelve if_expression (five if/else pairs + four single
// ifs + the three boolean-guarded tail ifs), four while, two do-while,
// two for, twelve match arms across two matches (every arm counts --
// the Rust match_arm discipline), two catch arms, two && and one ||
// (the gated infix kind). The extent stays under the OC001.max_lines
// default so OC001 fires exactly once on this corpus -- the
// longfn.scala fixture.
package app.stats

object Mathx {
  def sum26(n: Int): Int = {
    var acc = 0
    if (n == 0) { acc = 1 } else { acc = 2 }
    if (n == 1) { acc += 1 } else { acc += 2 }
    if (n == 2) { acc += 1 } else { acc += 2 }
    if (n == 3) { acc += 1 } else { acc += 2 }
    if (n == 4) { acc += 1 } else { acc += 2 }
    if (n == 5) { acc += 1 }
    if (n == 6) { acc += 1 }
    if (n == 7) { acc += 1 }
    if (n == 8) { acc += 1 }
    var i = 0
    while (i < 4) { acc += 1; i += 1 }
    while (i < 5) { acc += 1; i += 1 }
    while (i < 6) { acc += 1; i += 1 }
    while (i < 7) { acc += 1; i += 1 }
    do { acc += 1; i += 1 } while (i < 9)
    do { acc += 1; i += 1 } while (i < 10)
    for (j <- 0 to 1) { acc += j }
    for (j <- 0 to 2) { acc += j }
    acc match {
      case 0 => acc = 1
      case 1 => acc = 2
      case 2 => acc = 3
      case 3 => acc = 4
      case 4 => acc = 5
      case _ => acc = 6
    }
    n match {
      case 0 => acc = 1
      case 1 => acc = 2
      case 2 => acc = 3
      case 3 => acc = 4
      case 4 => acc = 5
      case _ => acc = 6
    }
    try {
      acc = acc / n
    } catch {
      case ex: ArithmeticException => acc = -1
      case _ => acc = 0
    }
    if (acc > 0 && n > 0) { acc += 1 }
    if (acc < 0 || n < 0) { acc -= 1 }
    if (acc == 0 && n == 0) { acc = 1 }
    acc
  }
}
"""

# ---------------------------------------------------------------- longfn.scala
# The OC001 long function (SPRINT-24): a 63-line definition extent
# (three lines over the default 60). The body stays decision-light
# (plain assignments) so OC004 fires exactly once on this corpus -- on
# sum26 (comp 38), not here.
lines = ["// The OC001 long function (SPRINT-24): a 63-line definition extent",
         "// (three lines over the default 60). The body stays decision-light",
         "// (plain assignments) so OC004 fires exactly once on this corpus --",
         "// on sum26 (comp 38), not here.",
         "package app.stats",
         "",
         "object LongRunner {",
         "  def longRunner(seed: Int): Int = {"]
for i in range(1, 61):
    lines.append("    seed = seed + %d * 2" % i)
lines.append("    seed")
lines.append("  }")
lines.append("}")
longfn = "\n".join(lines) + "\n"

# ---------------------------------------------------------------- credential.scala
credential = """\
// The OC011 fixture (SPRINT-24): a hardcoded credential-shaped value in
// a top-level val -- the secret-pattern pass fires on the as-written
// source.
package app.auth

val password = "hunter2-super-secret-value"

def authenticate(user: String): Boolean = {
  user == "admin"
}
"""

# ---------------------------------------------------------------- decls.scala
# The OC005/symbol boundary fixture (SPRINT-24): the body-less trait
# members are NOT symbols (the C prototype discipline); the trait
# default method IS a symbol; the private top-level helper is a symbol
# but never module surface (package-private, the C static rule's
# Kotlin spelling); the type alias and the enum are surface and the
# enum CASES are not (the documented boundary); the @main-annotated def
# is an entry claim.
decls = """\
// The OC005/symbol boundary fixture (SPRINT-24): the body-less trait
// members are NOT symbols (the C prototype discipline); the trait
// default method IS a symbol; the private top-level helper is a symbol
// but never module surface (package-private, the C static rule's
// Kotlin spelling); the type alias and the Scala 3 enum are surface
// and the enum CASES are not (the documented boundary).
package app.shapes

trait Sourced {
  def name: String
  def source(): String = {
    name
  }
}

enum Color {
  case Red, Green, Blue
}

type Handler = Int => Unit

private def hidden(): Int = 1

def visible(n: Int): Int = n + 1

@main def entry(): Unit = {
  val s = Sourced
  println(visible(1))
}
"""

# ---------------------------------------------------------------- kit.scala
kit = """\
// The alias-import target (SPRINT-24): `import com.example.kit.Helper as H2`
// resolves HERE -- same simple name as the tools object, different
// declared package: the declared-fact matching keys on the package
// clause, never on the name alone.
package com.example.kit

object Helper {
  val tag: String = "kit"
}
"""

# ---------------------------------------------------------------- tools.scala
tools = """\
// The wildcard-import target (SPRINT-24): `import com.example.tools.*`
// fans out over the package's files -- this file declares the package
// and the object Helper (the Java asterisk-marker fan-out contract).
package com.example.tools

object Helper {
  val TAG: String = "tools"

  def crack(n: Int): Int = {
    n + TAG.length
  }
}
"""

# ---------------------------------------------------------------- widget.scala
widget = """\
// The single-import target (SPRINT-24): `import com.example.widgets.Widget`
// resolves HERE -- the declared package matches and this file declares a
// top-level Widget (the resolver's declared-fact contract).
package com.example.widgets

class Widget(val id: Int) {
  def scale(v: Int): Int = v * id
}
"""

# ---------------------------------------------------------------- main_app.scala
main_app = """\
// The entry fixture (SPRINT-24): the object-main definition is the
// entry-point parse fact (the Q_SC_MAIN contract -- a def named main
// with a parameters list inside an object); the single and the
// wildcard import make this file the hub -- Widget claims widget.scala,
// the wildcard claims the whole com.example.tools package.
package app.main

import com.example.widgets.Widget
import com.example.tools.*

object App {
  def main(args: Array[String]): Unit = {
    val w = Widget(3)
    val scaled = w.scale(2)
    val cracked = Helper.crack(scaled)
    println(cracked)
  }
}
"""

# ---------------------------------------------------------------- todo.scala
todo = """\
// The OC008 fixture (SPRINT-24): a TODO marker in a comment.
package app.docs

def documented(): Int = {
  // TODO: tighten this boundary before v1.0
  0
}
"""

# ---------------------------------------------------------------- dup_a.scala / dup_b.scala
dup_body = """\
// The OC002 duplicate fixture (SPRINT-24): the SAME function body
// appears in dup_a.scala and dup_b.scala -- one clone pair, counted by
// construction.
package app.dup

def cloneTarget(n: Int): Int = {
  var acc = 0
  for (i <- 0 until n) {
    acc += i * 2
  }
  if (acc > 100) {
    acc - 1
  } else {
    acc + 1
  }
}
"""

# ---------------------------------------------------------------- types.scala
types = """\
// The surface-boundary + Scala 3 fixture (SPRINT-24): class members are
// never module surface (the type-nesting precedent); the companion
// object constant and the nested class are members too; the extension
// method IS a symbol (the body belongs to its def); the given with a
// body is a container, its compare method is the symbol; top-level
// val/var ARE surface (the simple pattern shape).
package app.shapes

class Holder {
  val cached: Int = {
    1
  }

  def hit(n: Int): Int = n

  object Registry {
    val kind: String = "holder"
  }

  class Part(val name: String)
}

extension (s: String) {
  def shout: String = s.toUpperCase
}

val topVal: Int = 7
var topVar: Int = 8
"""

# ---------------------------------------------------------------- resolver corpus
core = """\
// The resolver fixture (SPRINT-24): the declared package and the
// top-level names the importers claim.
package com.example.lib

class Service {
  def run(): Int = 1
}

def core(n: Int): Int = n + 1
"""

store = """\
// The resolver fixture (SPRINT-24): the member-import reading -- the
// object Store carries the member total (the a.b.C.f shape's first
// declared reading).
package com.example.lib

object Store {
  def total(a: Int, b: Int): Int = a + b
}
"""

typesr = """\
// The resolver fixture (SPRINT-24): the package com.example.lib.types
// declares TypeA and TypeB -- the selector and wildcard fan-out
// targets.
package com.example.lib.types

class TypeA {
  def id: Int = 1
}

class TypeB {
  def id: Int = 2
}
"""

libuser = """\
// The resolver fixture (SPRINT-24): the import shapes -- single
// (com.example.lib.core), wildcard (com.example.lib.types.*), selector
// pair (TypeA, TypeB), the Scala 3 tail rename (Service as Svc) and
// the member claim (com.example.lib.Store.total -- the ambiguous
// member shape, NULL bind, both declared readings).
package app.use

import com.example.lib.core
import com.example.lib.types.*
import com.example.lib.types.{TypeA, TypeB}
import com.example.lib.Service as Svc
import com.example.lib.Store.total

def useAll(): Int = {
  val svc = Svc
  val a = TypeA
  val b = TypeB
  core(a.id) + svc.run() + total(1, 2)
}
"""

decoy = """\
// The resolver fixture (SPRINT-24): the SAME simple names in a
// DIFFERENT declared package -- the declared-fact matching keys on the
// package clause, never on the name alone.
package com.example.other

class TypeA {
  def id: Int = 99
}

object Store {
  def total(a: Int, b: Int): Int = a - b
}
"""

nopkg = """\
// The resolver fixture (SPRINT-24): NO package clause -- the empty
// package is a recorded fact; the default-package claim matches the
// FIRST segment reading (the kt_prefix_sym contract).
def bare(n: Int): Int = n
"""

cyc_one = """\
// Resolver corpus B (SPRINT-24): the import cycle, side one -- claims
// Two from the same package (the declared-fact single import works
// intra-package too: the declared package is com.example.cyc and Two
// is declared in cyc_two.scala); OC003 pins the cycle.
package com.example.cyc

import com.example.cyc.Two

class One {
  val mate = new Two()
}
"""

cyc_two = """\
// Resolver corpus B (SPRINT-24): the import cycle, side two -- claims
// One back (the cycle closes; OC003 == 1 on this corpus).
package com.example.cyc

import com.example.cyc.One

class Two {
  val mate = new One()
}
"""

extra = """\
// Resolver corpus B (SPRINT-24): the SECOND file of package
// com.example.lib -- nothing claims THIS file (no import points at
// unclaimed; the file-level OC006 fires instead and the OC005 export
// pass stays empty BY CONSTRUCTION on this corpus -- the honest
// outcome, the Kotlin corpus B shape).
package com.example.lib

def unclaimed(n: Int): Int = n * 3
"""

member = """\
// The resolver fixture (SPRINT-24): the member import's SECOND
// declared reading -- package com.example.lib.Store would make total a
// top-level name in that package (the v1-kotlin declared-prefix walk
// matches BOTH readings honestly).
package com.example.lib.Store

def total(a: Int, b: Int): Int = a * b
"""

# ---------------------------------------------------------------- coverage fixture
# The OC007 fixture (SPRINT-24): a Cobertura-shaped XML (the sbt-jacoco
# reader takes the JaCoCo/Cobertura shape -- the same honesty as the
# Kotlin pack: the coverage reader is the Java pack's Cobertura reader,
# function-level counters keyed by the file path; the filename is the
# corpus-relative path -- rollup.scala, the Kotlin mathx.kt convention).
coverage = """\
<?xml version="1.0"?>
<!DOCTYPE coverage SYSTEM "http://cobertura.sourceforge.net/xml/coverage-04.dtd">
<coverage line-rate="1.0" branch-rate="0" lines-covered="1" lines-valid="1"
          branches-covered="0" branches-valid="0" complexity="0" version="1.9"
          timestamp="1">
  <sources>
    <source>.</source>
  </sources>
  <packages>
    <package name="app.stats" line-rate="1.0" branch-rate="0" complexity="0">
      <classes>
        <class name="Roller" filename="rollup.scala" line-rate="1.0" branch-rate="0" complexity="0">
          <methods />
          <lines>
            <line number="9" hits="1" branch="false" />
            <line number="10" hits="1" branch="false" />
            <line number="11" hits="1" branch="false" />
            <line number="12" hits="1" branch="false" />
            <line number="13" hits="1" branch="false" />
            <line number="14" hits="1" branch="false" />
            <line number="15" hits="1" branch="false" />
            <line number="16" hits="1" branch="false" />
            <line number="17" hits="1" branch="false" />
            <line number="18" hits="1" branch="false" />
            <line number="19" hits="1" branch="false" />
            <line number="20" hits="1" branch="false" />
            <line number="21" hits="1" branch="false" />
            <line number="22" hits="1" branch="false" />
            <line number="23" hits="1" branch="false" />
            <line number="24" hits="1" branch="false" />
            <line number="25" hits="1" branch="false" />
            <line number="26" hits="1" branch="false" />
            <line number="27" hits="1" branch="false" />
            <line number="28" hits="1" branch="false" />
            <line number="29" hits="1" branch="false" />
            <line number="30" hits="1" branch="false" />
            <line number="31" hits="1" branch="false" />
            <line number="32" hits="1" branch="false" />
            <line number="33" hits="1" branch="false" />
            <line number="34" hits="1" branch="false" />
            <line number="35" hits="1" branch="false" />
            <line number="36" hits="1" branch="false" />
            <line number="37" hits="1" branch="false" />
            <line number="38" hits="1" branch="false" />
            <line number="39" hits="1" branch="false" />
            <line number="40" hits="1" branch="false" />
            <line number="41" hits="1" branch="false" />
            <line number="42" hits="1" branch="false" />
            <line number="43" hits="1" branch="false" />
            <line number="44" hits="1" branch="false" />
          </lines>
        </class>
        <class name="Mathx" filename="mathx.scala" line-rate="0.0" branch-rate="0" complexity="0">
          <methods />
          <lines>
            <line number="13" hits="0" branch="false" />
            <line number="14" hits="0" branch="false" />
            <line number="15" hits="0" branch="false" />
          </lines>
        </class>
      </classes>
    </package>
  </packages>
</coverage>
"""


def main():
    w(os.path.join(ROOT, "rollup.scala"), rollup)
    w(os.path.join(ROOT, "mathx.scala"), mathx)
    w(os.path.join(ROOT, "longfn.scala"), longfn)
    w(os.path.join(ROOT, "credential.scala"), credential)
    w(os.path.join(ROOT, "decls.scala"), decls)
    w(os.path.join(ROOT, "kit.scala"), kit)
    w(os.path.join(ROOT, "tools.scala"), tools)
    w(os.path.join(ROOT, "widget.scala"), widget)
    w(os.path.join(ROOT, "main_app.scala"), main_app)
    w(os.path.join(ROOT, "todo.scala"), todo)
    w(os.path.join(ROOT, "dup_a.scala"), dup_body)
    w(os.path.join(ROOT, "dup_b.scala"),
      dup_body.replace("dup_a.scala", "dup_b.scala"))
    w(os.path.join(ROOT, "types.scala"), types)
    w(os.path.join(RES_ROOT, "core.scala"), core)
    w(os.path.join(RES_ROOT, "store.scala"), store)
    w(os.path.join(RES_ROOT, "typesr.scala"), typesr)
    w(os.path.join(RES_ROOT, "libuser.scala"), libuser)
    w(os.path.join(RES_ROOT, "decoy.scala"), decoy)
    w(os.path.join(RES_ROOT, "nopkg.scala"), nopkg)
    w(os.path.join(RES_ROOT, "cyc_one.scala"), cyc_one)
    w(os.path.join(RES_ROOT, "cyc_two.scala"), cyc_two)
    w(os.path.join(RES_ROOT, "extra.scala"), extra)
    w(os.path.join(RES_ROOT, "member.scala"), member)
    os.makedirs(FIX24, exist_ok=True)
    w(os.path.join(FIX24, "cobertura_scala.xml"), coverage)
    print("scala fixtures written:")
    print("  fixtures/scala: %d files" % len(os.listdir(ROOT)))
    print("  fixtures/scala_resolver: %d files" % len(os.listdir(RES_ROOT)))
    print("  fixtures_sprint24: %d files" % len(os.listdir(FIX24)))


if __name__ == "__main__":
    main()
