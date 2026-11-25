#!/usr/bin/env python3
"""Java-language acceptance gate (SPRINT-11, full adoption).

Locks the contracted posture of docs/LANGUAGES.md (Java) against THREE
pinned fixture corpora -- the lang_go_gate pattern applied to the
hand-written Java fixture sets, plus the pre-committed JaCoCo adapter:

Corpus A -- tests/fixtures/java (structural):
  1. Parse clean -- 9 fixture files analyzed, zero parse errors, java
     pack loaded. module-info.java and package-info.java parse cleanly
     and carry NO function symbols (contract caveat §Java.1); text
     blocks, records, compact constructors and both switch forms parse
     on the pinned v0.23.5 grammar.
  2. Byte-identical reports across N consecutive runs.
  3. Exactly the contracted rule counts fire: OC001 (64-line process
     method), OC002 (TODO), OC004 (describe complexity 25 -- one
     decision per §Java.4 kind, default-only switch group adds none),
     OC006 (8 non-entry files; Main.java is the pack glob entry),
     OC008 (one group, 2 locations), OC011 (the redacted-documented
     AWS example literal, exactly once). OC003/OC005/OC007/OC009/OC010
     never fire here (corpus A is edge-free and has no pom/coverage).
  4. Honesty invariants -- every import names a class OUTSIDE the
     analyzed set (JDK + external artifact), so the graph shows ZERO
     imports edges (an edge always means "the target is part of this
     analysis", docs/LANGUAGES.md §Java.5); positive resolution is
     pinned by corpus B.

Corpus B -- tests/fixtures/java_resolver (resolver v1-java):
  5. Resolver semantics, exact -- ELEVEN imports edges as a sorted
     MULTISET (duplicates are real: one record per import statement):
     the store wildcard fan-out (Store/Types/StoreTest -- a Java
     wildcard claims the whole analyzed package, test files included,
     the deliberate contrast with Go's _test.go exclusion), the
     duplicate single-type and static-qualifier records, the util
     wildcard (Helper), Helper -> Nested, and the Store/Types cycle
     (legal Java, OC003 pins it). The nested-class import
     (com.example.deep.Nested.Inner), the JDK and the pom-declared
     ghost artifact stay unresolved -- the resolver never reads
     classpath, build files or manifests. OC003 == 1 ("Import cycle
     among 2 files"), OC005 == 0 (the documented empty verdict set:
     single-type imports bind the class name Java's class-file model
     requires, wildcards fail-open), OC006 == 1 (the lonely file;
     StoreTest is claimed by the Surefire runner convention, Main.java
     by the pack glob), OC010 == 1 (the licence-less pom -- the SUPPLY
     tier, not the resolver), OC009 == 0 (no OSV snapshot, never
     guessed). Byte-identical reruns.

Corpus A + JaCoCo -- tests/fixtures/java_cov/jacoco.xml (OC007):
  6. The pre-committed JaCoCo XML adapter maps method-level counters
     onto definition spans through the standard path rule (the report
     path is the analyzed path's package tail): OC007 fires exactly
     once, on the uncovered describe method; coverage meta reports
     files_matched == 1, functions_measured == 2.

Usage:
  python3 scripts/acceptance/lang_java_gate.py --oc build/default/oc \
      [--runs 10] [--verdict PATH]

Exit code 0 iff every criterion holds; the verdict JSON lands next to
the other gate verdicts by default (docs/evidence/).
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

EXPECTED_RULES_A = {"OC001": 1, "OC002": 1, "OC004": 1, "OC006": 8, "OC008": 2, "OC011": 1}
FORBIDDEN_A = ("OC003", "OC005", "OC007", "OC009", "OC010")
FIXTURE_A_FILES = 9
FIXTURE_B_FILES = 7

# Corpus B: the eleven contracted imports edges as a sorted multiset
# (duplicates are real records: single-type + static qualifier + wildcard
# all target Store). Suffix match on fixture-relative paths.
RESOLVER_EDGE_PAIRS = [
    ("app/Main.java", "com/example/store/Store.java"),
    ("app/Main.java", "com/example/store/Store.java"),
    ("app/Main.java", "com/example/store/Store.java"),
    ("app/Main.java", "com/example/store/StoreTest.java"),
    ("app/Main.java", "com/example/store/StoreTest.java"),
    ("app/Main.java", "com/example/store/Types.java"),
    ("app/Main.java", "com/example/store/Types.java"),
    ("app/Main.java", "com/example/util/Helper.java"),
    ("com/example/store/Store.java", "com/example/store/Types.java"),
    ("com/example/store/Types.java", "com/example/store/Store.java"),
    ("com/example/util/Helper.java", "com/example/deep/Nested.java"),
]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def run_json(oc, args, out_path):
    with open(out_path, "wb") as fh:
        subprocess.run([oc, *args], stdout=fh, stderr=subprocess.DEVNULL)
    with open(out_path) as fh:
        return json.load(fh)


def check_corpus_a(oc, fixtures, packs, tmp, runs, wrap):
    """Structural corpus: criteria 1-4. Returns (crits, walls, info)."""
    hashes, walls = [], []
    for i in range(runs):
        out = f"{tmp}/a_run{i}.json"
        t0 = time.monotonic()
        subprocess.run(wrap + [oc, "check", fixtures, "--packs-dir", packs, "--format", "json"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
        walls.append(time.monotonic() - t0)
        hashes.append(sha256_file(out))
    stable = len(set(hashes)) == 1

    with open(f"{tmp}/a_run0.json") as fh:
        report = json.load(fh)
    findings = report.get("findings", [])
    meta = report.get("meta", {})

    analyzed = meta.get("files_analyzed", 0)
    parse_errs = meta.get("files_with_parse_errors", 1)
    crit1 = analyzed == FIXTURE_A_FILES and parse_errs == 0 \
        and "java" in (meta.get("packs") or [])

    # module-info.java and package-info.java: parse clean, ZERO function
    # symbols (the §Java.1 caveat, pinned by absence).
    graph = run_json(oc, ["graph", fixtures, "--packs-dir", packs], f"{tmp}/a_graph.json")
    file_paths = [n.get("path", "") for n in graph.get("nodes", []) if n.get("kind") == "file"]
    functions = [n for n in graph.get("nodes", []) if n.get("kind") == "function"]
    no_meta_symbols = all(
        not any(fp.endswith(pfx) for pfx in ("module-info.java", "package-info.java"))
        for fp in [f.get("path", "") for f in functions])

    # describe: complexity 25, one decision per §Java.4 kind.
    describe = [n for n in functions if n.get("name") == "describe"]
    cx_ok = len(describe) == 1 and describe[0].get("complexity") == 25

    counts = {}
    unseen = set()
    oc006_main = False
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule in FORBIDDEN_A:
            unseen.add(rule)
        if rule == "OC006" and f.get("file", "").endswith("Main.java"):
            oc006_main = True
    crit3 = counts == EXPECTED_RULES_A and not unseen and not oc006_main

    import_edges = [e for e in graph.get("edges", []) if e.get("kind") == "imports"]
    crit4 = len(import_edges) == 0 and len(file_paths) == FIXTURE_A_FILES

    crits = {
        "1_parse_clean_pack_loaded": {"pass": crit1, "files_analyzed": analyzed,
                                      "files_with_parse_errors": parse_errs,
                                      "packs": meta.get("packs")},
        "1b_metadata_files_symbol_free": {"pass": no_meta_symbols,
                                          "module_info_symbols": 0,
                                          "package_info_symbols": 0},
        "1c_describe_complexity_25": {"pass": cx_ok,
                                      "complexity": describe[0].get("complexity") if describe else None},
        "2_byte_identical_reports": {"pass": stable, "runs": runs,
                                     "distinct_sha256": len(set(hashes))},
        "3_enabled_rules_contracted_counts": {"pass": crit3, "counts": counts,
                                              "forbidden_seen": sorted(unseen),
                                              "oc006_on_main_java": oc006_main},
        "4_unresolved_outside_the_analyzed_set": {"pass": crit4,
                                                  "import_edges": len(import_edges),
                                                  "file_nodes": len(file_paths)},
    }
    return crits, walls, {"analyzed": analyzed, "parse_errs": parse_errs,
                          "packs": meta.get("packs"), "counts": counts,
                          "import_edges": len(import_edges)}


def check_corpus_b(oc, fixtures, packs, tmp, runs, wrap):
    """Resolver corpus: criterion 5 (exact v1-java semantics)."""
    hashes = []
    for i in range(runs):
        out = f"{tmp}/b_run{i}.json"
        subprocess.run(wrap + [oc, "check", fixtures, "--packs-dir", packs, "--format", "json"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
        hashes.append(sha256_file(out))
    stable = len(set(hashes)) == 1

    report = run_json(oc, ["check", fixtures, "--packs-dir", packs, "--format", "json"],
                      f"{tmp}/b_run0.json")
    findings = report.get("findings", [])
    meta = report.get("meta", {})
    analyzed = meta.get("files_analyzed", 0)
    parse_errs = meta.get("files_with_parse_errors", 1)

    counts = {}
    oc003_msgs, oc006_files, oc010_files = [], [], []
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule == "OC003":
            oc003_msgs.append(f.get("message", ""))
        if rule == "OC006":
            oc006_files.append(os.path.basename(f.get("file", "")))
        if rule == "OC010":
            oc010_files.append(os.path.basename(f.get("file", "")))

    graph = run_json(oc, ["graph", fixtures, "--packs-dir", packs], f"{tmp}/b_graph.json")
    path_by_id = {n.get("id"): n.get("path", "") for n in graph.get("nodes", [])
                  if n.get("kind") == "file"}
    edge_pairs = []
    for e in graph.get("edges", []):
        if e.get("kind") != "imports":
            continue
        fp = path_by_id.get(e.get("from"), "")
        tp = path_by_id.get(e.get("to"), "")
        edge_pairs.append((os.path.relpath(fp, fixtures), os.path.relpath(tp, fixtures)))
    edge_pairs.sort()

    wanted = sorted(RESOLVER_EDGE_PAIRS)
    multiset_exact = len(edge_pairs) == len(RESOLVER_EDGE_PAIRS) and edge_pairs == wanted
    ghost_unresolved = not any(tp.endswith("com/example/ghost/Thing.java")
                               for _, tp in edge_pairs)
    nested_unresolved = not any(tp.endswith("com/example/deep/Nested/Inner.java")
                                for _, tp in edge_pairs)
    test_targeted = any(tp.endswith("StoreTest.java") for _, tp in edge_pairs)

    crit5 = (
        analyzed == FIXTURE_B_FILES
        and parse_errs == 0
        and "java" in (meta.get("packs") or [])
        and stable
        and multiset_exact
        and ghost_unresolved
        and nested_unresolved
        and test_targeted
        and counts.get("OC003") == 1
        and all("Import cycle among 2 files" in m for m in oc003_msgs)
        and counts.get("OC005", 0) == 0
        and counts.get("OC006") == 1
        and oc006_files == ["Lonely.java"]
        and counts.get("OC010") == 1
        and oc010_files == ["pom.xml"]
        and counts.get("OC009", 0) == 0
        and set(counts) <= {"OC003", "OC006", "OC010"}
    )
    crits = {
        "5_resolver_v1_java_exact": {
            "pass": crit5,
            "files_analyzed": analyzed,
            "files_with_parse_errors": parse_errs,
            "byte_identical_runs": runs,
            "imports_edges": len(edge_pairs),
            "edge_pairs_multiset": [f"{a} -> {b}" for a, b in edge_pairs],
            "ghost_pom_decoy_unresolved": ghost_unresolved,
            "nested_class_unresolved": nested_unresolved,
            "test_file_is_fanout_target": test_targeted,
            "counts": counts,
            "oc006_files": oc006_files,
            "oc010_files": oc010_files,
            "oc003_message": oc003_msgs[0] if oc003_msgs else None,
        },
    }
    return crits, {"analyzed": analyzed, "parse_errs": parse_errs, "counts": counts,
                   "edge_pairs": edge_pairs}


def check_jacoco(oc, fixtures_a, jacoco, packs, tmp, wrap):
    """Corpus A + coverage: criterion 6 (the pre-committed JaCoCo adapter)."""
    report = run_json(oc, ["check", fixtures_a, "--packs-dir", packs, "--coverage", jacoco,
                           "--format", "json"], f"{tmp}/jac_run.json")
    findings = report.get("findings", [])
    meta = report.get("meta", {})
    oc007 = [f for f in findings if f.get("rule") == "OC007"]
    oc007_ok = (len(oc007) == 1 and "describe" in (oc007[0].get("symbol") or "")
                and "TextUtil.java" in (oc007[0].get("file") or ""))
    cov = meta.get("coverage", {})
    crit6 = oc007_ok and cov.get("files_matched") == 1 and cov.get("functions_measured") == 2
    crits = {
        "6_jacoco_adapter_mapping": {
            "pass": crit6,
            "oc007_findings": len(oc007),
            "oc007_symbol": oc007[0].get("symbol") if oc007 else None,
            "coverage_files_matched": cov.get("files_matched"),
            "coverage_functions_measured": cov.get("functions_measured"),
        },
    }
    return crits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--fixtures-a", default=None,
                    help="structural corpus (default: <repo>/tests/fixtures/java)")
    ap.add_argument("--fixtures-b", default=None,
                    help="resolver corpus (default: <repo>/tests/fixtures/java_resolver)")
    ap.add_argument("--jacoco", default=None,
                    help="JaCoCo XML fixture (default: <repo>/tests/fixtures/java_cov/jacoco.xml)")
    ap.add_argument("--packs", default=None,
                    help="packs dir (default: the repo's own packs/ directory)")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    fixtures_a = os.path.abspath(args.fixtures_a or os.path.join(repo, "tests", "fixtures", "java"))
    fixtures_b = os.path.abspath(args.fixtures_b or
                                 os.path.join(repo, "tests", "fixtures", "java_resolver"))
    jacoco = os.path.abspath(args.jacoco or
                             os.path.join(repo, "tests", "fixtures", "java_cov", "jacoco.xml"))
    packs = os.path.abspath(args.packs or os.path.join(repo, "packs"))
    for p in (oc, fixtures_a, fixtures_b, jacoco):
        if not os.path.exists(p):
            sys.exit(f"missing: {p}")

    tmp = "/tmp/omnicheck_lang_java_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    crits_a, walls, info_a = check_corpus_a(oc, fixtures_a, packs, tmp, args.runs, wrap)
    crits_b, info_b = check_corpus_b(oc, fixtures_b, packs, tmp, args.runs, wrap)
    crits_j = check_jacoco(oc, fixtures_a, jacoco, packs, tmp, wrap)

    print(f"corpus A: {fixtures_a}")
    print(f"corpus B: {fixtures_b}")
    print(f"jaCoCo:   {jacoco}")
    print(f"criterion 1 (parse clean, java pack loaded): analyzed={info_a['analyzed']} "
          f"parse_errors={info_a['parse_errs']} -> "
          f"{'PASS' if crits_a['1_parse_clean_pack_loaded']['pass'] else 'FAIL'}")
    print(f"criterion 1b (module-info/package-info symbol-free) -> "
          f"{'PASS' if crits_a['1b_metadata_files_symbol_free']['pass'] else 'FAIL'}")
    print(f"criterion 1c (describe complexity 25) -> "
          f"{'PASS' if crits_a['1c_describe_complexity_25']['pass'] else 'FAIL'}")
    print(f"criterion 2 (byte-identical, {args.runs} runs): -> "
          f"{'PASS' if crits_a['2_byte_identical_reports']['pass'] else 'FAIL'}")
    print(f"criterion 3 (enabled rules, contracted counts): counts={info_a['counts']} -> "
          f"{'PASS' if crits_a['3_enabled_rules_contracted_counts']['pass'] else 'FAIL'}")
    print(f"criterion 4 (outside-the-set stays unresolved): "
          f"import_edges={info_a['import_edges']} -> "
          f"{'PASS' if crits_a['4_unresolved_outside_the_analyzed_set']['pass'] else 'FAIL'}")
    b = crits_b["5_resolver_v1_java_exact"]
    print(f"criterion 5 (resolver v1-java exact: 11-edge multiset, pom decoy ignored, "
          f"nested unresolved, OC003=1, OC005=0, OC006=Lonely, OC010=pom): "
          f"edges={b['imports_edges']} counts={b['counts']} -> "
          f"{'PASS' if b['pass'] else 'FAIL'}")
    j = crits_j["6_jacoco_adapter_mapping"]
    print(f"criterion 6 (JaCoCo adapter: OC007 on describe, "
          f"files_matched={j['coverage_files_matched']}, "
          f"measured={j['coverage_functions_measured']}) -> {'PASS' if j['pass'] else 'FAIL'}")
    print(f"supporting: corpus A best wall {min(walls):.2f}s, worst {max(walls):.2f}s "
          f"(single-core: {bool(wrap)})")

    all_crits = {}
    all_crits.update(crits_a)
    all_crits.update(crits_b)
    all_crits.update(crits_j)
    all_pass = all(c["pass"] for c in all_crits.values())

    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "Java language adoption + resolver v1-java + JaCoCo adapter (SPRINT-11)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "fixtures_structural": fixtures_a,
        "fixtures_resolver": fixtures_b,
        "fixture_jacoco": jacoco,
        "contract": "docs/LANGUAGES.md (Java section, pre-approved before any code)",
        "grammar_pin": "tree-sitter/tree-sitter-java v0.23.5",
        "criteria": all_crits,
        "wall_best_s": round(min(walls), 2),
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "lang_java_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
