#!/usr/bin/env python3
"""C#-language acceptance gate (SPRINT-13 full-scope adoption).

Locks the contracted posture of docs/LANGUAGES.md (C#) against THREE
pinned fixture corpora -- the m2/m3 pattern applied to hand-written
language fixture sets:

Corpus A -- tests/fixtures/csharp (structural):
  1. Parse clean -- every fixture file analyzed, zero parse errors (the
     silent-partial-analysis kill-switch criterion carries forward).
  2. Byte-identical reports across N consecutive runs (full determinism
     for the pack, fingerprints stable).
  3. Exactly the enabled structural rules fire, with contracted counts
     (the Go-spike shape, C# edition): OC001 (64-line method), OC002
     (TODO), OC004 (complexity 26), OC006 (4 non-entry files -- Program
     .cs is claimed by the parsed static-Main fact), OC008 (one group,
     2 locations), OC011 (the AWS example literal, exactly once);
     OC003/OC005/OC007/OC009/OC010 never fire here (no intra-corpus
     usings, no coverage, no snapshot, no csproj).
  4. Honesty invariants -- every using in this corpus is external
     (System.*), so `oc graph` shows ZERO imports edges and the
     static-Main fact claims Program.cs (asserted by OC006's absence
     there).

Corpus B -- tests/fixtures/csharp_resolver (resolver v1-csharp):
  5. Resolver semantics, exact:
     - exactly ELEVEN imports edges with the contracted endpoint pairs:
       the namespace-claim fan-out (Program -> {Store, Types, StoreInfo,
       Inventory} -- the whole-namespace discipline, direct-children
       rule), the static claim (Program -> Store -- deduped into the
       set), the alias-type claim (Program -> Inventory -- deduped), the
       alias-NAMESPACE claim (Program -> store/deep/Nested -- the
       ordered probe's directory reading), the Client fan-out (Client ->
       {Store, Types, StoreInfo, Inventory} -- plain + global:: + static
       records collapse into the four distinct edges), and the A/B
       namespace cycle;
       System.Linq stays unresolved the same way as the decoy
       coordinate;
     - the decoy MyApp.App.csproj is never read BY THE RESOLVER: it
       declares Example.External.Lib which exists nowhere in the
       analyzed set (no project/MSBuild reading -- WBS 4.x precedent);
     - OC003 fires exactly once ("Import cycle among 2 files" -- C# has
       REAL compile-time-legal cycles);
     - OC005 fires ZERO times -- the namespace claims mark fail-open;
     - OC006 fires exactly twice (Client, Lonely -- zero in-degree, not
       entries);
     - OC010 fires ZERO times on the decoy csproj -- its
       PackageLicenseExpression IS detected (the positive pin for the
       csproj licence reader);
     - 10 .cs files analyzed, zero parse errors, byte-identical reruns.

SPRINT-13 closeout (criteria 6-7):
  6. Cobertura composition (corpus A) -- a runtime-generated Coverlet
     Cobertura XML report covering every line measures EVERY function of
     the corpus (functions_measured == functions_total), keeps
     byte-identical reruns, and stays quiet: CRAP(m) == comp, and the
     corpus's worst complexity is 26 < 30 -- "fully covered is not a
     hotspot" here (the csharp_corpus_gate carries the fuller story on
     real code).
  7. Manifest + coverage tier (csharp_manifest corpus) -- exactly TWO
     manifests (alpha with a licence expression, beta without); OC010
     fires exactly once (beta), anchored on beta/beta.csproj; with the
     vendored OSV snapshot OC009 fires exactly five times (2 pinned
     matches + 3 unpinned warns: a range, a ${property} reference and a
     floating version), all anchored at line 1 (the XML reader does not
     track lines -- never guessed); with a runtime-generated Cobertura
     report the covered alpha is quiet and the uncovered Grade
     (complexity 6, 0/13 anchors) yields exactly ONE OC007 at CRAP=42;
     functions_measured == 2; a malformed Cobertura record exits
     nonzero (fail loud).

Usage:
  python3 scripts/acceptance/lang_csharp_gate.py --oc build/default/oc \
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
INPUTLESS_RULES = ("OC003", "OC005", "OC007", "OC009", "OC010")
FIXTURE_FILES = 5

# Corpus B: the eleven contracted imports-edge endpoint pairs. The
# namespace claims fan out to every DIRECT-CHILD .cs file of the
# namespace directory (never the subdirectory); the static and
# alias-type records of the same targets collapse into the set; the
# alias-NAMESPACE claim reaches the subdirectory; the A/B cycle pins
# OC003's two-file SCC.
RESOLVER_EDGES = [
    ("MyApp/App/Program.cs", "MyApp/Store/Store.cs"),
    ("MyApp/App/Program.cs", "MyApp/Store/Types.cs"),
    ("MyApp/App/Program.cs", "MyApp/Store/StoreInfo.cs"),
    ("MyApp/App/Program.cs", "MyApp/Store/Inventory.cs"),
    ("MyApp/App/Program.cs", "MyApp/Store/Deep/Nested.cs"),
    ("MyApp/A/A.cs", "MyApp/B/B.cs"),
    ("MyApp/B/B.cs", "MyApp/A/A.cs"),
    ("MyApp/Client/Client.cs", "MyApp/Store/Store.cs"),
    ("MyApp/Client/Client.cs", "MyApp/Store/Types.cs"),
    ("MyApp/Client/Client.cs", "MyApp/Store/StoreInfo.cs"),
    ("MyApp/Client/Client.cs", "MyApp/Store/Inventory.cs"),
]
RESOLVER_FILES = 10


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
    """Structural corpus: criteria 1-4. Returns (crit dict, walls, extra)."""
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
    crit1 = analyzed == FIXTURE_FILES and parse_errs == 0 \
        and "csharp" in (meta.get("packs") or [])

    counts = {}
    unseen = set()
    oc006_program = False
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule in INPUTLESS_RULES:
            unseen.add(rule)
        if rule == "OC006" and f.get("file", "").endswith("Program.cs"):
            oc006_program = True
    crit3 = counts == EXPECTED_RULES and not unseen and not oc006_program

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
                                              "oc006_on_main_fact_file": oc006_program},
        "4_unresolved_outside_the_analyzed_set": {"pass": crit4,
                                                  "import_edges": len(import_edges),
                                                  "file_nodes": len(file_nodes)},
    }
    return crits, walls, {"analyzed": analyzed, "parse_errs": parse_errs,
                          "packs": meta.get("packs"), "counts": counts,
                          "import_edges": len(import_edges)}


def check_corpus_b(oc, fixtures, packs, tmp, runs, wrap):
    """Resolver corpus: criterion 5 (exact v1-csharp semantics)."""
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
    oc003_msgs, oc006_files = [], []
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule == "OC003":
            oc003_msgs.append(f.get("message", ""))
        if rule == "OC006":
            oc006_files.append(os.path.relpath(f.get("file", ""), fixtures))

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
    fanout_exact = len(edge_pairs) == len(RESOLVER_EDGES) and got == wanted

    crit5 = (
        analyzed == RESOLVER_FILES
        and parse_errs == 0
        and "csharp" in (meta.get("packs") or [])
        and stable
        and fanout_exact
        and counts.get("OC003") == 1
        and all("Import cycle among 2 files" in m for m in oc003_msgs)
        and counts.get("OC005", 0) == 0
        and counts.get("OC006") == 2
        and sorted(oc006_files) == sorted(["MyApp/Client/Client.cs", "MyApp/Lonely/Lonely.cs"])
        and counts.get("OC010", 0) == 0  # decoy csproj: its licence expression IS detected
        and set(counts) <= {"OC003", "OC006"}
    )
    crits = {
        "5_resolver_v1_csharp_exact": {
            "pass": crit5,
            "files_analyzed": analyzed,
            "files_with_parse_errors": parse_errs,
            "byte_identical_runs": runs,
            "imports_edges": len(edge_pairs),
            "edge_pairs": [f"{a} -> {b}" for a, b in got],
            "decoy_unresolved": ("Example.External.Lib",
                                 "no edge into an external/coordinate path"
                                 not in [f"{a} -> {b}" for a, b in got]),
            "counts": counts,
            "oc006_files": oc006_files,
            "oc003_message": oc003_msgs[0] if oc003_msgs else None,
        },
    }
    return crits, {"analyzed": analyzed, "parse_errs": parse_errs, "counts": counts,
                   "edge_pairs": got}


def check_cobertura(oc, fixtures_a, packs, tmp, wrap):
    """Criterion 6 (SPRINT-13): a Coverlet Cobertura XML report covering
    every line measures every function, stays byte-identical and stays
    quiet (CRAP(m) == comp <= 26 < 30 on this corpus)."""
    profile = f"{tmp}/cobertura.xml"
    blocks = []
    for root, _, files in os.walk(fixtures_a):
        for fn in sorted(files):
            if fn.endswith(".cs"):
                full = os.path.join(root, fn)
                n_lines = sum(1 for _ in open(full, "rb"))
                blocks.append((os.path.abspath(full), n_lines))
    with open(profile, "w") as fh:
        fh.write('<?xml version="1.0" encoding="utf-8"?>\n'
                 '<coverage line-rate="1.0" version="6.0.4">\n'
                 "  <packages><package name=\"gate\"><classes>\n")
        for path, nl in sorted(blocks):
            fh.write(f'    <class name="Gate" filename="{path}">\n      <lines>\n')
            for ln in range(1, nl + 1):
                fh.write(f'      <line number="{ln}" hits="3" branch="False" />\n')
            fh.write("      </lines>\n    </class>\n")
        fh.write("  </classes></package></packages>\n</coverage>\n")

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
    for f in report.get("findings", []):
        counts[f.get("rule", "")] = counts.get(f.get("rule", ""), 0) + 1
    hs = [hashlib.sha256(open(o, "rb").read()).hexdigest() for o in outs]
    crit6 = (
        covmeta.get("functions_total", 0) > 0
        and covmeta.get("functions_measured", 0) == covmeta.get("functions_total", -1)
        and covmeta.get("files_matched", 0) == FIXTURE_FILES
        and len(set(hs)) == 1
        and counts.get("OC007", 0) == 0  # CRAP(m) == comp <= 26: quiet
        and counts.get("OC004", 0) == EXPECTED_RULES["OC004"]
    )
    return {"pass": crit6, "functions_total": covmeta.get("functions_total"),
            "functions_measured": covmeta.get("functions_measured"),
            "files_matched": covmeta.get("files_matched"),
            "distinct_sha256": len(set(hs)), "counts": counts}


def check_manifest_tier(oc, repo, packs, tmp, wrap):
    """Criterion 7 (SPRINT-13): the csharp_manifest corpus -- csproj tier
    with and without the snapshot, plus the Cobertura composition."""
    fixtures = os.path.join(repo, "tests", "fixtures", "csharp_manifest")
    snap = os.path.join(repo, "tests", "fixtures", "supply", "osv_snapshot_csharp.json")
    out = f"{tmp}/manifest_plain.json"
    subprocess.run(wrap + [oc, "check", fixtures, "--packs-dir", packs, "--format", "json"],
                   stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
    with open(out) as fh:
        report = json.load(fh)
    supply = report.get("meta", {}).get("supply") or {}
    counts = {}
    oc010_files = []
    for f in report.get("findings", []):
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule == "OC010":
            oc010_files.append(os.path.relpath(f.get("file", ""), fixtures))
    plain_ok = (supply.get("manifests_scanned", -1) == 2
                and counts.get("OC010", 0) == 1
                and oc010_files == ["beta/beta.csproj"]
                and counts.get("OC009", 0) == 0  # no snapshot: never guessed
                and counts.get("OC007", 0) == 0  # no coverage passed
                and counts.get("OC006", 0) == 2)  # Grade + Lib (same-namespace
                # usage needs no using directive in C# -- the v1 modeling
                # boundary, honestly invisible to import-driven marking;
                # Program.cs is claimed by the top-level-statements fact)

    # with the snapshot: 2 pinned matches + 3 unpinned warns, line 1.
    out2 = f"{tmp}/manifest_snap.json"
    subprocess.run(wrap + [oc, "check", fixtures, "--packs-dir", packs,
                           "--osv-snapshot", snap, "--format", "json"],
                   stdout=open(out2, "wb"), stderr=subprocess.DEVNULL)
    with open(out2) as fh:
        report = json.load(fh)
    counts2 = {}
    oc009_line_ok = True
    for f in report.get("findings", []):
        rule = f.get("rule", "")
        counts2[rule] = counts2.get(rule, 0) + 1
        if rule == "OC009" and f.get("line", -1) != 1:
            oc009_line_ok = False  # the XML reader does not track lines
    snap_ok = (counts2.get("OC009", 0) == 5 and oc009_line_ok
               and counts2.get("OC010", 0) == 1)

    # with the generated Cobertura report: Grade uncovered -> CRAP 42.
    cob = f"{tmp}/cobertura_gen.xml"
    root_f = os.path.abspath(fixtures)
    with open(cob, "w") as fh:
        fh.write('<?xml version="1.0" encoding="utf-8"?>\n'
                 '<coverage line-rate="0.3" version="6.0.4">\n'
                 "  <packages><package name=\"gen\"><classes>\n")
        alpha = os.path.join(root_f, "alpha/Alpha")
        fh.write(f'    <class name="Alpha.Lib" filename="{alpha}/Lib.cs">\n      <lines>\n')
        for ln in range(1, 7):
            fh.write(f'      <line number="{ln}" hits="3" branch="False" />\n')
        fh.write("      </lines>\n    </class>\n")
        beta = os.path.join(root_f, "beta")
        fh.write(f'    <class name="Beta.Grade" filename="{beta}/Grade.cs">\n      <lines>\n')
        for ln in range(1, 14):
            fh.write(f'      <line number="{ln}" hits="0" branch="False" />\n')
        fh.write("      </lines>\n    </class>\n")
        fh.write("  </classes></package></packages>\n</coverage>\n")
    out3 = f"{tmp}/manifest_cov.json"
    subprocess.run(wrap + [oc, "check", fixtures, "--packs-dir", packs,
                           "--coverage", cob, "--format", "json"],
                   stdout=open(out3, "wb"), stderr=subprocess.DEVNULL)
    with open(out3) as fh:
        report = json.load(fh)
    covmeta = report.get("meta", {}).get("coverage") or {}
    counts3 = {}
    oc007_crap = ""
    for f in report.get("findings", []):
        rule = f.get("rule", "")
        counts3[rule] = counts3.get(rule, 0) + 1
        if rule == "OC007":
            oc007_crap = f.get("message", "")
    cov_ok = (covmeta.get("functions_measured", -1) == 2
              and covmeta.get("files_matched", -1) == 2
              and counts3.get("OC007", 0) == 1 and "CRAP=42" in oc007_crap
              and counts3.get("OC010", 0) == 1 and counts3.get("OC006", 0) == 2)

    # malformed Cobertura record fails loud
    bad = f"{tmp}/cobertura_bad.xml"
    with open(bad, "w") as fh:
        fh.write('<?xml version="1.0"?><coverage><packages><package name="p">'
                 '<classes><class name="X" filename="X.cs"><lines><line hits="1"/>'
                 "</lines></class></classes></package></coverage>")
    rc = subprocess.run(wrap + [oc, "check", fixtures, "--packs-dir", packs,
                                "--coverage", bad, "--format", "json"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fail_loud = rc.returncode != 0

    crit7 = plain_ok and snap_ok and cov_ok and fail_loud
    return {"pass": crit7, "manifests_scanned": supply.get("manifests_scanned"),
            "plain_counts": counts, "snapshot_counts": counts2, "coverage_counts": counts3,
            "fail_loud": fail_loud}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--fixtures-a", default=None,
                    help="structural corpus (default: <repo>/tests/fixtures/csharp)")
    ap.add_argument("--fixtures-b", default=None,
                    help="resolver corpus (default: <repo>/tests/fixtures/csharp_resolver)")
    ap.add_argument("--packs", default=None,
                    help="packs dir (default: the repo's own packs/ directory)")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    fixtures_a = os.path.abspath(args.fixtures_a or os.path.join(repo, "tests", "fixtures",
                                                                 "csharp"))
    fixtures_b = os.path.abspath(args.fixtures_b or
                                 os.path.join(repo, "tests", "fixtures", "csharp_resolver"))
    packs = os.path.abspath(args.packs or os.path.join(repo, "packs"))
    for p in (oc, fixtures_a, fixtures_b):
        if not os.path.exists(p):
            sys.exit(f"missing: {p}")

    tmp = "/tmp/omnicheck_lang_csharp_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    crits_a, walls, info_a = check_corpus_a(oc, fixtures_a, packs, tmp, args.runs, wrap)
    crits_b, info_b = check_corpus_b(oc, fixtures_b, packs, tmp, args.runs, wrap)
    crits_cov = {"6_cobertura_composition": check_cobertura(oc, fixtures_a, packs, tmp, wrap)}
    crits_man = {"7_manifest_coverage_tier": check_manifest_tier(oc, repo, packs, tmp, wrap)}

    print(f"corpus A: {fixtures_a}")
    print(f"corpus B: {fixtures_b}")
    print(f"criterion 1 (parse clean, csharp pack loaded): analyzed={info_a['analyzed']} "
          f"parse_errors={info_a['parse_errs']} -> "
          f"{'PASS' if crits_a['1_parse_clean_pack_loaded']['pass'] else 'FAIL'}")
    print(f"criterion 2 (byte-identical, {args.runs} runs): -> "
          f"{'PASS' if crits_a['2_byte_identical_reports']['pass'] else 'FAIL'}")
    print(f"criterion 3 (enabled rules, contracted counts): counts={info_a['counts']} -> "
          f"{'PASS' if crits_a['3_enabled_rules_contracted_counts']['pass'] else 'FAIL'}")
    print(f"criterion 4 (outside-the-set stays unresolved): "
          f"import_edges={info_a['import_edges']} -> "
          f"{'PASS' if crits_a['4_unresolved_outside_the_analyzed_set']['pass'] else 'FAIL'}")
    b = crits_b["5_resolver_v1_csharp_exact"]
    print(f"criterion 5 (resolver v1-csharp exact: 11 edges, decoy csproj ignored, OC003=1, "
          f"OC005=0, OC006=2, OC010=0): edges={b['imports_edges']} counts={b['counts']} -> "
          f"{'PASS' if b['pass'] else 'FAIL'}")
    c6 = crits_cov["6_cobertura_composition"]
    print(f"criterion 6 (Cobertura composition: measured={c6['functions_measured']}/"
          f"{c6['functions_total']}, matched={c6['files_matched']}, OC007=0) -> "
          f"{'PASS' if c6['pass'] else 'FAIL'}")
    c7 = crits_man["7_manifest_coverage_tier"]
    print(f"criterion 7 (csproj+Cobertura tier: manifests={c7['manifests_scanned']}, "
          f"OC010={c7['plain_counts'].get('OC010', 0)}, "
          f"OC009={c7['snapshot_counts'].get('OC009', 0)} (line 1), "
          f"OC007={c7['coverage_counts'].get('OC007', 0)} = CRAP==comp, "
          f"fail_loud={c7['fail_loud']}) -> {'PASS' if c7['pass'] else 'FAIL'}")
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
        "gate": "C# language adoption, full scope (SPRINT-13)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "fixtures_structural": fixtures_a,
        "fixtures_resolver": fixtures_b,
        "contract": "docs/LANGUAGES.md (C# section)",
        "grammar_pin": "tree-sitter-grammars/tree-sitter-c-sharp v0.24.0",
        "criteria": all_crits,
        "wall_best_s": round(min(walls), 2),
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "lang_csharp_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
