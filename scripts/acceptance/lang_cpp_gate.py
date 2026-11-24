#!/usr/bin/env python3
r"""C++ language acceptance gate (SPRINT-21, the m2/m3/lang pattern
applied to the hand-written C++ fixture corpora).

Locks the contracted posture of docs/LANGUAGES.md (C++ section) against
TWO pinned fixture corpora:

Corpus A -- tests/fixtures/cpp (structural, 14 files):
  1. Parse clean, the cpp pack loaded (the kill-switch criterion).
  2. Byte-identical reports across N consecutive runs.
  3. Exactly the enabled structural rules fire with contracted counts:
     OC001 (the 63-line function), OC002 (the TODO), OC004 (complexity
     35 -- the hand-counted decision inventory incl. the C++11 range-for
     and the catch handler; the default arm and bare else count 0),
     OC006 (12 non-entry files; main_app.cpp is claimed by the main
     PARSE FACT), OC008 (one group, 2 locations), OC011 (the AWS-docs
     example credential, exactly once). OC003/OC005 (enabled with the
     resolver) and OC007/OC009/OC010 (inputless) never fire here. The
     OC006 set is checked EXACTLY (the twelve files), and local_util.h
     stays out (included twice).
  4. Honesty invariants: exactly TWO imports edges, both into the
     analyzed local_util.h -- the quoted include resolves (an edge
     always means "the target is part of this analysis"); the system
     include, the macro path and the missing-header include produce
     NONE.
  5. The .h routing block (the approved architecture, SPRINT-21):
     types.h SNIFFED to cpp (a C++-distinctive header), local_util.h by
     the PROJECT-ROOT DEFAULT to cpp (the corpus carries .cpp files);
     sniffed_c == 0 and default_c == 0 on this corpus.

Corpus B -- tests/fixtures/cpp_resolver (resolver v1-cpp, 10 files):
  6. Resolver semantics, exact: NINE imports edges with the contracted
     endpoint pairs (quoted includes relative to the INCLUDING file's
     directory: same-dir, nested-include anchoring at the header's own
     directory, the lexical .. pop, the driver edges into both cycle
     headers). The decoys stay unresolved: <vector> (the system channel
     never resolves), HEADER_PATH (a macro path is not a static
     payload), missing.hpp (outside the analyzed set). OC003 fires
     exactly once (the a.hpp/b.hpp include cycle). OC005's verdict set
     is EMPTY by construction (every imported header's exports are
     marked used by the whole-file include binds). OC006 fires exactly
     five times (main.cpp, log.cpp, p.cpp, outer.hpp -- nothing includes
     it -- and cycle_driver.cpp). OC010 fires ZERO times (the honest
     supply tier).

SPRINT-21 closeout (criteria 7-9):
  7. Cobertura composition (corpus A) -- a runtime-composed gcovr-
     shaped Cobertura XML covering every line measures EVERY function
     (functions_measured == functions_total), keeps byte-identical
     reruns, and proves the full-coverage limit of the formula:
     CRAP(m) == comp, so exactly ONE OC007 finding remains -- the
     comp-35 function (threshold 30) -- "fully covered is not
     invisible".
  8. Supply-tier honesty (corpus B) -- manifests_scanned == 0 (no C++
     manifest reader exists in v1; no fabricated pkg-config/vcpkg/conan
     tier), OC010 == 0, OC009 == 0 even WITH a vendored OSV snapshot
     loaded (no C++ include ecosystem exists and none is invented --
     the snapshot loads and the meta reports it, but zero manifests
     means zero rows), and the resolver edge set is UNCHANGED.
  9. The C-pack regression leg (the SPRINT-20 freeze honored): with the
     cpp pack REGISTERED, both SPRINT-20 corpora scan UNCHANGED --
     fixtures/c keeps the contracted counts (OC001 1, OC002 1, OC004 1,
     OC006 11, OC008 2, OC011 1, analyzed 13) with BOTH .h files routed
     by the root default to C (default_c == 2, sniffed == 0), and
     fixtures/c_resolver keeps its nine v1-c edges, OC003 == 1 and
     OC006 == 5. The cpp registration moved nothing the C pack owns.

Usage:
  python3 scripts/acceptance/lang_cpp_gate.py --oc build/default/oc \
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
import time

EXPECTED_RULES = {"OC001": 1, "OC002": 1, "OC004": 1, "OC006": 12, "OC008": 2, "OC011": 1}
EXPECTED_OC006 = sorted([
    "app_test.cpp", "cfg/secret.hpp", "decls.cpp", "dup_a.cpp", "dup_b.cpp", "engine.cpp",
    "include_user.cpp", "longfn.cpp", "mathx.cpp", "rollup.cpp", "todo.cpp", "types.h",
])
EXPECTED_ROUTING = {"sniffed_cpp": 1, "sniffed_c": 0, "default_cpp": 1, "default_c": 0}
INPUTLESS_RULES = ("OC007", "OC009", "OC010")
SILENT_ON_A = ("OC003", "OC005")
FIXTURE_FILES = 14

# Corpus B: the nine contracted imports-edge endpoint pairs
# (fixture-relative suffix match).
RESOLVER_EDGES = [
    ("cyc/a.hpp", "cyc/b.hpp"),
    ("cyc/b.hpp", "cyc/a.hpp"),
    ("cyc/cycle_driver.cpp", "cyc/a.hpp"),
    ("cyc/cycle_driver.cpp", "cyc/b.hpp"),
    ("include/outer.hpp", "include/sub/inner.hpp"),
    ("src/main.cpp", "src/util/log.hpp"),
    ("src/main.cpp", "third/p.hpp"),
    ("src/util/log.cpp", "src/util/log.hpp"),
    ("third/p.cpp", "third/p.hpp"),
]
RESOLVER_FILES = 10
OC006_FILES_B = ("cyc/cycle_driver.cpp", "include/outer.hpp", "src/main.cpp", "src/util/log.cpp",
                 "third/p.cpp")

# The SPRINT-20 freeze (criterion 9): the C corpora keep their contracted
# numbers on the sprint-21 tree.
C_EXPECTED_RULES = {"OC001": 1, "OC002": 1, "OC004": 1, "OC006": 11, "OC008": 2, "OC011": 1}
C_ROUTING = {"sniffed_cpp": 0, "sniffed_c": 0, "default_cpp": 0, "default_c": 2}


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


def scan_counts(report, fixtures):
    findings = report.get("findings", [])
    meta = report.get("meta", {})
    counts = collections.Counter(f.get("rule", "") for f in findings)
    oc006_files = [os.path.relpath(f.get("file", ""), fixtures)
                   for f in findings if f.get("rule") == "OC006"]
    return meta, counts, oc006_files


def check_corpus_a(oc, fixtures, packs, tmp, runs, wrap):
    """Structural corpus: criteria 1-5."""
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
    meta, counts, oc006_files = scan_counts(report, fixtures)
    analyzed = meta.get("files_analyzed", 0)
    parse_errs = meta.get("files_with_parse_errors", 1)
    routing = meta.get("header_routing")

    unseen = set()
    for f in report.get("findings", []):
        if f.get("rule") in INPUTLESS_RULES or f.get("rule") in SILENT_ON_A:
            unseen.add(f.get("rule"))

    crit1 = analyzed == FIXTURE_FILES and parse_errs == 0 \
        and "cpp" in (meta.get("packs") or [])
    crit3 = (dict(counts) == EXPECTED_RULES and not unseen
             and sorted(oc006_files) == EXPECTED_OC006)
    crit5 = routing == EXPECTED_ROUTING

    graph = run_json(oc, ["graph", fixtures, "--packs-dir", packs], f"{tmp}/a_graph.json")
    path_by_id = {n.get("id"): n.get("path", "") for n in graph.get("nodes", [])
                  if n.get("kind") == "file"}
    edge_pairs = []
    for e in graph.get("edges", []):
        if e.get("kind") != "imports":
            continue
        fp = path_by_id.get(e.get("from"), "")
        tp = path_by_id.get(e.get("to"), "")
        edge_pairs.append((os.path.relpath(fp, fixtures), os.path.relpath(tp, fixtures)))
    file_nodes = [n for n in graph.get("nodes", []) if n.get("kind") == "file"]
    # both resolved edges point INTO the analyzed set (local_util.h); the
    # system / macro / missing include channels produced none
    crit4 = (len(edge_pairs) == 2
             and all(tp.endswith("local_util.h") for _, tp in edge_pairs)
             and len(file_nodes) == FIXTURE_FILES)

    crits = {
        "1_parse_clean_pack_loaded": {"pass": crit1, "files_analyzed": analyzed,
                                      "files_with_parse_errors": parse_errs,
                                      "packs": meta.get("packs")},
        "2_byte_identical_reports": {"pass": stable, "runs": runs,
                                     "distinct_sha256": len(set(hashes))},
        "3_enabled_rules_contracted_counts": {"pass": crit3, "counts": dict(counts),
                                              "forbidden_seen": sorted(unseen),
                                              "oc006_files": sorted(oc006_files)},
        "4_resolved_edges_stay_in_the_analyzed_set": {
            "pass": crit4,
            "import_edges": len(edge_pairs),
            "edge_pairs": [f"{a} -> {b}" for a, b in sorted(edge_pairs)],
            "file_nodes": len(file_nodes)},
        "5_header_routing_block": {"pass": crit5, "routing": routing,
                                   "expected": EXPECTED_ROUTING},
    }
    return crits, walls, {"analyzed": analyzed, "parse_errs": parse_errs,
                          "packs": meta.get("packs"), "counts": dict(counts),
                          "import_edges": len(edge_pairs), "routing": routing}


def check_corpus_b(oc, fixtures, packs, tmp, runs, wrap):
    """Resolver corpus: criterion 6 (exact v1-cpp semantics)."""
    hashes = []
    for i in range(runs):
        out = f"{tmp}/b_run{i}.json"
        subprocess.run(wrap + [oc, "check", fixtures, "--packs-dir", packs, "--format", "json"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
        hashes.append(sha256_file(out))
    stable = len(set(hashes)) == 1

    report = run_json(oc, ["check", fixtures, "--packs-dir", packs, "--format", "json"],
                      f"{tmp}/b_run0.json")
    meta, counts, oc006_files = scan_counts(report, fixtures)
    analyzed = meta.get("files_analyzed", 0)
    parse_errs = meta.get("files_with_parse_errors", 1)
    oc003_msgs = [f.get("message", "") for f in report.get("findings", [])
                  if f.get("rule") == "OC003"]

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

    wanted = sorted(RESOLVER_EDGES)
    got = sorted(set(edge_pairs))
    edges_exact = len(edge_pairs) == len(RESOLVER_EDGES) and got == wanted

    crit6 = (
        analyzed == RESOLVER_FILES
        and parse_errs == 0
        and "cpp" in (meta.get("packs") or [])
        and stable
        and edges_exact
        and counts.get("OC003") == 1
        and all("Import cycle among 2 files" in m for m in oc003_msgs)
        and counts.get("OC005", 0) == 0  # empty by construction
        and counts.get("OC006") == 5
        and sorted(oc006_files) == sorted(OC006_FILES_B)
        and counts.get("OC010", 0) == 0  # no manifest tier: nothing fabricated
        and set(counts) <= {"OC003", "OC005", "OC006"}
    )
    crits = {
        "6_resolver_v1_cpp_exact": {
            "pass": crit6,
            "files_analyzed": analyzed,
            "files_with_parse_errors": parse_errs,
            "byte_identical_runs": runs,
            "imports_edges": len(edge_pairs),
            "edge_pairs": [f"{a} -> {b}" for a, b in got],
            "decoys_unresolved": (
                not any("vector" in b for _, b in got)
                and not any("HEADER" in b for _, b in got)
                and not any(b.endswith("missing.hpp") for _, b in got)
                and "src/main.cpp -> src/util/log.hpp" in [f"{a} -> {b}" for a, b in got]),
            "counts": dict(counts),
            "oc006_files": sorted(oc006_files),
            "oc003_message": oc003_msgs[0] if oc003_msgs else None,
        },
    }
    return crits, {"analyzed": analyzed, "parse_errs": parse_errs, "counts": dict(counts),
                   "edge_pairs": got}


def check_cobertura_composition(oc, fixtures_a, packs, tmp, wrap):
    """Criterion 7 (SPRINT-21): a composed gcovr-shaped Cobertura XML
    that covers every line measures every function, stays byte-identical
    and stays quiet except the comp-35 function (CRAP(m) == comp at 100
    percent)."""
    profile = f"{tmp}/cover/gcovr.xml"
    os.makedirs(os.path.dirname(profile), exist_ok=True)
    files = []
    for root, _, fns in os.walk(fixtures_a):
        for fn in sorted(fns):
            if fn.endswith((".cpp", ".cc", ".h", ".hpp")):
                full = os.path.join(root, fn)
                n_lines = sum(1 for _ in open(full, "rb"))
                files.append((os.path.relpath(full, fixtures_a), n_lines))
    files.sort()
    parts = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<coverage branch-rate="0.0" line-rate="1.0" lines-covered="0" '
             'lines-valid="0" timestamp="1764100000" version="gcovr 7.2">',
             '  <sources>', '    <source>.</source>', '  </sources>', '  <packages>',
             '    <package name="corpus">']
    for rel, n in files:
        parts.append(f'      <class name="cpp_{rel.replace("/", "_")}" filename="{rel}" '
                     'line-rate="1.0" branch-rate="0.0" complexity="0.0">')
        parts.append('        <methods/>')
        parts.append('        <lines>')
        for ln in range(1, n + 1):
            parts.append(f'          <line number="{ln}" hits="2" branch="False"/>')
        parts.append('        </lines>')
        parts.append('      </class>')
    parts.append('    </package>')
    parts.append('  </packages>')
    parts.append('</coverage>')
    with open(profile, "w") as fh:
        fh.write("\n".join(parts) + "\n")

    outs = []
    for i in range(2):
        out = f"{tmp}/cov_run{i}.json"
        subprocess.run(wrap + [oc, "check", fixtures_a, "--packs-dir", packs,
                               "--coverage", profile, "--format", "json"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
        outs.append(out)
    with open(outs[0]) as fh:
        report = json.load(fh)
    meta = report.get("meta", {})
    covmeta = meta.get("coverage", {})
    counts = collections.Counter()
    oc007 = []
    for f in report.get("findings", []):
        counts[f.get("rule", "")] += 1
        if f.get("rule") == "OC007":
            oc007.append(f.get("symbol", ""))
    hs = [hashlib.sha256(open(o, "rb").read()).hexdigest() for o in outs]
    crit7 = (
        covmeta.get("functions_total", 0) > 0
        and covmeta.get("functions_measured", 0) == covmeta.get("functions_total", -1)
        and covmeta.get("files_matched", 0) == FIXTURE_FILES
        and len(set(hs)) == 1
        and counts.get("OC007", 0) == 1  # CRAP(m) == comp at 100 percent: only the
                                         # comp-35 function stays above 30
        and oc007 == ["sum26"]
        and counts.get("OC004", 0) == EXPECTED_RULES["OC004"]
    )
    return {"pass": crit7, "functions_total": covmeta.get("functions_total"),
            "functions_measured": covmeta.get("functions_measured"),
            "files_matched": covmeta.get("files_matched"),
            "distinct_sha256": len(set(hs)), "counts": dict(counts), "oc007_symbols": oc007}


def check_supply_tier_honesty(oc, fixtures_b, packs, tmp, wrap, repo):
    """Criterion 8 (SPRINT-21): the honest supply tier -- no C++ manifest
    reader exists in v1, nothing is discovered (manifests_scanned == 0),
    OC010 == 0, OC009 == 0 even WITH a vendored OSV snapshot loaded (no
    C++ include ecosystem exists and none is invented; the snapshot
    loads and the meta reports it, but zero manifests means zero rows),
    and the resolver edge set is unchanged."""
    out = f"{tmp}/supply_run.json"
    subprocess.run(wrap + [oc, "check", fixtures_b, "--packs-dir", packs, "--format", "json"],
                   stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
    with open(out) as fh:
        report = json.load(fh)
    supply = report.get("meta", {}).get("supply")  # ABSENT with zero manifests
    counts = collections.Counter(f.get("rule", "") for f in report.get("findings", []))
    base_ok = (
        supply is None  # the honest zero-state: the meta block does not exist
        and counts.get("OC010", 0) == 0
        and counts.get("OC009", 0) == 0  # no snapshot, no manifests: never guessed
        and counts.get("OC007", 0) == 0  # no profile passed
    )

    # the snapshot leg: even WITH a loaded OSV snapshot, a C++ tree has
    # no ecosystem and no manifests -- zero rows, the meta reports the load
    snap = os.path.join(repo, "tests", "fixtures_sprint19", "osv_snapshot_php.json")
    out2 = f"{tmp}/supply_osv_run.json"
    subprocess.run(wrap + [oc, "check", fixtures_b, "--packs-dir", packs, "--format", "json",
                           "--osv-snapshot", snap],
                   stdout=open(out2, "wb"), stderr=subprocess.DEVNULL)
    with open(out2) as fh:
        report2 = json.load(fh)
    counts2 = collections.Counter(f.get("rule", "") for f in report2.get("findings", []))
    osv_ok = (
        report2.get("meta", {}).get("supply", {}).get("osv_snapshot_date") == "2026-11-22"
        and report2.get("meta", {}).get("supply", {}).get("manifests_scanned") == 0
        and counts2.get("OC009", 0) == 0  # zero manifests -> zero rows, honestly
        and counts2.get("OC010", 0) == 0
    )

    graph = run_json(oc, ["graph", fixtures_b, "--packs-dir", packs], f"{tmp}/s_graph.json")
    n_edges = sum(1 for e in graph.get("edges", []) if e.get("kind") == "imports")
    edges_unchanged = n_edges == len(RESOLVER_EDGES)
    return {"pass": base_ok and osv_ok and edges_unchanged,
            "supply_block_present": supply is not None,
            "osv_supply_block": report2.get("meta", {}).get("supply"),
            "base_counts": dict(counts), "osv_counts": dict(counts2), "edges": n_edges}


def check_c_pack_regression(oc, fixtures_c, fixtures_cres, packs, tmp, wrap, c_resolver_edges):
    """Criterion 9 (SPRINT-21): the SPRINT-20 freeze honored -- with the
    cpp pack registered, the C corpora scan UNCHANGED and both .h files
    of fixtures/c route by the root default to C."""
    out = f"{tmp}/creg_run.json"
    subprocess.run(wrap + [oc, "check", fixtures_c, "--packs-dir", packs, "--format", "json"],
                   stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
    with open(out) as fh:
        report = json.load(fh)
    meta, counts, _ = scan_counts(report, fixtures_c)
    routing = meta.get("header_routing")
    c_ok = (meta.get("files_analyzed") == 13
            and meta.get("files_with_parse_errors") == 0
            and dict(counts) == C_EXPECTED_RULES
            and routing == C_ROUTING)

    graph = run_json(oc, ["graph", fixtures_cres, "--packs-dir", packs], f"{tmp}/creg_graph.json")
    path_by_id = {n.get("id"): n.get("path", "") for n in graph.get("nodes", [])
                  if n.get("kind") == "file"}
    edge_pairs = []
    for e in graph.get("edges", []):
        if e.get("kind") != "imports":
            continue
        fp = path_by_id.get(e.get("from"), "")
        tp = path_by_id.get(e.get("to"), "")
        edge_pairs.append((os.path.relpath(fp, fixtures_cres), os.path.relpath(tp, fixtures_cres)))
    wanted = sorted(c_resolver_edges)
    got = sorted(set(edge_pairs))
    cres_ok = (len(edge_pairs) == len(c_resolver_edges) and got == wanted)
    return {"pass": c_ok and cres_ok, "c_counts": dict(counts), "c_routing": routing,
            "c_resolver_edges": len(edge_pairs),
            "c_resolver_exact": cres_ok}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--fixtures-a", default=None,
                    help="structural corpus (default: <repo>/tests/fixtures/cpp)")
    ap.add_argument("--fixtures-b", default=None,
                    help="resolver corpus (default: <repo>/tests/fixtures/cpp_resolver)")
    ap.add_argument("--packs", default=None, help="packs dir (default: the repo packs/)")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    fixtures_a = os.path.abspath(args.fixtures_a or os.path.join(repo, "tests", "fixtures", "cpp"))
    fixtures_b = os.path.abspath(args.fixtures_b or
                                 os.path.join(repo, "tests", "fixtures", "cpp_resolver"))
    fixtures_c = os.path.abspath(os.path.join(repo, "tests", "fixtures", "c"))
    fixtures_cres = os.path.abspath(os.path.join(repo, "tests", "fixtures", "c_resolver"))
    packs = os.path.abspath(args.packs or os.path.join(repo, "packs"))
    for p in (oc, fixtures_a, fixtures_b):
        if not os.path.exists(p):
            sys.exit(f"missing: {p}")

    tmp = "/tmp/omnicheck_lang_cpp_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # the SPRINT-20 contracted C resolver edge set (criterion 9 uses it)
    c_resolver_edges = [
        ("cyc/a.h", "cyc/b.h"),
        ("cyc/b.h", "cyc/a.h"),
        ("cyc/cycle_driver.c", "cyc/a.h"),
        ("cyc/cycle_driver.c", "cyc/b.h"),
        ("include/outer.h", "include/sub/inner.h"),
        ("src/main.c", "src/util/log.h"),
        ("src/main.c", "third/p.h"),
        ("src/util/log.c", "src/util/log.h"),
        ("third/p.c", "third/p.h"),
    ]

    crits_a, walls, info_a = check_corpus_a(oc, fixtures_a, packs, tmp, args.runs, wrap)
    crits_b, info_b = check_corpus_b(oc, fixtures_b, packs, tmp, args.runs, wrap)
    crits_cov = {"7_cobertura_composition":
                 check_cobertura_composition(oc, fixtures_a, packs, tmp, wrap)}
    crits_sup = {"8_supply_tier_honesty":
                 check_supply_tier_honesty(oc, fixtures_b, packs, tmp, wrap, repo)}
    crits_reg = {"9_c_pack_regression_freeze":
                 check_c_pack_regression(oc, fixtures_c, fixtures_cres, packs, tmp, wrap,
                                         c_resolver_edges)}

    print(f"corpus A: {fixtures_a}")
    print(f"corpus B: {fixtures_b}")
    print(f"criterion 1 (parse clean, cpp pack loaded): analyzed={info_a['analyzed']} "
          f"parse_errors={info_a['parse_errs']} -> "
          f"{'PASS' if crits_a['1_parse_clean_pack_loaded']['pass'] else 'FAIL'}")
    print(f"criterion 2 (byte-identical, {args.runs} runs): -> "
          f"{'PASS' if crits_a['2_byte_identical_reports']['pass'] else 'FAIL'}")
    print(f"criterion 3 (enabled rules, contracted counts): counts={info_a['counts']} -> "
          f"{'PASS' if crits_a['3_enabled_rules_contracted_counts']['pass'] else 'FAIL'}")
    print(f"criterion 4 (resolved edges stay in the analyzed set): "
          f"import_edges={info_a['import_edges']} -> "
          f"{'PASS' if crits_a['4_resolved_edges_stay_in_the_analyzed_set']['pass'] else 'FAIL'}")
    print(f"criterion 5 (.h routing block: {info_a['routing']}) -> "
          f"{'PASS' if crits_a['5_header_routing_block']['pass'] else 'FAIL'}")
    b = crits_b["6_resolver_v1_cpp_exact"]
    print(f"criterion 6 (resolver v1-cpp exact: 9 edges, decoys unresolved, OC003=1, "
          f"OC005 empty by construction, OC006=5, OC010=0): edges={b['imports_edges']} "
          f"counts={b['counts']} -> {'PASS' if b['pass'] else 'FAIL'}")
    c7 = crits_cov["7_cobertura_composition"]
    print(f"criterion 7 (gcovr Cobertura composition: measured={c7['functions_measured']}/"
          f"{c7['functions_total']}, matched={c7['files_matched']}, "
          f"OC007={c7['counts'].get('OC007', 0)} on {c7['oc007_symbols']} = CRAP==comp) -> "
          f"{'PASS' if c7['pass'] else 'FAIL'}")
    c8 = crits_sup["8_supply_tier_honesty"]
    print(f"criterion 8 (supply-tier honesty: supply block absent={not c8['supply_block_present']}, "
          f"OC010={c8['base_counts'].get('OC010', 0)}, OC009 with snapshot="
          f"{c8['osv_counts'].get('OC009', 0)}, "
          f"osv manifests_scanned={c8['osv_supply_block'].get('manifests_scanned')}, "
          f"edges unchanged={c8['edges']} -> "
          f"{'PASS' if c8['pass'] else 'FAIL'}")
    c9 = crits_reg["9_c_pack_regression_freeze"]
    print(f"criterion 9 (C-pack freeze: counts={c9['c_counts']}, routing={c9['c_routing']}, "
          f"resolver edges={c9['c_resolver_edges']} exact={c9['c_resolver_exact']}) -> "
          f"{'PASS' if c9['pass'] else 'FAIL'}")
    print(f"supporting: corpus A best wall {min(walls):.2f}s, worst {max(walls):.2f}s "
          f"(single-core: {bool(wrap)})")

    all_crits = {}
    all_crits.update(crits_a)
    all_crits.update(crits_b)
    all_crits.update(crits_cov)
    all_crits.update(crits_sup)
    all_crits.update(crits_reg)
    all_pass = all(c["pass"] for c in all_crits.values())

    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "C++ language adoption (SPRINT-21, full scope: resolver v1-cpp + the .h "
                "routing treatment + gcovr Cobertura + the honest empty supply tier + the "
                "C-pack freeze leg)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "fixtures_structural": fixtures_a,
        "fixtures_resolver": fixtures_b,
        "contract": "docs/LANGUAGES.md (C++ section)",
        "grammar_pin": "tree-sitter/tree-sitter-cpp v0.23.4",
        "criteria": all_crits,
        "wall_best_s": round(min(walls), 2),
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "lang_cpp_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
