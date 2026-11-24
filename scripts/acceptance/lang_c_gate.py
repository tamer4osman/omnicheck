#!/usr/bin/env python3
r"""C language acceptance gate (SPRINT-20, the m2/m3/lang pattern
applied to the hand-written C fixture corpora).

Locks the contracted posture of docs/LANGUAGES.md (C) against TWO
pinned fixture corpora:

Corpus A -- tests/fixtures/c (structural, 13 .c/.h files):
  1. Parse clean, the c pack loaded (the kill-switch criterion).
  2. Byte-identical reports across N consecutive runs.
  3. Exactly the enabled structural rules fire with contracted counts:
     OC001 (the 63-line function), OC002 (the TODO), OC004 (complexity
     35 -- the hand-counted decision inventory; the default arm and
     bare else count 0; preprocessor conditionals are not decisions),
     OC006 (11 non-entry files; main_app.c is claimed by the main
     PARSE FACT -- no name glob exists for 'defines main'),
     OC008 (one group, 2 locations), OC011 (the AWS-docs example
     credential, exactly once). OC003/OC005 (enabled with the
     resolver) and OC007/OC009/OC010 (inputless) never fire here.
     The OC006 set is checked EXACTLY (the eleven files), and
     local_util.h stays out (included twice).
  4. Honesty invariants: exactly TWO imports edges, both into the
     analyzed local_util.h -- the quoted include resolves (an edge
     always means "the target is part of this analysis"); the system
     include, the macro path and the missing-header include produce
     NONE.

Corpus B -- tests/fixtures/c_resolver (resolver v1-c, 10 files):
  5. Resolver semantics, exact: NINE imports edges with the contracted
     endpoint pairs (quoted includes relative to the INCLUDING file's
     directory: same-dir, nested-include anchoring at the header's own
     directory, the lexical .. pop, the driver edges into both cycle
     headers). The decoys stay unresolved: <stdio.h> (the system
     channel never resolves), HEADER_PATH (a macro path is not a
     static payload), missing.h (outside the analyzed set). OC003
     fires exactly once (the a.h/b.h include cycle). OC005's verdict
     set is EMPTY by construction (the OC005 precondition: only files
     that are themselves imported are judged; every imported header's
     exports are marked used by the whole-file include binds). OC006
     fires exactly five times (main.c, log.c, p.c, outer.h -- nothing
     includes it -- and cycle_driver.c). OC010 fires ZERO times (the
     honest supply tier: C has no canonical manifest, nothing is
     discovered, nothing is fabricated).

SPRINT-20 closeout (criteria 6-7):
  6. Cobertura composition (corpus A) -- a runtime-composed gcovr-
     shaped Cobertura XML covering every line measures EVERY function
     (functions_measured == functions_total), keeps byte-identical
     reruns, and proves the full-coverage limit of the formula:
     CRAP(m) == comp, so exactly ONE OC007 finding remains -- the
     comp-35 function (threshold 30) -- "fully covered is not
     invisible".
  7. Supply-tier honesty (corpus B) -- manifests_scanned == 0 (no C
     manifest reader exists; no fabricated pkg-config/vendored tier),
     OC010 == 0, OC009 == 0 even WITH a vendored OSV snapshot loaded
     (no C ecosystem exists and none is invented -- the snapshot loads
     and the meta reports it, but zero manifests means zero rows), and
     the resolver edge set is UNCHANGED.

Usage:
  python3 scripts/acceptance/lang_c_gate.py --oc build/default/oc \
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

EXPECTED_RULES = {"OC001": 1, "OC002": 1, "OC004": 1, "OC006": 11, "OC008": 2, "OC011": 1}
EXPECTED_OC006 = sorted([
    "app_test.c", "cfg/secret.h", "decls.c", "dup_a.c", "dup_b.c", "include_user.c",
    "longfn.c", "mathx.c", "rollup.c", "todo.c", "types.c",
])
INPUTLESS_RULES = ("OC007", "OC009", "OC010")
SILENT_ON_A = ("OC003", "OC005")
FIXTURE_FILES = 13

# Corpus B: the nine contracted imports-edge endpoint pairs
# (fixture-relative suffix match).
RESOLVER_EDGES = [
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
RESOLVER_FILES = 10
OC006_FILES_B = ("cycle_driver.c", "log.c", "main.c", "outer.h", "p.c")


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
    """Structural corpus: criteria 1-4."""
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

    counts = collections.Counter()
    unseen = set()
    oc006_files = []
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] += 1
        if rule in INPUTLESS_RULES or rule in SILENT_ON_A:
            unseen.add(rule)
        if rule == "OC006":
            oc006_files.append(os.path.relpath(f.get("file", ""), fixtures))
    crit1 = analyzed == FIXTURE_FILES and parse_errs == 0 \
        and "c" in (meta.get("packs") or [])
    crit3 = (dict(counts) == EXPECTED_RULES and not unseen
             and sorted(oc006_files) == EXPECTED_OC006)

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
    }
    return crits, walls, {"analyzed": analyzed, "parse_errs": parse_errs,
                          "packs": meta.get("packs"), "counts": dict(counts),
                          "import_edges": len(edge_pairs)}


def check_corpus_b(oc, fixtures, packs, tmp, runs, wrap):
    """Resolver corpus: criterion 5 (exact v1-c semantics)."""
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

    counts = collections.Counter()
    oc003_msgs, oc006_files = [], []
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] += 1
        if rule == "OC003":
            oc003_msgs.append(f.get("message", ""))
        if rule == "OC006":
            oc006_files.append(os.path.basename(f.get("file", "")))

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

    crit5 = (
        analyzed == RESOLVER_FILES
        and parse_errs == 0
        and "c" in (meta.get("packs") or [])
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
        "5_resolver_v1_c_exact": {
            "pass": crit5,
            "files_analyzed": analyzed,
            "files_with_parse_errors": parse_errs,
            "byte_identical_runs": runs,
            "imports_edges": len(edge_pairs),
            "edge_pairs": [f"{a} -> {b}" for a, b in got],
            "decoys_unresolved": (
                not any("stdio" in b for _, b in got)
                and not any("HEADER" in b for _, b in got)
                and not any(b.endswith("missing.h") for _, b in got)
                and "src/main.c -> src/util/log.h" in [f"{a} -> {b}" for a, b in got]),
            "counts": dict(counts),
            "oc006_files": sorted(oc006_files),
            "oc003_message": oc003_msgs[0] if oc003_msgs else None,
        },
    }
    return crits, {"analyzed": analyzed, "parse_errs": parse_errs, "counts": dict(counts),
                   "edge_pairs": got}


def check_cobertura_composition(oc, fixtures_a, packs, tmp, wrap):
    """Criterion 6 (SPRINT-20): a composed gcovr-shaped Cobertura XML
    that covers every line measures every function, stays byte-identical
    and stays quiet except the comp-35 function (CRAP(m) == comp at 100
    percent)."""
    profile = f"{tmp}/cover/gcovr.xml"
    os.makedirs(os.path.dirname(profile), exist_ok=True)
    files = []
    for root, _, fns in os.walk(fixtures_a):
        for fn in sorted(fns):
            if fn.endswith((".c", ".h")):
                full = os.path.join(root, fn)
                n_lines = sum(1 for _ in open(full, "rb"))
                files.append((os.path.relpath(full, fixtures_a), n_lines))
    files.sort()
    parts = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<coverage branch-rate="0.0" line-rate="1.0" lines-covered="0" '
             'lines-valid="0" timestamp="1764000000" version="gcovr 7.2">',
             '  <sources>', '    <source>.</source>', '  </sources>', '  <packages>',
             '    <package name="corpus">']
    for rel, n in files:
        parts.append(f'      <class name="c_{rel.replace("/", "_")}" filename="{rel}" '
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
    crit6 = (
        covmeta.get("functions_total", 0) > 0
        and covmeta.get("functions_measured", 0) == covmeta.get("functions_total", -1)
        and covmeta.get("files_matched", 0) == FIXTURE_FILES
        and len(set(hs)) == 1
        and counts.get("OC007", 0) == 1  # CRAP(m) == comp at 100 percent: only the
                                         # comp-35 function stays above 30
        and oc007 == ["sum26"]
        and counts.get("OC004", 0) == EXPECTED_RULES["OC004"]
    )
    return {"pass": crit6, "functions_total": covmeta.get("functions_total"),
            "functions_measured": covmeta.get("functions_measured"),
            "files_matched": covmeta.get("files_matched"),
            "distinct_sha256": len(set(hs)), "counts": dict(counts), "oc007_symbols": oc007}


def check_supply_tier_honesty(oc, fixtures_b, packs, tmp, wrap, repo):
    """Criterion 7 (SPRINT-20): the honest supply tier -- no C manifest
    reader exists, nothing is discovered (manifests_scanned == 0), OC010
    == 0, OC009 == 0 even WITH a vendored OSV snapshot loaded (no C
    ecosystem exists and none is invented; the snapshot loads and the
    meta reports it, but zero manifests means zero rows), and the
    resolver edge set is unchanged."""
    out = f"{tmp}/supply_run.json"
    subprocess.run(wrap + [oc, "check", fixtures_b, "--packs-dir", packs, "--format", "json"],
                   stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
    with open(out) as fh:
        report = json.load(fh)
    supply = report.get("meta", {}).get("supply")  # ABSENT with zero manifests:
    counts = collections.Counter(f.get("rule", "") for f in report.get("findings", []))
    base_ok = (
        supply is None  # the honest zero-state: the meta block does not exist
        and counts.get("OC010", 0) == 0
        and counts.get("OC009", 0) == 0  # no snapshot, no manifests: never guessed
        and counts.get("OC007", 0) == 0  # no profile passed
    )

    # the snapshot leg: even WITH a loaded OSV snapshot, a C tree has no
    # ecosystem and no manifests -- zero rows, the meta reports the load
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--fixtures-a", default=None,
                    help="structural corpus (default: <repo>/tests/fixtures/c)")
    ap.add_argument("--fixtures-b", default=None,
                    help="resolver corpus (default: <repo>/tests/fixtures/c_resolver)")
    ap.add_argument("--packs", default=None, help="packs dir (default: the repo packs/)")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    fixtures_a = os.path.abspath(args.fixtures_a or os.path.join(repo, "tests", "fixtures", "c"))
    fixtures_b = os.path.abspath(args.fixtures_b or
                                 os.path.join(repo, "tests", "fixtures", "c_resolver"))
    packs = os.path.abspath(args.packs or os.path.join(repo, "packs"))
    for p in (oc, fixtures_a, fixtures_b):
        if not os.path.exists(p):
            sys.exit(f"missing: {p}")

    tmp = "/tmp/omnicheck_lang_c_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    crits_a, walls, info_a = check_corpus_a(oc, fixtures_a, packs, tmp, args.runs, wrap)
    crits_b, info_b = check_corpus_b(oc, fixtures_b, packs, tmp, args.runs, wrap)
    crits_cov = {"6_cobertura_composition":
                 check_cobertura_composition(oc, fixtures_a, packs, tmp, wrap)}
    crits_sup = {"7_supply_tier_honesty":
                 check_supply_tier_honesty(oc, fixtures_b, packs, tmp, wrap, repo)}

    print(f"corpus A: {fixtures_a}")
    print(f"corpus B: {fixtures_b}")
    print(f"criterion 1 (parse clean, c pack loaded): analyzed={info_a['analyzed']} "
          f"parse_errors={info_a['parse_errs']} -> "
          f"{'PASS' if crits_a['1_parse_clean_pack_loaded']['pass'] else 'FAIL'}")
    print(f"criterion 2 (byte-identical, {args.runs} runs): -> "
          f"{'PASS' if crits_a['2_byte_identical_reports']['pass'] else 'FAIL'}")
    print(f"criterion 3 (enabled rules, contracted counts): counts={info_a['counts']} -> "
          f"{'PASS' if crits_a['3_enabled_rules_contracted_counts']['pass'] else 'FAIL'}")
    print(f"criterion 4 (resolved edges stay in the analyzed set): "
          f"import_edges={info_a['import_edges']} -> "
          f"{'PASS' if crits_a['4_resolved_edges_stay_in_the_analyzed_set']['pass'] else 'FAIL'}")
    b = crits_b["5_resolver_v1_c_exact"]
    print(f"criterion 5 (resolver v1-c exact: 9 edges, decoys unresolved, OC003=1, "
          f"OC005 empty by construction, OC006=5, OC010=0): edges={b['imports_edges']} "
          f"counts={b['counts']} -> {'PASS' if b['pass'] else 'FAIL'}")
    c6 = crits_cov["6_cobertura_composition"]
    print(f"criterion 6 (gcovr Cobertura composition: measured={c6['functions_measured']}/"
          f"{c6['functions_total']}, matched={c6['files_matched']}, "
          f"OC007={c6['counts'].get('OC007', 0)} on {c6['oc007_symbols']} = CRAP==comp) -> "
          f"{'PASS' if c6['pass'] else 'FAIL'}")
    c7 = crits_sup["7_supply_tier_honesty"]
    print(f"criterion 7 (supply-tier honesty: supply block absent={not c7['supply_block_present']}, "
          f"OC010={c7['base_counts'].get('OC010', 0)}, OC009 with snapshot="
          f"{c7['osv_counts'].get('OC009', 0)}, "
          f"osv manifests_scanned={c7['osv_supply_block'].get('manifests_scanned')}, "
          f"edges unchanged={c7['edges']} -> "
          f"{'PASS' if c7['pass'] else 'FAIL'}")
    print(f"supporting: corpus A best wall {min(walls):.2f}s, worst {max(walls):.2f}s "
          f"(single-core: {bool(wrap)})")

    all_crits = {}
    all_crits.update(crits_a)
    all_crits.update(crits_b)
    all_crits.update(crits_cov)
    all_crits.update(crits_sup)
    all_pass = all(c["pass"] for c in all_crits.values())

    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "C language adoption (SPRINT-20, full scope: resolver v1-c + gcovr "
                "Cobertura + the honest empty supply tier)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "fixtures_structural": fixtures_a,
        "fixtures_resolver": fixtures_b,
        "contract": "docs/LANGUAGES.md (C section)",
        "grammar_pin": "tree-sitter/tree-sitter-c v0.23.4",
        "criteria": all_crits,
        "wall_best_s": round(min(walls), 2),
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "lang_c_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
