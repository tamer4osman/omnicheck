#!/usr/bin/env python3
"""Generates tests/fixtures/swift + tests/fixtures/swift_resolver +
tests/fixtures_sprint23/cobertura_swift.xml (the SPRINT-23 structural
corpora).

Hand-counted fixtures, the SPRINT-20/21/22 corpus-A discipline: every count
is pinned by construction and asserted here AND by tests/test_sprint23.c.

The Swift module model reshapes the corpora: Package.swift declares the
targets (the declared-fact resolver's module channel), the target files
live under their DECLARED paths, and the loose (module-less) files at the
corpus root are never import targets.
"""
import os

ROOT = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "swift")
RES_ROOT = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "swift_resolver")
FIX23 = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures_sprint23")

# ============================================================== corpus A ===
# The structural corpus: 17 files, 3 contracted imports edges (the
# kind-scoped named claim, the whole-module wholesale claim x2 shapes),
# 1 entry file (the top-level-statement parse fact), 13 unreferenced
# files (OC006 exactly), OC001/OC002/OC004/OC008/OC011 pinned by
# construction, OC005 empty by construction (every claimed file's
# surface is claimed by its bind).

manifest_a = """\
// swift-tools-version:5.9
// The SPM manifest fixture (SPRINT-23): the declared-fact resolver's
// module channel -- five targets with EXPLICIT paths; the manifest file
// itself carries no module and stays in the OC006 set.
import PackageDescription

let package = Package(
    name: "Fixtures",
    targets: [
        .target(name: "Core", path: "Core"),
        .target(name: "Widgets", path: "Widgets"),
        .target(name: "Tools", path: "Tools"),
        .target(name: "Kit", path: "Kit"),
        .target(name: "App", path: "App")
    ]
)
"""

main_app = """\
// The entry fixture (SPRINT-23): the top-level statements are the
// entry-point parse fact (the Q_SW_TOP contract -- a source_file with
// direct executable statements). The kind-scoped struct import claims
// Widget from the Widgets module (the named bind); the whole-module
// import claims the Tools module wholesale (the NULL bind).
import struct Widgets.Widget
import Tools

let boot = Widget(id: 3)
let scaled = boot.scale(2)
print(Helper.crack(scaled))
"""

kit_user = """\
// The kind-scoped import fixture (SPRINT-23): `import typealias Kit.Tag`
// names the member Tag from module Kit -- the named claim marks the
// matching export. The file has NO top-level statements (a struct is a
// declaration, not code) and no incoming claims: OC006 fires here.
import typealias Kit.Tag

struct Holder {
    var tag = Tag(id: 1)
    func show() -> String {
        return tag.describe()
    }
}
"""

widget = """\
// The single-type import target (SPRINT-23): module Widgets carries
// exactly ONE top-level declaration (struct Widget) -- the named bind
// claims it, so OC005 stays empty by construction. The accessor bodies
// ARE symbols (the C# SPRINT-14 precedent): the implicit getter
// (doubled.get), the explicit pair (tracked.get/tracked.set) and the
// observer (observed.willSet, comp 2 -- one if); the EMPTY didSet body
// matches nothing (the body-less accessor rule, pinned by its absence).
public struct Widget {
    public let id: Int
    private var cached: Int = 0
    public init(id: Int) {
        self.id = id
    }
    public func scale(_ v: Int) -> Int {
        return v * id
    }
    public var doubled: Int {
        return id * 2
    }
    public var tracked: Int {
        get {
            return cached
        }
        set(v) {
            cached = v
        }
    }
    public var observed: Int = 0 {
        willSet {
            if newValue > cached {
                cached = newValue
            }
        }
        didSet {
        }
    }
}
"""

helper = """\
// The whole-module import target (SPRINT-23): module Tools -- the NULL
// bind marks every export wholesale (importing a module loads all of
// it, the Kotlin wildcard discipline).
public enum Helper {
    case simple
    public static func crack(_ v: Int) -> Int {
        return v + 1
    }
}
"""

tag = """\
// The kind-scoped import target (SPRINT-23): module Kit carries exactly
// ONE top-level declaration (struct Tag) -- the named bind claims it.
public struct Tag {
    public let id: Int
    public init(id: Int) {
        self.id = id
    }
    public func describe() -> String {
        return "tag\\(id)"
    }
}
"""

engine = """\
// The Core module (SPRINT-23): intra-module files need NO import, so
// nobody on corpus A claims them -- the OC006 set carries both Core
// files (the module model made visible).
class Engine {
    func start() -> Int {
        return 1
    }
}

func kickstart() -> Int {
    return Engine().start()
}
"""

extra = """\
// The SECOND Core file (SPRINT-23): same module, same story -- OC006.
func backup() -> Int {
    return 7
}
"""

dup_a = """\
// The OC008 duplicate fixture a (SPRINT-23): the SAME function body
// appears in dup_a.swift and dup_b.swift -- one clone pair, counted by
// construction.
func dupTarget(x: Int) -> Int {
    let scaled = x * 3
    return scaled + 1
}

func dupCaller(seed: Int) -> Int {
    return dupTarget(seed) + dupTarget(seed + 1)
}
"""

dup_b = """\
// The OC008 duplicate fixture b (SPRINT-23): the SAME function body
// appears in dup_a.swift and dup_b.swift -- one clone pair, counted by
// construction.
func dupTarget(x: Int) -> Int {
    let scaled = x * 3
    return scaled + 1
}

func dupCaller(seed: Int) -> Int {
    return dupTarget(seed) + dupTarget(seed + 1)
}
"""

longfn = """\
// The OC001 fixture (SPRINT-23): the definition extent is 63 lines --
// one over the OC001.max_lines default (60), so OC001 fires exactly
// once on this corpus (mathx.swift's sum26 stays under the threshold).
// Every statement differs so no 10-line normalized window repeats (the
// OC008 storm the first draft taught).
func longRunner(n: Int) -> Int {
    var t = n
    t += 1
    t += 2
    t += 3
    t += 4
    t += 5
    t += 6
    t += 7
    t += 8
    t += 9
    t += 10
    t += 11
    t += 12
    t += 13
    t += 14
    t += 15
    t += 16
    t += 17
    t += 18
    t += 19
    t += 20
    t += 21
    t += 22
    t += 23
    t += 24
    t += 25
    t += 26
    t += 27
    t += 28
    t += 29
    t += 30
    t += 31
    t += 32
    t += 33
    t += 34
    t += 35
    t += 36
    t += 37
    t += 38
    t += 39
    t += 40
    t += 41
    t += 42
    t += 43
    t += 44
    t += 45
    t += 46
    t += 47
    t += 48
    t += 49
    t += 50
    t += 51
    t += 52
    t += 53
    t += 54
    t += 55
    t += 56
    t += 57
    return t
}
"""

# comp(sum26) = 1 + 34 decisions: 7 if/else-if pairs (14 if_statement,
# else-if nested), 3 guard_statement, 2 nil_coalescing_expression,
# 1 ternary_expression, 3 switch_entry (2 + 1, EVERY arm counts), 2
# for_statement, 4 while_statement, 2 repeat_while_statement, 1
# catch_block, 1 conjunction_expression, 1 disjunction_expression = 34.
mcode = """\
// The comp-35 fixture (SPRINT-23): the hand-counted decision inventory
// -- 34 decisions, one per recorded kind (docs/LANGUAGES.md Swift
// section 4): 7 if/else-if pairs (14 if_statement, else-if nested), 3
// guard, 2 nil-coalescing, 1 ternary, 3 switch arms (every arm counts
// including default), 2 for, 4 while, 2 repeat-while, 1 catch handler,
// 1 conjunction and 1 disjunction. The definition extent stays under
// the OC001.max_lines default (60) so OC001 fires exactly once on this
// corpus -- the longfn.swift fixture.
func sum26(n: Int, items: [Int]) -> Int {
    var t = 0
    if n > 0 { t = 1 } else if n < 0 { t = -1 }
    if n == 1 { t = 2 } else if n == 2 { t = -2 }
    if n == 3 { t = 3 } else if n == 4 { t = -3 }
    if n == 5 { t = 4 } else if n == 6 { t = -4 }
    if n == 7 { t = 5 } else if n == 8 { t = -5 }
    if n == 9 { t = 6 } else if n == 10 { t = -6 }
    if n == 11 { t = 7 } else if n == 12 { t = -7 }
    guard items.count > 0 else { return 0 }
    guard items.count > 1 else { return 1 }
    guard items.count > 2 else { return 2 }
    let e: Int? = nil
    t = t + (e ?? 0)
    t = t + (e ?? 1)
    t = t > 100 ? t : -t
    switch n {
    case 0:
        t += 100
    default:
        t += 1
    }
    switch items.first {
    default:
        break
    }
    for v in items { t += v }
    for i in 0..<2 { t += i }
    var w = n
    while w > 100 { w -= 1 }
    while w < -100 { w += 1 }
    while w > 90 { w -= 1 }
    while w < -90 { w += 1 }
    repeat { w += 1 } while w < 0
    repeat { w -= 1 } while w > 0
    do {
        t = try boomer(t)
    } catch {
        t = 0
    }
    let ok = t > 0 && items.isEmpty
    let alt = ok || t > 1000
    _ = alt
    return t
}

func boomer(_ x: Int) throws -> Int {
    return x
}
"""

# comp(rollup) = 1 + 13: if + nested else-if (2 if_statement) + guard +
# ternary + 2 switch_entry + for + while + repeat-while + 1 catch +
# nil-coalescing + conjunction + disjunction = 13. File rollup = 14
# (under the OC004 threshold, the OC001 span check too).
rollup = """\
// The OC004 rollup fixture (SPRINT-23): the hand-counted decision
// inventory of docs/LANGUAGES.md Swift section 4 -- every decision kind
// in one function, counted by construction and pinned by
// test_sprint23: comp(rollup) = 1 + 13 (if + else-if nested if + guard
// + ternary + two switch arms -- every arm counts including default --
// + for + while + repeat-while + one catch handler + the
// nil-coalescing + && + ||).
func rollup(n: Int, items: [Int]) -> Int {
    if n > 0 {
        return 1
    } else if n < 0 {
        return -1
    }
    guard !items.isEmpty else { return 0 }
    let e: Int? = nil
    var t = e ?? n
    t = t > 10 ? t : -t
    switch t {
    case 1:
        t += 10
    default:
        t += 2
    }
    for v in items {
        t += v
    }
    var i = 0
    while i < 2 {
        i += 1
    }
    repeat {
        t += 1
    } while t < -100
    do {
        t = try boomer(t)
    } catch {
        t = -1
    }
    let ok = t > 0 && items.count > 1
    let alt = ok || t > 500
    _ = alt
    return t
}
"""

todo = """\
// The OC002 fixture (SPRINT-23): two task markers, counted by
// construction -- the comment kinds are `comment` (line) and
// `multiline_comment` (block; block comments NEST in Swift).
// TODO: wire the retry path
func documented() -> Int {
    /* TODO: drop the magic number */
    return 42
}
"""

credential = """\
// The OC011 fixture (SPRINT-23): a hardcoded credential-shaped value in
// a top-level property -- the secret-pattern pass fires on the
// as-written source.
let password = "hunter2-super-secret-value"

func authenticate(user: String) -> Bool {
    return user == "admin"
}
"""

decls = """\
// The structural surface fixture (SPRINT-23): every top-level
// declaration shape surfaces (functions, class/struct/enum/extension
// via the anonymous declaration_kind token, protocol, typealias,
// stored globals); members, enum entries and accessors never surface
// (the type-nesting precedent).
func freeFn(x: Int) -> Int {
    return x
}

class Klass {
    func member() -> Int {
        return 1
    }
}

struct St {
    let id = 0
}

enum Mode {
    case on
    case off
}

extension Int {
    func doubled() -> Int {
        return self * 2
    }
}

protocol Spec {
    func run() -> Int
}

typealias Handler = (Int) -> Int

let globalLet = 1
var globalVar = 2
"""

types = """\
// The type-shape fixture (SPRINT-23): generics, optionals, arrays,
// dictionaries, closures, variadics, inout, throws/async, external
// parameter labels -- the parameter extraction exercises every shape
// (docs/LANGUAGES.md Swift section 3).
func generic<T: Comparable>(_ a: T, _ b: T) -> T {
    return a
}

func labeled(external local: Int, rest: Int = 2) -> Int {
    return local + rest
}

func inoutp(v: inout Int) {
    v += 1
}

func variadic(items: Int..., tail: String) -> Int {
    return items.count
}

func throwing() throws -> Int {
    return 1
}

func loading() async throws -> Int {
    return 1
}

func closure(x: Int, f: (Int) -> Int) -> Int {
    return f(x)
}

func containers(m: [String: Int], a: [Int], o: Int?) -> Int {
    return a.count + (o ?? m.count)
}
"""

CORPUS_A = {
    "Package.swift": manifest_a,
    "App/main.swift": main_app,
    "App/kituser.swift": kit_user,
    "Widgets/Widget.swift": widget,
    "Tools/Helper.swift": helper,
    "Kit/Tag.swift": tag,
    "Core/Engine.swift": engine,
    "Core/Extra.swift": extra,
    "dup_a.swift": dup_a,
    "dup_b.swift": dup_b,
    "longfn.swift": longfn,
    "mathx.swift": mcode,
    "rollup.swift": rollup,
    "todo.swift": todo,
    "credential.swift": credential,
    "decls.swift": decls,
    "types.swift": types,
}

# ============================================================ corpus B ====
# The resolver fixture: the declared-fact MODULE contract end to end --
# the whole-module wholesale fan-out, the kind-scoped member claim, the
# submodule first-segment rule, @testable, the DEFAULT path
# (Sources/<name>), the cycle (OC003 == 1), the unresolved decoys.
# 15 files, 9 contracted edges.

manifest_b = """\
// Resolver corpus B (SPRINT-23): the manifest declares five targets --
// Store, Lib, Wide, Alpha and Beta with EXPLICIT paths, and Std with NO
// path argument: the SPM documented default Sources/<name> applies (the
// default-path rule proven live).
// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "Resolver",
    targets: [
        .target(name: "Store", path: "Store"),
        .target(name: "Lib", path: "Lib"),
        .target(name: "Wide", path: "Wide"),
        .target(name: "Alpha", path: "Alpha"),
        .target(name: "Beta", path: "Beta"),
        .target(name: "Std")
    ]
)
"""

store_engine = """\
// Resolver corpus B (SPRINT-23): module Store carries TWO files -- the
// whole-module claim (@testable or plain) fans out over BOTH (the
// Kotlin wildcard discipline at module scale).
class Engine {
    func start() -> Int {
        return 1
    }
}

func alpha() -> Int {
    return 2
}
"""

store_extra = """\
// Resolver corpus B (SPRINT-23): the SECOND Store file -- the
// whole-module fan-out reaches it without any symbol requirement.
func beta() -> Int {
    return 3
}
"""

gamma = """\
// Resolver corpus B (SPRINT-23): module Lib carries exactly ONE file
// with exactly ONE top-level declaration -- the kind-scoped member
// claim `import func Lib.gamma` marks it by name (OC005 stays empty by
// construction: every export here IS the claimed name).
func gamma() -> Int {
    return 4
}
"""

wide_core = """\
// Resolver corpus B (SPRINT-23): module Wide carries TWO files; the
// whole-module import fans out over both (Core.swift AND Deep.swift).
func walpha() -> Int {
    return 5
}
"""

wide_deep = """\
// Resolver corpus B (SPRINT-23): the SECOND Wide file -- the fan-out
// target (set semantics, lexicographic order).
func wbeta() -> Int {
    return 6
}
"""

cyc_one = """\
// Resolver corpus B (SPRINT-23): the import cycle, side one -- the
// Alpha target claims module Beta; OC003 pins the cycle (the analyzer
// reports the declared facts: SPM would refuse this at build time, the
// scan of sources does not).
import Beta

class One {
    let pair = 1
}
"""

cyc_two = """\
// Resolver corpus B (SPRINT-23): the import cycle, side two -- Beta
// claims Alpha back (the cycle closes; OC003 == 1 on this corpus).
import Alpha

class Two {
    let mate = 2
}
"""

std = """\
// Resolver corpus B (SPRINT-23): the DEFAULT-path target Std -- no
// `path:` argument in the manifest, the SPM documented default
// Sources/<name> covers this file (the declared-path rule, proven
// live).
func standalone() -> Int {
    return 9
}
"""

wide_user = """\
// Resolver corpus B (SPRINT-23): the whole-module claim from a LOOSE
// file (module-less importers are legal: only the TARGET needs
// declared facts) -- `import Wide` fans out over BOTH Wide files.
import Wide

func wideUser() -> Int {
    return walpha() + wbeta()
}
"""

gamma_user = """\
// Resolver corpus B (SPRINT-23): the kind-scoped member claim -- the
// LAST segment names the member, the module is the FIRST (the named
// bind, the over-mark direction bounded by the declared facts).
import func Lib.gamma

func gammaUser() -> Int {
    return gamma()
}
"""

std_user = """\
// Resolver corpus B (SPRINT-23): the default-path target claim --
// `import Std` resolves through the SPM documented default
// Sources/Std.
import Std

func stdUser() -> Int {
    return standalone()
}
"""

decoy = """\
// Resolver corpus B (SPRINT-23): the unresolved decoys -- UIKit and
// Foundation are no declared target of this manifest (external
// modules), Mod.Missing names no target either: recorded and
// unresolved, never guessed. OC003/OC005 count nothing for them.
import UIKit
import Foundation
import Mod.Missing

func decoy() -> Int {
    return 0
}
"""

testable_user = """\
// Resolver corpus B (SPRINT-23): @testable import -- the attribute
// never changes the module: the whole-module claim fans out over BOTH
// Store files (the Kotlin wildcard discipline).
@testable import Store

func testUser() -> Int {
    return Engine().start() + alpha() + beta()
}
"""

sub_user = """\
// Resolver corpus B (SPRINT-23): the submodule spelling -- the FIRST
// segment is the module (the recorded v1 boundary): `import Lib.Sub`
// resolves to module Lib's file exactly like the plain claim.
import Lib.Sub

func subUser() -> Int {
    return gamma()
}
"""

CORPUS_B = {
    "Package.swift": manifest_b,
    "Store/Engine.swift": store_engine,
    "Store/Extra.swift": store_extra,
    "Lib/Gamma.swift": gamma,
    "Wide/Core.swift": wide_core,
    "Wide/Deep.swift": wide_deep,
    "Alpha/One.swift": cyc_one,
    "Beta/Two.swift": cyc_two,
    "Sources/Std/Std.swift": std,
    "wide.swift": wide_user,
    "gamma.swift": gamma_user,
    "std.swift": std_user,
    "decoy.swift": decoy,
    "testable.swift": testable_user,
    "sub.swift": sub_user,
}

# ------------------------------------------------------------ coverage ----
# The OC007 composition fixture (SPRINT-23): a Cobertura-shaped XML (the
# gcovr shape -- the SAME reader the C/C++/Kotlin packs take) over the
# corpus-A files: sum26's lines fully covered, so the CRAP composition
# equals comp at 100 percent coverage; longRunner zero-covered.
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
       '    <package name="swift" line-rate="1.0" branch-rate="0" complexity="0">',
       '      <classes>']
cov.append('        <class name="MathxSwift" filename="mathx.swift" line-rate="1.0" '
           'branch-rate="0" complexity="0">')
cov.append('          <methods />')
cov.append('          <lines>')
for ln in sum_lines:
    cov.append(f'            <line number="{ln}" hits="1" branch="false" />')
cov.append('          </lines>')
cov.append('        </class>')
cov.append('        <class name="LongfnSwift" filename="longfn.swift" line-rate="0.0" '
           'branch-rate="0" complexity="0">')
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
    for root, corpus in ((ROOT, CORPUS_A), (RES_ROOT, CORPUS_B)):
        for name, text in corpus.items():
            p = os.path.join(root, name)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w") as f:
                f.write(text)
    os.makedirs(FIX23, exist_ok=True)
    with open(os.path.join(FIX23, "cobertura_swift.xml"), "w") as f:
        f.write(cobertura)
    print(f"swift corpus A: {len(CORPUS_A)} files -> {ROOT}")
    print(f"swift corpus B: {len(CORPUS_B)} files -> {RES_ROOT}")
    print(f"coverage fixture -> {os.path.join(FIX23, 'cobertura_swift.xml')}")
    print("rollup decisions: 13 (comp 14)")
    print("sum26 decisions: 34 (comp 35)")
    print("longRunner extent: 63 lines")
    print(f"sum26 covered lines: {len(sum_lines)}")


if __name__ == "__main__":
    main()
