#!/usr/bin/env python3
"""Rust language acceptance gate (SPRINT-15, the m2/m3/lang pattern
applied to the hand-written Rust fixture corpora).

Locks the contracted posture of docs/LANGUAGES.md (Rust) against TWO
pinned fixture corpora:

Corpus A -- tests/fixtures/rust (structural, 5 .rs files):
  1. Parse clean, the rust pack loaded (the kill-switch criterion).
  2. Byte-identical reports across N consecutive runs.
  3. Exactly the enabled structural rules fire with contracted counts:
     OC001 (the 69-line function), OC002 (the TODO), OC004 (complexity
     35 -- the hand-counted decision inventory, one decision per
     recorded kind), OC006 (4 non-entry files; lib.rs is claimed by the
     pack glob), OC008 (one group, 2 locations), OC011 (the credential
     literal, exactly once). OC003/OC005 (enabled with the resolver)
     and OC007/OC009/OC010 (inputless) never fire here.
  4. Honesty invariants: every import in this corpus names std or an
     external crate, so `oc graph` shows ZERO imports edges -- an edge
     always means "the target is part of this analysis".

Corpus B -- tests/fixtures/rust_resolver (resolver v1-rust, 11 files
+ the decoy manifest):
  5. Resolver semantics, exact: TWELVE imports edges with the
     contracted endpoint pairs (the mod-tree channel: lib.rs's five
     mod declarations + util.rs's child; the use channel: crate::/
     super::/self:: leaves with the item-reading pop ladder; the a/b
     use cycle; the set collapse of mod+use on lib->a). The decoy
     Cargo.toml is never read BY THE RESOLVER (it declares the crate
     name "mycorp" and tests/intg.rs imports mycorp::... -- the
     external head stays unresolved). src/bin/tool.rs's crate::
     resolves ONLY inside src/bin/ (the own-crate exception). External
     heads (serde, std, tokio) stay unresolved the same way.
     OC003 fires exactly once (the a/b cycle). OC005 fires exactly
     four times, on the four honestly-unused exports (isolated,
     store_root, fmt_name, fmt_inner) -- mod declarations mark
     nothing and the binding marking is exact. OC006 fires exactly
     once, on orphan.rs (tests/intg.rs is runner data;
     src/bin/tool.rs is claimed by the fn main parse fact).

SPRINT-15 closeout (criteria 6-7):
  6. LCOV composition (corpus A) -- a runtime-composed LCOV profile
     covering every line measures EVERY function (functions_measured ==
     functions_total), keeps byte-identical reruns, and proves the
     full-coverage limit of the formula: CRAP(m) == comp, so exactly
     ONE OC007 finding remains -- the comp-35 function (threshold 30) --
     "fully covered is not invisible".
  7. Manifest tier (corpus B) -- the supply tier parses exactly one
     manifest (the decoy Cargo.toml), OC010 fires exactly once anchored
     on it (no licence file at the root), OC009 stays silent without a
     snapshot and fires exactly once WITH the vendored crates.io
     snapshot (the unpinned `serde = "1.0"` requirement -- Cargo's own
     semantics, never guessed), and the resolver edge set is UNCHANGED.

Usage:
  python3 scripts/acceptance/lang_rust_gate.py --oc build/default/oc \
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

EXPECTED_RULES = {"OC001": 1, "OC002": 1, "OC004": 1, "OC006": 4, "OC008": 2, "OC011": 1}
INPUTLESS_RULES = ("OC007", "OC009", "OC010")
SILENT_ON_A = ("OC003", "OC005")
FIXTURE_FILES = 5

# Corpus B: the twelve contracted imports-edge endpoint pairs (fixture-
# relative suffix match), the resolver verdicts and the entry rules.
RESOLVER_EDGES = [
    ("src/a.rs", "src/b.rs"),
    ("src/b.rs", "src/a.rs"),
    ("src/lib.rs", "src/a.rs"),
    ("src/lib.rs", "src/b.rs"),
    ("src/lib.rs", "src/lonely.rs"),
    ("src/lib.rs", "src/store/mod.rs"),
    ("src/lib.rs", "src/store/types.rs"),
    ("src/lib.rs", "src/util.rs"),
    ("src/store/mod.rs", "src/a.rs"),
    ("src/store/mod.rs", "src/store/types.rs"),
    ("src/util.rs", "src/b.rs"),
    ("src/util.rs", "src/util/helper.rs"),
]
RESOLVER_FILES = 11
OC005_EXPORTS = ("isolated", "store_root", "fmt_name", "fmt_inner")


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
    oc006_lib = False
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule in INPUTLESS_RULES or rule in SILENT_ON_A:
            unseen.add(rule)
        if rule == "OC006" and f.get("file", "").endswith("lib.rs"):
            oc006_lib = True
    crit1 = analyzed == FIXTURE_FILES and parse_errs == 0 \
        and "rust" in (meta.get("packs") or [])
    crit3 = counts == EXPECTED_RULES and not unseen and not oc006_lib

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
                                              "oc006_on_lib_rs": oc006_lib},
        "4_unresolved_outside_the_analyzed_set": {"pass": crit4,
                                                  "import_edges": len(import_edges),
                                                  "file_nodes": len(file_nodes)},
    }
    return crits, walls, {"analyzed": analyzed, "parse_errs": parse_errs,
                          "packs": meta.get("packs"), "counts": counts,
                          "import_edges": len(import_edges)}


def check_corpus_b(oc, fixtures, packs, tmp, runs, wrap):
    """Resolver corpus: criterion 5 (exact v1-rust semantics)."""
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
    oc003_msgs, oc005_syms, oc006_files, oc010_files = [], [], [], []
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule == "OC003":
            oc003_msgs.append(f.get("message", ""))
        if rule == "OC005":
            oc005_syms.append(f.get("symbol", ""))
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

    wanted = sorted(RESOLVER_EDGES)
    got = sorted(set(edge_pairs))
    edges_exact = len(edge_pairs) == len(RESOLVER_EDGES) and got == wanted

    crit5 = (
        analyzed == RESOLVER_FILES
        and parse_errs == 0
        and "rust" in (meta.get("packs") or [])
        and stable
        and edges_exact
        and counts.get("OC003") == 1
        and all("Import cycle among 2 files" in m for m in oc003_msgs)
        and counts.get("OC005", 0) == 4
        and sorted(oc005_syms) == sorted(OC005_EXPORTS)
        and counts.get("OC006") == 1
        and oc006_files == ["orphan.rs"]
        and counts.get("OC010", 0) == 1  # the decoy Cargo.toml: no licence at the root
        and set(counts) <= {"OC003", "OC005", "OC006", "OC010"}
    )
    crits = {
        "5_resolver_v1_rust_exact": {
            "pass": crit5,
            "files_analyzed": analyzed,
            "files_with_parse_errors": parse_errs,
            "byte_identical_runs": runs,
            "imports_edges": len(edge_pairs),
            "edge_pairs": [f"{a} -> {b}" for a, b in got],
            "decoy_unresolved": ("src/lib.rs -> src/store/types.rs" in
                                 [f"{a} -> {b}" for a, b in got]) and
                                not any(b.endswith("intg.rs") for _, b in got),
            "bin_own_crate": not any("src/bin/" in b for _, b in got),
            "counts": counts,
            "oc005_exports": sorted(oc005_syms),
            "oc006_files": oc006_files,
            "oc010_files": oc010_files,
            "oc003_message": oc003_msgs[0] if oc003_msgs else None,
        },
    }
    return crits, {"analyzed": analyzed, "parse_errs": parse_errs, "counts": counts,
                   "edge_pairs": got}


def check_lcov_composition(oc, fixtures_a, packs, tmp, wrap):
    """Criterion 6 (SPRINT-15): a composed LCOV profile that covers every
    line measures every function, stays byte-identical and stays quiet
    except the comp-35 function (CRAP(m) == comp at 100 percent)."""
    profile = f"{tmp}/cover.lcov"
    blocks = []
    for root, _, files in os.walk(fixtures_a):
        for fn in sorted(files):
            if fn.endswith(".rs"):
                full = os.path.join(root, fn)
                n_lines = sum(1 for _ in open(full, "rb"))
                blocks.append((os.path.abspath(full), n_lines))
    with open(profile, "w") as fh:
        for path, nl in sorted(blocks):
            fh.write(f"TN:gate\nSF:{path}\n")
            for line in range(1, nl + 1):
                fh.write(f"DA:{line},2\n")
            fh.write("LF:%d\nLH:%d\nend_of_record\n" % (nl, nl))

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
        and oc007 == ["rust_long_function"]
        and counts.get("OC004", 0) == EXPECTED_RULES["OC004"]
    )
    return {"pass": crit6, "functions_total": covmeta.get("functions_total"),
            "functions_measured": covmeta.get("functions_measured"),
            "files_matched": covmeta.get("files_matched"),
            "distinct_sha256": len(set(hs)), "counts": counts, "oc007_symbols": oc007}


def check_manifest_tier(oc, fixtures_b, packs, tmp, wrap, repo):
    """Criterion 7 (SPRINT-15): the supply tier reads exactly one manifest
    (the decoy Cargo.toml), OC010 anchors on it once, OC009 stays silent
    without a snapshot and warns exactly once WITH the vendored crates.io
    snapshot (the unpinned requirement), and the resolver edge set is
    unchanged."""
    out = f"{tmp}/manifest_run.json"
    subprocess.run(wrap + [oc, "check", fixtures_b, "--packs-dir", packs, "--format", "json"],
                   stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
    with open(out) as fh:
        report = json.load(fh)
    supply = report.get("meta", {}).get("supply", {})
    counts = {}
    oc010_files = []
    for f in report.get("findings", []):
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule == "OC010":
            oc010_files.append(os.path.basename(f.get("file", "")))
    base_ok = (
        supply.get("manifests_scanned", -1) == 1
        and counts.get("OC010", 0) == 1
        and oc010_files == ["Cargo.toml"]
        and counts.get("OC009", 0) == 0  # no snapshot: never guessed, meta says so
        and counts.get("OC007", 0) == 0  # no profile passed
    )

    snap = os.path.join(repo, "tests", "fixtures", "supply", "osv_snapshot_rust.json")
    out2 = f"{tmp}/manifest_osv_run.json"
    subprocess.run(wrap + [oc, "check", fixtures_b, "--packs-dir", packs, "--format", "json",
                           "--osv-snapshot", snap],
                   stdout=open(out2, "wb"), stderr=subprocess.DEVNULL)
    with open(out2) as fh:
        report2 = json.load(fh)
    counts2 = {}
    oc009_files = []
    oc009_syms = []
    for f in report2.get("findings", []):
        rule = f.get("rule", "")
        counts2[rule] = counts2.get(rule, 0) + 1
        if rule == "OC009":
            oc009_files.append(os.path.basename(f.get("file", "")))
            oc009_syms.append(f.get("symbol", ""))
    osv_ok = (
        report2.get("meta", {}).get("supply", {}).get("osv_snapshot_date") == "2026-11-21"
        and counts2.get("OC009", 0) == 1  # serde = "1.0" unpinned: warn (Cargo semantics)
        and oc009_files == ["Cargo.toml"]
        and oc009_syms == ["serde"]
        and counts2.get("OC010", 0) == 1  # unchanged by the snapshot
    )

    graph = run_json(oc, ["graph", fixtures_b, "--packs-dir", packs], f"{tmp}/m_graph.json")
    n_edges = sum(1 for e in graph.get("edges", []) if e.get("kind") == "imports")
    edges_unchanged = n_edges == len(RESOLVER_EDGES)
    return {"pass": base_ok and osv_ok and edges_unchanged,
            "manifests_scanned": supply.get("manifests_scanned"),
            "base_counts": counts, "osv_counts": counts2,
            "oc009_files": oc009_files, "edges": n_edges}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--fixtures-a", default=None,
                    help="structural corpus (default: <repo>/tests/fixtures/rust)")
    ap.add_argument("--fixtures-b", default=None,
                    help="resolver corpus (default: <repo>/tests/fixtures/rust_resolver)")
    ap.add_argument("--packs", default=None, help="packs dir (default: the repo packs/)")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    fixtures_a = os.path.abspath(args.fixtures_a or os.path.join(repo, "tests", "fixtures", "rust"))
    fixtures_b = os.path.abspath(args.fixtures_b or
                                 os.path.join(repo, "tests", "fixtures", "rust_resolver"))
    packs = os.path.abspath(args.packs or os.path.join(repo, "packs"))
    for p in (oc, fixtures_a, fixtures_b):
        if not os.path.exists(p):
            sys.exit(f"missing: {p}")

    tmp = "/tmp/omnicheck_lang_rust_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    crits_a, walls, info_a = check_corpus_a(oc, fixtures_a, packs, tmp, args.runs, wrap)
    crits_b, info_b = check_corpus_b(oc, fixtures_b, packs, tmp, args.runs, wrap)
    crits_cov = {"6_lcov_composition": check_lcov_composition(oc, fixtures_a, packs, tmp, wrap)}
    crits_man = {"7_manifest_tier": check_manifest_tier(oc, fixtures_b, packs, tmp, wrap, repo)}

    print(f"corpus A: {fixtures_a}")
    print(f"corpus B: {fixtures_b}")
    print(f"criterion 1 (parse clean, rust pack loaded): analyzed={info_a['analyzed']} "
          f"parse_errors={info_a['parse_errs']} -> "
          f"{'PASS' if crits_a['1_parse_clean_pack_loaded']['pass'] else 'FAIL'}")
    print(f"criterion 2 (byte-identical, {args.runs} runs): -> "
          f"{'PASS' if crits_a['2_byte_identical_reports']['pass'] else 'FAIL'}")
    print(f"criterion 3 (enabled rules, contracted counts): counts={info_a['counts']} -> "
          f"{'PASS' if crits_a['3_enabled_rules_contracted_counts']['pass'] else 'FAIL'}")
    print(f"criterion 4 (outside-the-set stays unresolved): "
          f"import_edges={info_a['import_edges']} -> "
          f"{'PASS' if crits_a['4_unresolved_outside_the_analyzed_set']['pass'] else 'FAIL'}")
    b = crits_b["5_resolver_v1_rust_exact"]
    print(f"criterion 5 (resolver v1-rust exact: 12 edges, decoy ignored, OC003=1, "
          f"OC005=4 exact exports, OC006=orphan): edges={b['imports_edges']} "
          f"counts={b['counts']} -> {'PASS' if b['pass'] else 'FAIL'}")
    c6 = crits_cov["6_lcov_composition"]
    print(f"criterion 6 (LCOV composition: measured={c6['functions_measured']}/"
          f"{c6['functions_total']}, matched={c6['files_matched']}, "
          f"OC007={c6['counts'].get('OC007', 0)} on {c6['oc007_symbols']} = CRAP==comp) -> "
          f"{'PASS' if c6['pass'] else 'FAIL'}")
    c7 = crits_man["7_manifest_tier"]
    print(f"criterion 7 (manifest tier: manifests={c7['manifests_scanned']}, "
          f"OC010={c7['base_counts'].get('OC010', 0)}, OC009 with snapshot="
          f"{c7['osv_counts'].get('OC009', 0)} on {c7['oc009_files']}, "
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
        "gate": "Rust language adoption (SPRINT-15, full scope: resolver v1-rust + "
                "LCOV + Cargo manifests)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "fixtures_structural": fixtures_a,
        "fixtures_resolver": fixtures_b,
        "contract": "docs/LANGUAGES.md (Rust section)",
        "grammar_pin": "tree-sitter/tree-sitter-rust v0.24.0",
        "criteria": all_crits,
        "wall_best_s": round(min(walls), 2),
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "lang_rust_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
