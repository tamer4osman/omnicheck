#!/usr/bin/env python3
r"""Scala language acceptance gate (SPRINT-24, the lang pattern applied
to the hand-written Scala fixture corpora).

Locks the contracted posture of docs/LANGUAGES.md (Scala section)
against TWO pinned fixture corpora:

Corpus A -- tests/fixtures/scala (structural, 13 files):
  1. Parse clean, the scala pack loaded (the kill-switch criterion).
  2. Byte-identical reports across N consecutive runs.
  3. Exactly the enabled structural rules fire with contracted counts:
     OC001 (the 63-line function), OC002 (the two TODO markers),
     OC004 (complexity 38 -- the hand-counted decision inventory incl.
     the fourteen if_expression, the twelve match arms, the two catch
     arms and the gated boolean infix; every arm counts, the Rust
     match_arm discipline), OC006 (9 non-entry files; main_app.scala is
     claimed by the object-main PARSE FACT, decls.scala by @main, and
     widget.scala/tools.scala by the import edges), OC008 (one group,
     2 locations), OC011 (the credential val, exactly once).
     OC003/OC005 (enabled with the resolver) and OC007/OC009/OC010
     (inputless) never fire here. The OC006 set is checked EXACTLY
     (the nine files).
  4. Honesty invariants: exactly TWO imports edges (main_app.scala ->
     widget.scala through the single-type import, main_app.scala ->
     tools.scala through the wildcard fan-out); an edge always means
     "the target is part of this analysis"; NO header_routing block
     exists in the meta (no C/C++ pack is involved on this corpus).
  5. The declared-fact resolver, unit corpus B --
     tests/fixtures/scala_resolver (10 files): SIX imports edges with
     the contracted endpoint pairs (the set semantics dedupe the alias
     onto the single-type import's file and the selectors onto the
     wildcard's file). The wildcard `com.example.lib.types.*` fans out
     over typesr.scala (the Java asterisk-marker discipline); the
     single-type import of core resolves to core.scala (the declared
     package + the declared symbol); the ambiguous member shape
     `com.example.lib.Store.total` claims BOTH readings (store.scala
     through the object and member.scala through the package clause);
     the cycle closes over cyc_one.scala/cyc_two.scala (OC003 exactly
     once). The decoys stay unresolved: java.util.ArrayList (the
     prefix `java` matches no declared package),
     com.example.missing.Ghost (no such declared package). OC005's
     verdict set is EMPTY by construction (every claimed export is
     marked; the one unclaimed file carries OC006 instead -- the
     honest Kotlin corpus B shape). OC006 fires exactly four times
     (libuser.scala, decoy.scala, nopkg.scala, extra.scala).

SPRINT-24 closeout (criteria 6-9):
  6. Cobertura composition (corpus A) -- the composed Cobertura XML
     measures 3 of 17 functions; sum26 (comp 38) is uncovered and
     CRAP=1482, exactly ONE OC007 finding (threshold 30); the fully
     covered rollup (comp 14) produces NO OC007 -- "fully covered is
     not invisible".
  7. Supply-tier honesty (corpus B) -- the supply meta block is ABSENT
     with zero manifests (no Scala manifest reader exists in v1: a
     build.sbt libraryDependencies call is not a pin and is not read --
     nothing is fabricated), OC010 == 0, OC009 == 0, and the resolver
     edge set is UNCHANGED.
  8. Shape pins: the extension method shout IS a symbol (the Swift
     extension precedent); the comp pins rollup == 14 and sum26 == 38;
     longRunner carries its 63-line span; the pack routes .scala and
     .sc; the given/extension containers are never symbols.
  9. The Kotlin freeze leg (the SPRINT-22 freeze honored): with the
     scala pack REGISTERED, the Kotlin corpus scans UNCHANGED -- the
     SPRINT-22 counts (analyzed 14, OC001 1, OC002 2, OC004 1, OC006
     10, OC008 2, OC011 1) and the three import edges. The scala
     registration moved nothing the Kotlin pack owns.

Usage:
  python3 scripts/acceptance/lang_scala_gate.py --oc build/default/oc \
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
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORPUS_A = os.path.join(REPO, "tests", "fixtures", "scala")
CORPUS_B = os.path.join(REPO, "tests", "fixtures", "scala_resolver")
FIXKT = os.path.join(REPO, "tests", "fixtures", "kotlin")
COVERAGE = os.path.join(REPO, "tests", "fixtures_sprint24", "cobertura_scala.xml")


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
    tmp = tempfile.mkdtemp(prefix="scagate.")
    try:
        # ---- criterion 1: parse clean + the pack loaded ----------------
        rep = run_oc(oc, ["check", CORPUS_A], REPO)
        ok1 = (rep["meta"]["files_analyzed"] == 13
               and rep["meta"]["files_with_parse_errors"] == 0
               and any(p == "scala" for p in rep["meta"]["packs"]))
        criteria["1_parse_clean_pack_loaded"] = {
            "pass": ok1, "files_analyzed": rep["meta"]["files_analyzed"],
            "parse_errors": rep["meta"]["files_with_parse_errors"],
            "scala_pack_loaded": any(p == "scala" for p in rep["meta"]["packs"])}
        if not ok1:
            print("criterion 1 FAILED"); sys.exit(1)

        # ---- criterion 2: byte-identical x N ---------------------------
        digests = set()
        walls = []
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
        want = {"OC001": 1, "OC002": 2, "OC004": 1, "OC006": 9, "OC008": 2, "OC011": 1,
                "OC003": 0, "OC005": 0, "OC007": 0, "OC009": 0, "OC010": 0}
        got = {k: c.get(k, 0) for k in want}
        ok3 = got == want
        # the OC006 set EXACTLY: the nine non-entry files
        want6 = {"credential.scala", "dup_a.scala", "dup_b.scala", "kit.scala",
                 "longfn.scala", "mathx.scala", "rollup.scala", "todo.scala",
                 "types.scala"}
        got6 = set()
        for f in rep.get("findings", []):
            if f["rule"] == "OC006":
                got6.add(os.path.basename(f.get("file") or f.get("path") or ""))
        ok3 = ok3 and got6 == want6
        criteria["3_enabled_rules_contracted_counts"] = {
            "pass": ok3, "counts": got, "oc006_files_exact": got6 == want6}
        if not ok3:
            print(f"criterion 3 FAILED: {got} vs {want}"); sys.exit(1)

        # ---- criterion 4: the import edges + no routing block ----------
        g = run_oc(oc, ["graph", CORPUS_A], REPO)
        edges = edges_of(g)
        want_edges = [("main_app.scala", "tools.scala"), ("main_app.scala", "widget.scala")]
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
        want_b = [("cyc_one.scala", "cyc_two.scala"), ("cyc_two.scala", "cyc_one.scala"),
                  ("libuser.scala", "core.scala"), ("libuser.scala", "member.scala"),
                  ("libuser.scala", "store.scala"), ("libuser.scala", "typesr.scala")]
        cb = counts_of(rep_b)
        ok5 = (edges_b == want_b
               and cb.get("OC003", 0) == 1
               and cb.get("OC005", 0) == 0
               and cb.get("OC006", 0) == 4
               and rep_b["meta"]["files_with_parse_errors"] == 0)
        criteria["5_resolver_v1_scala_exact"] = {
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
        rollup_hits = [f for f in rep_cov.get("findings", [])
                       if f["rule"] == "OC007" and "rollup" in json.dumps(f.get("symbol", ""))]
        ok6 = (cov.get("functions_total") == 17
               and cov.get("functions_measured") == 3
               and cc.get("OC007", 0) == 1
               and len(crap_hits) == 1
               and len(rollup_hits) == 0
               and "CRAP=1482" in json.dumps(rep_cov.get("findings", [])))
        criteria["6_coverage_composition"] = {
            "pass": ok6, "functions_total": cov.get("functions_total"),
            "functions_measured": cov.get("functions_measured"),
            "oc007": cc.get("OC007", 0),
            "fully_covered_rollup_silent": len(rollup_hits) == 0}
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

        # ---- criterion 8: the shape pins --------------------------------
        g_acc = run_oc(oc, ["graph", CORPUS_A], REPO)
        names = {n["name"]: n for n in g_acc.get("nodes", []) if n.get("kind") == "function"}
        lr = names.get("longRunner", {})
        ok8 = ("shout" in names                                # the extension method
               and "hit" in names                              # the given/container members
               and names.get("rollup", {}).get("complexity") == 14
               and names.get("sum26", {}).get("complexity") == 38
               and lr.get("def_end", 0) - lr.get("def_start", 0) + 1 == 63)
        criteria["8_shape_pins"] = {
            "pass": ok8,
            "comps": {"rollup": names.get("rollup", {}).get("complexity"),
                      "sum26": names.get("sum26", {}).get("complexity")},
            "longRunner_span": (lr.get("def_end", 0) - lr.get("def_start", 0) + 1
                                if lr else None)}
        if not ok8:
            print(f"criterion 8 FAILED: {sorted(names)[:24]}"); sys.exit(1)

        # ---- criterion 9: the Kotlin freeze leg --------------------------
        rep_kt = run_oc(oc, ["check", FIXKT], REPO)
        kt2 = counts_of(rep_kt)
        g_kt = run_oc(oc, ["graph", FIXKT], REPO)
        kt_edges = edges_of(g_kt)
        ok9 = (rep_kt["meta"]["files_analyzed"] == 14
               and kt2.get("OC001", 0) == 1 and kt2.get("OC002", 0) == 2
               and kt2.get("OC004", 0) == 1 and kt2.get("OC006", 0) == 10
               and kt2.get("OC008", 0) == 2 and kt2.get("OC011", 0) == 1
               and kt_edges == [("include_user.kt", "kit.kt"),
                                ("main_app.kt", "tools.kt"),
                                ("main_app.kt", "widget.kt")])
        criteria["9_kotlin_freeze_leg"] = {
            "pass": ok9, "kt_counts": {k: kt2.get(k, 0) for k in
                                       ("OC001", "OC002", "OC004", "OC006", "OC008", "OC011")},
            "kt_edges": kt_edges}
        if not ok9:
            print(f"criterion 9 FAILED: kt={ok9}"); sys.exit(1)

        # ---- supporting facts --------------------------------------------
        verdict = {
            "schema": "omnicheck.evidence/v0",
            "gate": "Scala language adoption (SPRINT-24, full scope: resolver "
                    "v1-scala declared-fact (SHARED with v1-kotlin) + the gated "
                    "boolean infix kind + the honest empty supply tier + the Kotlin "
                    "freeze leg)",
            "date_executed": "2026-11-23",
            "fixtures_structural": CORPUS_A,
            "fixtures_resolver": CORPUS_B,
            "contract": "docs/LANGUAGES.md (Scala section)",
            "grammar_pin": "tree-sitter/tree-sitter-scala v0.23.4",
            "criteria": criteria,
            "supporting": {
                "corpus_a_wall_seconds": round(max(walls), 3),
                "runs": args.runs,
                "single_core": True,
            },
        }
        out = args.verdict or os.path.join(REPO, "docs", "evidence",
                                           "lang_scala_gate_verdict.json")
        with open(out, "w") as f:
            json.dump(verdict, f, indent=1, sort_keys=True)
        print("verdict: PASS ->", out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
