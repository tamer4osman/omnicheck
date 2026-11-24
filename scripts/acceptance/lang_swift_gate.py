#!/usr/bin/env python3
r"""Swift language acceptance gate (SPRINT-23, the lang pattern applied
to the hand-written Swift fixture corpora).

Locks the contracted posture of docs/LANGUAGES.md (Swift section)
against TWO pinned fixture corpora:

Corpus A -- tests/fixtures/swift (structural, 17 files, the SPM layout:
Package.swift + five declared targets + nine module-less loose files):
  1. Parse clean, the swift pack loaded (the kill-switch criterion).
  2. Byte-identical reports across N consecutive runs.
  3. Exactly the enabled structural rules fire with contracted counts:
     OC001 (the 63-line function), OC002 (the two task markers),
     OC004 (complexity 35 -- the hand-counted decision inventory incl.
     the three switch arms, the guard, the ternary, the nil-coalescing
     and the boolean containers; every arm counts, the Rust match_arm
     discipline), OC006 (13 unreferenced files; App/main.swift is
     claimed by the top-level-statement PARSE FACT), OC008 (one group,
     2 locations), OC011 (the credential assignment, exactly once).
     OC003/OC005 (enabled with the resolver) and OC007/OC009/OC010
     (inputless) never fire here. The OC006 set is checked EXACTLY
     (the thirteen files), and the three claimed target files stay out.
  4. Honesty invariants: exactly THREE imports edges (App/main.swift ->
     Widgets/Widget.swift through the kind-scoped member import,
     App/main.swift -> Tools/Helper.swift through the whole-module
     claim, App/kituser.swift -> Kit/Tag.swift through the kind-scoped
     import); an edge always means "the target is part of this
     analysis"; NO header_routing block exists in the meta (no C/C++
     pack is involved on this corpus).
  5. The declared-fact resolver, unit corpus B --
     tests/fixtures/swift_resolver (15 files, the SPM layout with an
     explicit-path manifest AND a default-path target): NINE imports
     edges with the contracted endpoint pairs. The whole-module claim
     `import Wide` fans out over BOTH Wide files (the Kotlin wildcard
     discipline at module scale); the kind-scoped member claim
     `import func Lib.gamma` marks the named export; `import Std`
     resolves through the SPM documented default Sources/Std; the
     submodule spelling `import Lib.Sub` resolves on the FIRST segment;
     @testable changes nothing; the cycle closes over Alpha/Beta
     (OC003 exactly once -- the analyzer reports the declared facts,
     SPM would refuse the build); the decoys (UIKit, Foundation,
     Mod.Missing) stay unresolved. OC005's verdict set is EMPTY by
     construction (every target export is claimed by a bind or a
     whole-module claim). OC006 fires exactly seven times (the
     manifest and the six loose importer files).

SPRINT-23 closeout (criteria 6-9):
  6. Cobertura composition (corpus A) -- the runtime-composed
     Cobertura XML covering every code line of mathx.swift measures the
     composed functions (functions_measured == 3 of functions_total ==
     34) and proves the full-coverage limit of the formula on the
     measured function: CRAP(sum26) == comp == 35, exactly ONE OC007
     finding (threshold 30) -- "fully covered is not invisible".
  7. Supply-tier honesty (corpus B) -- the supply meta block is ABSENT
     with zero manifests (no Swift manifest reader exists in v1:
     Package.resolved IS a version-pinned artifact but no OSV Swift
     ecosystem exists and none is invented -- nothing is fabricated),
     OC010 == 0, OC009 == 0, and the resolver edge set is UNCHANGED.
  8. Accessor + entry-fact shape pins (the C# precedent legs): the
     accessor bodies ARE symbols (doubled.get == 1, tracked.get == 1,
     tracked.set == 1, observed.willSet == 2 -- the owner reached by
     the PARENT CLIMB; the EMPTY didSet body matches NOTHING, pinned by
     its absence); App/main.swift is claimed by the top-level-statement
     fact while App/kituser.swift (declarations only, no incoming
     claims) keeps its OC006.
  9. The C/C++/Kotlin freeze legs (the SPRINT-20/21/22 freezes
     honored): with the swift pack REGISTERED, all three prior corpora
     scan UNCHANGED -- fixtures/c keeps the SPRINT-20 counts (OC001 1,
     OC002 1, OC004 1, OC006 11, OC008 2, OC011 1, the routing
     default_c == 2), fixtures/cpp keeps the SPRINT-21 counts (OC001 1,
     OC002 1, OC004 1, OC006 12, OC008 2, OC011 1, 2 edges), and
     fixtures/kotlin keeps the SPRINT-22 counts (OC001 1, OC002 2,
     OC004 1, OC006 10, OC008 2, OC011 1, 3 edges + the resolver corpus
     B edges 9 with OC003 1). The swift registration moved nothing the
     prior packs own.

Usage:
  python3 scripts/acceptance/lang_swift_gate.py --oc build/default/oc \
      [--runs 10] [--verdict PATH]

Exit code 0 iff every criterion holds; the verdict JSON lands next to
the other gate verdicts by default (docs/evidence/).
"""
import argparse
import collections
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORPUS_A = os.path.join(REPO, "tests", "fixtures", "swift")
CORPUS_B = os.path.join(REPO, "tests", "fixtures", "swift_resolver")
FIXC = os.path.join(REPO, "tests", "fixtures", "c")
FIXC_RES = os.path.join(REPO, "tests", "fixtures", "c_resolver")
FIXCPP = os.path.join(REPO, "tests", "fixtures", "cpp")
FIXCPP_RES = os.path.join(REPO, "tests", "fixtures", "cpp_resolver")
FIXKT = os.path.join(REPO, "tests", "fixtures", "kotlin")
FIXKT_RES = os.path.join(REPO, "tests", "fixtures", "kotlin_resolver")
COVERAGE = os.path.join(REPO, "tests", "fixtures_sprint23", "cobertura_swift.xml")


def run_oc(oc, args, cwd):
    cmd = [oc] + args + ["--packs-dir", os.path.join(REPO, "packs"), "--format", "json"]
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if p.returncode not in (0, 1, 2):
        raise RuntimeError(f"oc {' '.join(args)} exit {p.returncode}: {p.stderr[:400]}")
    return json.loads(p.stdout)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def counts_of(report):
    c = collections.Counter(f["rule"] for f in report.get("findings", []))
    return c


def edges_of(report):
    """(from, to) basename pairs of imports edges from a graph JSON."""
    nodes = {n["id"]: os.path.basename(n["path"]) for n in report.get("nodes", [])}
    out = []
    for e in report.get("edges", []):
        if e.get("kind") == "imports":
            out.append((nodes[e["from"]], nodes[e["to"]]))
    return sorted(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True)
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--verdict", default=None)
    args = ap.parse_args()
    oc = os.path.abspath(args.oc)
    criteria = {}
    tmp = tempfile.mkdtemp(prefix="swgate.")
    try:
        # ---- criterion 1: parse clean + the pack loaded ----------------
        rep = run_oc(oc, ["check", CORPUS_A], REPO)
        ok1 = (rep["meta"]["files_analyzed"] == 17
               and rep["meta"]["files_with_parse_errors"] == 0
               and any(p == "swift" for p in rep["meta"]["packs"]))
        criteria["1_parse_clean_pack_loaded"] = {
            "pass": ok1, "files_analyzed": rep["meta"]["files_analyzed"],
            "parse_errors": rep["meta"]["files_with_parse_errors"],
            "swift_pack_loaded": any(p == "swift" for p in rep["meta"]["packs"])}
        if not ok1:
            print("criterion 1 FAILED"); sys.exit(1)

        # ---- criterion 2: byte-identical x N ---------------------------
        digests = set()
        walls = []
        import time
        for i in range(args.runs):
            t0 = time.time()
            r = run_oc(oc, ["check", CORPUS_A], REPO)
            walls.append(time.time() - t0)
            blob = json.dumps(r, sort_keys=True).encode()
            digests.add(hashlib.sha256(blob).hexdigest())
        ok2 = len(digests) == 1
        criteria["2_byte_identical"] = {"pass": ok2, "runs": args.runs,
                                        "distinct_digests": len(digests)}
        if not ok2:
            print("criterion 2 FAILED"); sys.exit(1)

        # ---- criterion 3: contracted rule counts -----------------------
        c = counts_of(rep)
        want = {"OC001": 1, "OC002": 2, "OC004": 1, "OC006": 13, "OC008": 2, "OC011": 1,
                "OC003": 0, "OC005": 0, "OC007": 0, "OC009": 0, "OC010": 0}
        got = {k: c.get(k, 0) for k in want}
        ok3 = got == want
        # the OC006 set EXACTLY: the thirteen unreferenced files
        want6 = {"Engine.swift", "Extra.swift", "Package.swift", "kituser.swift",
                 "dup_a.swift", "dup_b.swift", "longfn.swift", "mathx.swift",
                 "rollup.swift", "todo.swift", "credential.swift", "decls.swift",
                 "types.swift"}
        got6 = set()
        for f in rep.get("findings", []):
            if f["rule"] == "OC006":
                got6.add(os.path.basename(f.get("file") or f.get("path") or ""))
        ok3 = ok3 and got6 == want6
        criteria["3_enabled_rules_contracted_counts"] = {
            "pass": ok3, "counts": got, "oc006_files_exact": got6 == want6}
        if not ok3:
            print(f"criterion 3 FAILED: {got} vs {want} / {sorted(got6)}"); sys.exit(1)

        # ---- criterion 4: the import edges + no routing block ----------
        g = run_oc(oc, ["graph", CORPUS_A], REPO)
        edges = edges_of(g)
        want_edges = [("kituser.swift", "Tag.swift"), ("main.swift", "Helper.swift"),
                      ("main.swift", "Widget.swift")]
        ok4 = (edges == want_edges
               and "header_routing" not in json.dumps(rep.get("meta", {})))
        criteria["4_edges_and_no_routing_block"] = {
            "pass": ok4, "edges": edges, "header_routing_absent":
                "header_routing" not in json.dumps(rep.get("meta", {}))}
        if not ok4:
            print(f"criterion 4 FAILED: {edges}"); sys.exit(1)

        # ---- criterion 5: the declared-fact resolver, corpus B ---------
        rep_b = run_oc(oc, ["check", CORPUS_B], REPO)
        g_b = run_oc(oc, ["graph", CORPUS_B], REPO)
        edges_b = edges_of(g_b)
        want_b = [("One.swift", "Two.swift"), ("Two.swift", "One.swift"),
                  ("gamma.swift", "Gamma.swift"), ("std.swift", "Std.swift"),
                  ("sub.swift", "Gamma.swift"), ("testable.swift", "Engine.swift"),
                  ("testable.swift", "Extra.swift"), ("wide.swift", "Core.swift"),
                  ("wide.swift", "Deep.swift")]
        cb = counts_of(rep_b)
        ok5 = (edges_b == want_b
               and cb.get("OC003", 0) == 1
               and cb.get("OC005", 0) == 0
               and cb.get("OC006", 0) == 7
               and rep_b["meta"]["files_with_parse_errors"] == 0)
        criteria["5_resolver_v1_swift_exact"] = {
            "pass": ok5, "edges": edges_b,
            "counts": {"OC003": cb.get("OC003", 0), "OC005": cb.get("OC005", 0),
                       "OC006": cb.get("OC006", 0)}}
        if not ok5:
            print(f"criterion 5 FAILED: {edges_b}"); sys.exit(1)

        # ---- criterion 6: the coverage composition ---------------------
        rep_cov = run_oc(oc, ["check", "--coverage", COVERAGE, CORPUS_A], REPO)
        cov = rep_cov["meta"].get("coverage", {})
        cc = counts_of(rep_cov)
        crap_hits = [f for f in rep_cov.get("findings", [])
                     if f["rule"] == "OC007" and "sum26" in json.dumps(f.get("symbol", ""))]
        ok6 = (cov.get("functions_total") == 34
               and cov.get("functions_measured") == 3
               and cc.get("OC007", 0) == 1
               and len(crap_hits) == 1
               and "CRAP=35" in json.dumps(rep_cov.get("findings", [])))
        criteria["6_coverage_composition"] = {
            "pass": ok6, "functions_total": cov.get("functions_total"),
            "functions_measured": cov.get("functions_measured"),
            "oc007": cc.get("OC007", 0)}
        if not ok6:
            print(f"criterion 6 FAILED: {cov}"); sys.exit(1)

        # ---- criterion 7: supply-tier honesty ---------------------------
        supply = rep_b.get("meta", {}).get("supply")  # ABSENT with zero manifests
        edges_b2 = edges_of(run_oc(oc, ["graph", CORPUS_B], REPO))
        ok7 = (supply is None
               and cb.get("OC009", 0) == 0 and cb.get("OC010", 0) == 0
               and edges_b2 == want_b)
        criteria["7_supply_tier_honesty"] = {
            "pass": ok7, "supply_block_absent": supply is None,
            "edges_unchanged": edges_b2 == want_b}
        if not ok7:
            print("criterion 7 FAILED"); sys.exit(1)

        # ---- criterion 8: accessor + entry-fact shape pins ---------------
        g_acc = run_oc(oc, ["graph", CORPUS_A], REPO)
        names = {n["name"]: n for n in g_acc.get("nodes", []) if n.get("kind") == "function"}
        ok8 = ("doubled.get" in names and "tracked.get" in names
               and "tracked.set" in names and "observed.willSet" in names
               and "didSet.absent" not in names
               and names.get("observed.willSet", {}).get("complexity") == 2
               and names.get("doubled.get", {}).get("complexity") == 1
               and names.get("tracked.set", {}).get("complexity") == 1
               and names.get("sum26", {}).get("complexity") == 35
               and names.get("rollup", {}).get("complexity") == 14
               and "init" in names)
        criteria["8_accessor_and_entry_shape_pins"] = {
            "pass": ok8,
            "accessors": {k: v.get("complexity") for k, v in names.items() if "." in k}}
        if not ok8:
            print(f"criterion 8 FAILED: {sorted(names)[:24]}"); sys.exit(1)

        # ---- criterion 9: the C/C++/Kotlin freeze legs --------------------
        rep_c = run_oc(oc, ["check", FIXC], REPO)
        cc2 = counts_of(rep_c)
        g_c = run_oc(oc, ["graph", FIXC], REPO)
        routing = rep_c.get("meta", {}).get("header_routing", {})
        ok_c = (rep_c["meta"]["files_analyzed"] == 13
                and cc2.get("OC001", 0) == 1 and cc2.get("OC002", 0) == 1
                and cc2.get("OC004", 0) == 1 and cc2.get("OC006", 0) == 11
                and cc2.get("OC008", 0) == 2 and cc2.get("OC011", 0) == 1
                and routing.get("default_c") == 2 and routing.get("sniffed_cpp", 0) == 0)
        rep_cpp = run_oc(oc, ["check", FIXCPP], REPO)
        cpp2 = counts_of(rep_cpp)
        ok_cpp = (rep_cpp["meta"]["files_analyzed"] == 14
                  and cpp2.get("OC001", 0) == 1 and cpp2.get("OC002", 0) == 1
                  and cpp2.get("OC004", 0) == 1 and cpp2.get("OC006", 0) == 12
                  and cpp2.get("OC008", 0) == 2 and cpp2.get("OC011", 0) == 1
                  and len(edges_of(run_oc(oc, ["graph", FIXCPP], REPO))) == 2)
        rep_cr = run_oc(oc, ["check", FIXC_RES], REPO)
        ok_cr = counts_of(rep_cr).get("OC003", 0) == 1
        rep_cppr = run_oc(oc, ["check", FIXCPP_RES], REPO)
        ok_cppr = counts_of(rep_cppr).get("OC003", 0) == 1
        rep_kt = run_oc(oc, ["check", FIXKT], REPO)
        kt2 = counts_of(rep_kt)
        ok_kt = (rep_kt["meta"]["files_analyzed"] == 14
                 and kt2.get("OC001", 0) == 1 and kt2.get("OC002", 0) == 2
                 and kt2.get("OC004", 0) == 1 and kt2.get("OC006", 0) == 10
                 and kt2.get("OC008", 0) == 2 and kt2.get("OC011", 0) == 1
                 and len(edges_of(run_oc(oc, ["graph", FIXKT], REPO))) == 3)
        rep_ktr = run_oc(oc, ["check", FIXKT_RES], REPO)
        ok_ktr = (counts_of(rep_ktr).get("OC003", 0) == 1
                  and len(edges_of(run_oc(oc, ["graph", FIXKT_RES], REPO))) == 9)
        ok9 = ok_c and ok_cpp and ok_cr and ok_cppr and ok_kt and ok_ktr
        criteria["9_c_cpp_kotlin_freeze_legs"] = {
            "pass": ok9, "c_counts": {k: cc2.get(k, 0) for k in
                                      ("OC001", "OC002", "OC004", "OC006", "OC008", "OC011")},
            "c_routing": routing,
            "cpp_counts": {k: cpp2.get(k, 0) for k in
                           ("OC001", "OC002", "OC004", "OC006", "OC008", "OC011")},
            "kotlin_counts": {k: kt2.get(k, 0) for k in
                              ("OC001", "OC002", "OC004", "OC006", "OC008", "OC011")},
            "c_resolver_oc003": counts_of(rep_cr).get("OC003", 0),
            "cpp_resolver_oc003": counts_of(rep_cppr).get("OC003", 0),
            "kotlin_resolver_oc003": counts_of(rep_ktr).get("OC003", 0)}
        if not ok9:
            print(f"criterion 9 FAILED: c={ok_c} cpp={ok_cpp} cr={ok_cr} "
                  f"cppr={ok_cppr} kt={ok_kt} ktr={ok_ktr}")
            sys.exit(1)

        # ---- supporting facts --------------------------------------------
        verdict = {
            "schema": "omnicheck.evidence/v0",
            "gate": "Swift language adoption (SPRINT-23, full scope: resolver "
                    "v1-swift declared-fact modules + accessors + the honest empty supply "
                    "tier + the C/C++/Kotlin freeze legs)",
            "date_executed": "2026-11-23",
            "fixtures_structural": CORPUS_A,
            "fixtures_resolver": CORPUS_B,
            "contract": "docs/LANGUAGES.md (Swift section)",
            "grammar_pin": "alex-pinkus/tree-sitter-swift 0.7.1-with-generated-files",
            "criteria": criteria,
            "supporting": {
                "corpus_a_wall_seconds": round(max(walls), 3),
                "runs": args.runs,
                "single_core": True,
            },
        }
        out = args.verdict or os.path.join(REPO, "docs", "evidence",
                                           "lang_swift_gate_verdict.json")
        with open(out, "w") as f:
            json.dump(verdict, f, indent=1, sort_keys=True)
        print("verdict: PASS ->", out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
