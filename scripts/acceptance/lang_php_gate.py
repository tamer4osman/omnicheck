#!/usr/bin/env python3
r"""PHP language acceptance gate (SPRINT-19, the m2/m3/lang pattern
applied to the hand-written PHP fixture corpora).

Locks the contracted posture of docs/LANGUAGES.md (PHP) against TWO
pinned fixture corpora:

Corpus A -- tests/fixtures/php (structural, 9 .php files):
  1. Parse clean, the php pack loaded (the kill-switch criterion).
  2. Byte-identical reports across N consecutive runs.
  3. Exactly the enabled structural rules fire with contracted counts:
     OC001 (the 63-line function), OC002 (the TODO), OC004 (complexity
     35 -- the hand-counted decision inventory, one decision per
     recorded kind; the default_statement counts 0 and ?? counts 0),
     OC006 (8 non-entry files; bin/tool.php is claimed by the pack's
     **/bin/* glob -- NO shebang hint exists for PHP), OC008 (one group,
     2 locations), OC011 (the AWS-example credential, exactly once).
     OC003/OC005 (enabled with the resolver) and OC007/OC009/OC010
     (inputless) never fire here.
  4. Honesty invariants: nothing in this corpus names a file inside the
     analyzed set, so `oc graph` shows ZERO imports edges -- an edge
     always means "the target is part of this analysis".

Corpus B -- tests/fixtures/php_resolver (resolver v1-php, 15 .php files
+ composer.json/composer.lock + the decoy nested composer.json):
  5. Resolver semantics, exact: ELEVEN imports edges with the
     contracted endpoint pairs (PSR-4 through the ROOT composer.json:
     plain, grouped with alias, function use, the Store class; PSR-0
     keeps the full name path; require/include with lexical .. pops;
     the index.php -> loader require; the a/b include cycle). The
     decoys stay unresolved: Psr\..., Doctrine\..., the __DIR__
     concatenation, the interpolated include, Acme\Shop\Missing (a
     matching prefix whose class file is absent), and the nested
     composer.json's fake/ mapping (the resolver reads the composer.json
     of the ANALYSIS ROOT only). OC003 fires exactly once (the a/b
     cycle). OC005's verdict set is EMPTY by construction. OC006 fires
     exactly three times (lonely.php, orphan.php, uses_legacy.php).
     OC010 fires exactly twice (the root composer.json AND the decoy
     nested composer.json; the composer.lock is the OC009-only lockfile
     tier).

SPRINT-19 closeout (criteria 6-7):
  6. Clover composition (corpus A) -- a runtime-composed Clover XML
     covering every line measures EVERY function
     (functions_measured == functions_total), keeps byte-identical
     reruns, and proves the full-coverage limit of the formula:
     CRAP(m) == comp, so exactly ONE OC007 finding remains -- the
     comp-35 function (threshold 30) -- "fully covered is not invisible".
  7. Manifest tier (corpus B) -- the supply tier parses exactly THREE
     manifests (the root composer.json AND composer.lock AND the decoy
     nested composer.json), OC010 anchors on each composer.json once
     (composer.lock is the OC009-only lockfile tier), OC009 stays silent
     without a snapshot and fires exactly twice WITH the vendored
     packagist snapshot (the composer.json exact pin acme/log 1.2.3
     inside [0, 1.3.0) AND the composer.lock resolved pin acme/log
     1.2.3; the ^2.0 constraint range records nothing -- the npm
     discipline), and the resolver edge set is UNCHANGED.

Usage:
  python3 scripts/acceptance/lang_php_gate.py --oc build/default/oc \
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

EXPECTED_RULES = {"OC001": 1, "OC002": 1, "OC004": 1, "OC006": 8, "OC008": 2, "OC011": 1}
INPUTLESS_RULES = ("OC007", "OC009", "OC010")
SILENT_ON_A = ("OC003", "OC005")
FIXTURE_FILES = 9

# Corpus B: the eleven contracted imports-edge endpoint pairs
# (fixture-relative suffix match), the resolver verdicts and the entry
# rules.
RESOLVER_EDGES = [
    ("index.php", "src/loader.php"),
    ("src/Shop.php", "src/Contracts/Mapper.php"),
    ("src/Shop.php", "src/Order.php"),
    ("src/Shop.php", "src/Product.php"),
    ("src/Shop.php", "src/Store/Types.php"),
    ("src/Shop.php", "src/helpers/sum.php"),
    ("src/a.php", "src/b.php"),
    ("src/b.php", "src/a.php"),
    ("src/loader.php", "src/Contracts/Mapper.php"),
    ("src/loader.php", "src/Shop.php"),
    ("src/uses_legacy.php", "lib/Acme/Legacy/Legacy.php"),
]
RESOLVER_FILES = 15
OC006_FILES = ("lonely.php", "orphan.php", "uses_legacy.php")
OC010_FILES = ("composer.json", "composer.json")  # root + the decoy nested one


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

    counts = {}
    unseen = set()
    oc006_bin = False
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule in INPUTLESS_RULES or rule in SILENT_ON_A:
            unseen.add(rule)
        if rule == "OC006" and f.get("file", "").endswith("bin/tool.php"):
            oc006_bin = True
    crit1 = analyzed == FIXTURE_FILES and parse_errs == 0 \
        and "php" in (meta.get("packs") or [])
    crit3 = counts == EXPECTED_RULES and not unseen and not oc006_bin

    graph = run_json(oc, ["graph", fixtures, "--packs-dir", packs], f"{tmp}/a_graph.json")
    import_edges = [e for e in graph.get("edges", []) if e.get("kind") == "imports"]
    file_nodes = [n for n in graph.get("nodes", []) if n.get("kind") == "file"]
    crit4 = len(import_edges) == 0 and len(file_nodes) == FIXTURE_FILES

    crits = {
        "1_parse_clean_pack_loaded": {"pass": crit1, "files_analyzed": analyzed,
                                      "files_with_parse_errors": parse_errs,
                                      "packs": meta.get("packs")},
        "2_byte_identical_reports": {"pass": stable, "runs": runs,
                                     "distinct_sha256": len(set(hashes))},
        "3_enabled_rules_contracted_counts": {"pass": crit3, "counts": counts,
                                              "forbidden_seen": sorted(unseen),
                                              "oc006_on_bin_tool": oc006_bin},
        "4_unresolved_outside_the_analyzed_set": {"pass": crit4,
                                                  "import_edges": len(import_edges),
                                                  "file_nodes": len(file_nodes)},
    }
    return crits, walls, {"analyzed": analyzed, "parse_errs": parse_errs,
                          "packs": meta.get("packs"), "counts": counts,
                          "import_edges": len(import_edges)}


def check_corpus_b(oc, fixtures, packs, tmp, runs, wrap):
    """Resolver corpus: criterion 5 (exact v1-php semantics)."""
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
    oc003_msgs, oc006_files, oc010_paths = [], [], []
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule == "OC003":
            oc003_msgs.append(f.get("message", ""))
        if rule == "OC006":
            oc006_files.append(os.path.basename(f.get("file", "")))
        if rule == "OC010":
            oc010_paths.append(f.get("file", ""))

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
        and "php" in (meta.get("packs") or [])
        and stable
        and edges_exact
        and counts.get("OC003") == 1
        and all("Import cycle among 2 files" in m for m in oc003_msgs)
        and counts.get("OC005", 0) == 0  # empty by construction
        and counts.get("OC006") == 3
        and sorted(oc006_files) == sorted(OC006_FILES)
        and counts.get("OC010") == 2  # the root composer.json AND the decoy
        and len({os.path.dirname(p) for p in oc010_paths}) == 2
        and set(counts) <= {"OC003", "OC006", "OC010"}
    )
    crits = {
        "5_resolver_v1_php_exact": {
            "pass": crit5,
            "files_analyzed": analyzed,
            "files_with_parse_errors": parse_errs,
            "byte_identical_runs": runs,
            "imports_edges": len(edge_pairs),
            "edge_pairs": [f"{a} -> {b}" for a, b in got],
            "decoys_unresolved": (
                not any(b.endswith("fake/") or "/fake/" in b for _, b in got)
                and not any(b.endswith("Missing.php") or "Missing" in b for _, b in got)
                and not any("LoggerInterface" in b or "Doctrine" in b for _, b in got)
                and "src/Shop.php -> src/Contracts/Mapper.php" in
                [f"{a} -> {b}" for a, b in got]),
            "counts": counts,
            "oc006_files": sorted(oc006_files),
            "oc010_dirs": sorted({os.path.dirname(p) for p in oc010_paths}),
            "oc003_message": oc003_msgs[0] if oc003_msgs else None,
        },
    }
    return crits, {"analyzed": analyzed, "parse_errs": parse_errs, "counts": counts,
                   "edge_pairs": got}


def check_clover_composition(oc, fixtures_a, packs, tmp, wrap):
    """Criterion 6 (SPRINT-19): a composed Clover XML that covers every
    line measures every function, stays byte-identical and stays quiet
    except the comp-35 function (CRAP(m) == comp at 100 percent)."""
    profile = f"{tmp}/cover/clover.xml"
    os.makedirs(os.path.dirname(profile), exist_ok=True)
    files = []
    for root, _, fns in os.walk(fixtures_a):
        for fn in sorted(fns):
            if fn.endswith(".php"):
                full = os.path.join(root, fn)
                n_lines = sum(1 for _ in open(full, "rb"))
                files.append((os.path.relpath(full, fixtures_a), n_lines))
    files.sort()
    parts = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<coverage generated="1764000000" threshold="0">',
             ' <project timestamp="1764000000" name="GateSuite">']
    for rel, n in files:
        parts.append(f'  <file name="{rel}">')
        for ln in range(1, n + 1):
            parts.append(f'   <line num="{ln}" type="stmt" count="2"/>')
        parts.append('  </file>')
    parts.append(' </project>')
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
    counts = {}
    oc007 = []
    for f in report.get("findings", []):
        counts[f.get("rule", "")] = counts.get(f.get("rule", ""), 0) + 1
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
        and oc007 == ["rollup"]
        and counts.get("OC004", 0) == EXPECTED_RULES["OC004"]
    )
    return {"pass": crit6, "functions_total": covmeta.get("functions_total"),
            "functions_measured": covmeta.get("functions_measured"),
            "files_matched": covmeta.get("files_matched"),
            "distinct_sha256": len(set(hs)), "counts": counts, "oc007_symbols": oc007}


def check_manifest_tier(oc, fixtures_b, packs, tmp, wrap, repo):
    """Criterion 7 (SPRINT-19): the supply tier reads exactly THREE
    manifests (root composer.json + composer.lock + the decoy nested
    composer.json), OC010 anchors on each composer.json once
    (composer.lock is the OC009-only lockfile tier), OC009 stays silent
    without a snapshot and fires exactly twice WITH the vendored
    packagist snapshot (the composer.json exact pin acme/log 1.2.3
    inside [0, 1.3.0) and the composer.lock resolved pin acme/log 1.2.3;
    the ^2.0 range and the outside-snapshot pins are silent), and the
    resolver edge set is unchanged."""
    out = f"{tmp}/manifest_run.json"
    subprocess.run(wrap + [oc, "check", fixtures_b, "--packs-dir", packs, "--format", "json"],
                   stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
    with open(out) as fh:
        report = json.load(fh)
    supply = report.get("meta", {}).get("supply", {})
    counts = {}
    oc010_dirs = []
    for f in report.get("findings", []):
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule == "OC010":
            oc010_dirs.append(os.path.dirname(f.get("file", "")))
    base_ok = (
        supply.get("manifests_scanned", -1) == 3
        and counts.get("OC010", 0) == 2
        and len(set(oc010_dirs)) == 2  # the root AND the nested decoy dir
        and counts.get("OC009", 0) == 0  # no snapshot: never guessed, meta says so
        and counts.get("OC007", 0) == 0  # no profile passed
    )

    snap = os.path.join(repo, "tests", "fixtures_sprint19", "osv_snapshot_php.json")
    out2 = f"{tmp}/manifest_osv_run.json"
    subprocess.run(wrap + [oc, "check", fixtures_b, "--packs-dir", packs, "--format", "json",
                           "--osv-snapshot", snap],
                   stdout=open(out2, "wb"), stderr=subprocess.DEVNULL)
    with open(out2) as fh:
        report2 = json.load(fh)
    counts2 = {}
    oc009_rows = []
    for f in report2.get("findings", []):
        rule = f.get("rule", "")
        counts2[rule] = counts2.get(rule, 0) + 1
        if rule == "OC009":
            oc009_rows.append((os.path.basename(f.get("file", "")), f.get("symbol", ""),
                               f.get("severity", "")))
    osv_ok = (
        report2.get("meta", {}).get("supply", {}).get("osv_snapshot_date") == "2026-11-22"
        and counts2.get("OC009", 0) == 2
        and sorted(oc009_rows) == sorted([
            ("composer.json", "acme/log", "error"),
            ("composer.lock", "acme/log", "error"),
        ])
        and counts2.get("OC010", 0) == 2  # unchanged by the snapshot
        # acme/gear ^2.0 records nothing (the npm discipline); acme/gear
        # 2.4.0 in the lock is pinned outside the snapshot: silent;
        # acme/devtool 0.9.0 is pinned outside the snapshot: silent
        and not any(sym == "acme/gear" or sym == "acme/devtool" for _, sym, _ in oc009_rows)
    )

    graph = run_json(oc, ["graph", fixtures_b, "--packs-dir", packs], f"{tmp}/m_graph.json")
    n_edges = sum(1 for e in graph.get("edges", []) if e.get("kind") == "imports")
    edges_unchanged = n_edges == len(RESOLVER_EDGES)
    return {"pass": base_ok and osv_ok and edges_unchanged,
            "manifests_scanned": supply.get("manifests_scanned"),
            "base_counts": counts, "osv_counts": counts2,
            "oc009_rows": [f"{a}:{b}:{c}" for a, b, c in oc009_rows], "edges": n_edges}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--fixtures-a", default=None,
                    help="structural corpus (default: <repo>/tests/fixtures/php)")
    ap.add_argument("--fixtures-b", default=None,
                    help="resolver corpus (default: <repo>/tests/fixtures/php_resolver)")
    ap.add_argument("--packs", default=None, help="packs dir (default: the repo packs/)")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    fixtures_a = os.path.abspath(args.fixtures_a or os.path.join(repo, "tests", "fixtures", "php"))
    fixtures_b = os.path.abspath(args.fixtures_b or
                                 os.path.join(repo, "tests", "fixtures", "php_resolver"))
    packs = os.path.abspath(args.packs or os.path.join(repo, "packs"))
    for p in (oc, fixtures_a, fixtures_b):
        if not os.path.exists(p):
            sys.exit(f"missing: {p}")

    tmp = "/tmp/omnicheck_lang_php_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    crits_a, walls, info_a = check_corpus_a(oc, fixtures_a, packs, tmp, args.runs, wrap)
    crits_b, info_b = check_corpus_b(oc, fixtures_b, packs, tmp, args.runs, wrap)
    crits_cov = {"6_clover_composition": check_clover_composition(oc, fixtures_a, packs, tmp, wrap)}
    crits_man = {"7_manifest_tier": check_manifest_tier(oc, fixtures_b, packs, tmp, wrap, repo)}

    print(f"corpus A: {fixtures_a}")
    print(f"corpus B: {fixtures_b}")
    print(f"criterion 1 (parse clean, php pack loaded): analyzed={info_a['analyzed']} "
          f"parse_errors={info_a['parse_errs']} -> "
          f"{'PASS' if crits_a['1_parse_clean_pack_loaded']['pass'] else 'FAIL'}")
    print(f"criterion 2 (byte-identical, {args.runs} runs): -> "
          f"{'PASS' if crits_a['2_byte_identical_reports']['pass'] else 'FAIL'}")
    print(f"criterion 3 (enabled rules, contracted counts): counts={info_a['counts']} -> "
          f"{'PASS' if crits_a['3_enabled_rules_contracted_counts']['pass'] else 'FAIL'}")
    print(f"criterion 4 (outside-the-set stays unresolved): "
          f"import_edges={info_a['import_edges']} -> "
          f"{'PASS' if crits_a['4_unresolved_outside_the_analyzed_set']['pass'] else 'FAIL'}")
    b = crits_b["5_resolver_v1_php_exact"]
    print(f"criterion 5 (resolver v1-php exact: 11 edges, decoys ignored, OC003=1, "
          f"OC005 empty by construction, OC006=3, OC010=2): edges={b['imports_edges']} "
          f"counts={b['counts']} -> {'PASS' if b['pass'] else 'FAIL'}")
    c6 = crits_cov["6_clover_composition"]
    print(f"criterion 6 (Clover composition: measured={c6['functions_measured']}/"
          f"{c6['functions_total']}, matched={c6['files_matched']}, "
          f"OC007={c6['counts'].get('OC007', 0)} on {c6['oc007_symbols']} = CRAP==comp) -> "
          f"{'PASS' if c6['pass'] else 'FAIL'}")
    c7 = crits_man["7_manifest_tier"]
    print(f"criterion 7 (manifest tier: manifests={c7['manifests_scanned']}, "
          f"OC010={c7['base_counts'].get('OC010', 0)}, OC009 with snapshot="
          f"{c7['osv_counts'].get('OC009', 0)} on {c7['oc009_rows']}, "
          f"edges unchanged={c7['edges']} -> "
          f"{'PASS' if c7['pass'] else 'FAIL'}")
    print(f"supporting: corpus A best wall {min(walls):.2f}s, worst {max(walls):.2f}s "
          f"(single-core: {bool(wrap)})")

    all_crits = {}
    all_crits.update(crits_a)
    all_crits.update(crits_b)
    all_crits.update(crits_cov)
    all_crits.update(crits_man)
    all_pass = all(c["pass"] for c in all_crits.values())

    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "PHP language adoption (SPRINT-19, full scope: resolver v1-php + "
                "Clover + composer manifests)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "fixtures_structural": fixtures_a,
        "fixtures_resolver": fixtures_b,
        "contract": "docs/LANGUAGES.md (PHP section)",
        "grammar_pin": "tree-sitter/tree-sitter-php v0.24.2",
        "criteria": all_crits,
        "wall_best_s": round(min(walls), 2),
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "lang_php_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
