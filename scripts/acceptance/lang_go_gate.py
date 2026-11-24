#!/usr/bin/env python3
"""Go-language acceptance gate (SPRINT-9 spike + SPRINT-10 resolver v1-go
+ SPRINT-11 coverage/manifests closeout).

Locks the contracted posture of docs/LANGUAGES.md (Go) against TWO pinned
fixture corpora -- the m2/m3 pattern applied to hand-written language
fixture sets:

Corpus A -- tests/fixtures/go (structural, SPRINT-9):
  1. Parse clean -- every fixture file analyzed, zero parse errors (the
     silent-partial-analysis kill-switch criterion carries forward).
  2. Byte-identical reports across N consecutive runs (full determinism
     for the pack, fingerprints stable).
  3. Exactly the enabled structural rules fire, with contracted counts:
     OC001 (66-line function), OC002 (TODO), OC004 (complexity 25),
     OC006 (4 non-entry files), OC008 (one group, 2 locations), OC011
     (the AKIA example literal, exactly once); OC003/OC005 (enabled since
     SPRINT-10) and OC007/OC009/OC010 (enabled since SPRINT-11, but
     inputless here -- no profile, no snapshot, no go.mod) never fire.
  4. Honesty invariants -- no specifier in this corpus names a package
     directory inside the analyzed set (stdlib "fmt"/"strings", a module
     path whose directory does not exist), so `oc graph` shows ZERO
     imports edges and the entry rules (pack glob + package_clause)
     claim main.go.

Corpus B -- tests/fixtures/go_resolver (resolver v1-go, SPRINT-10):
  5. Resolver semantics, exact:
     - exactly SIX imports edges with the contracted endpoint pairs:
       the whole-package fan-out (main -> store/{store,types}; the
       package's own store_test.go is NOT a target), the aliased import
       (main -> util), the verbatim dotted-host edge (main ->
       example.org/app/store), and the a/b package cycle;
     - the decoy manifest is never read BY THE RESOLVER: go.mod declares
       module "mycorp" and cmd/main.go imports "mycorp/store" while
       store/ EXISTS -- it stays unresolved (module-prefix stripping
       would resolve it);
     - stdlib ("fmt", "testing") and remote ("github.com/x/remote")
       stay unresolved the same way;
     - OC003 fires exactly once ("Import cycle among 2 files");
     - OC005 fires ZERO times -- the asserted empty verdict set of the
       package-wide fail-open marking (docs/LANGUAGES.md §Go.5);
     - OC006 fires exactly once, on the lonely package: cmd/serve.go is
       claimed by the parsed package_clause (package main, not named
       main.go) and store_test.go by the *_test.go convention;
     - 9 .go files analyzed, zero parse errors, byte-identical reruns;
     - the ONLY other finding is OC010 on the decoy go.mod (SPRINT-11:
       the SUPPLY tier legitimately reads it as a manifest; the module
       root carries no licence file -- criterion 7 pins this).

SPRINT-11 closeout (criteria 6-7):
  6. Coverprofile composition (corpus A) -- a runtime-generated `go
     test -coverprofile` profile covering every block count>0 measures
     EVERY function of the corpus (functions_measured ==
     functions_total), keeps byte-identical reruns, and proves the
     full-coverage limit of the formula: CRAP(m) == comp, so exactly
     ONE OC007 finding remains -- the comp-35 function, CRAP=35
     (threshold 30) -- the "fully covered is not invisible" honesty.
  7. Manifest tier (corpus B) -- the supply tier parses exactly one
     manifest (the decoy go.mod), OC010 fires exactly once anchored on
     it (no licence file at the module root), OC009 stays silent
     without a snapshot, and the resolver edge set is UNCHANGED (the
     decoy is never read as a resolver input).

Usage:
  python3 scripts/acceptance/lang_go_gate.py --oc build/default/oc \
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
# Enabled since SPRINT-11 but INPUTLESS on corpus A (no coverage profile,
# no OSV snapshot, no go.mod): they must never fire there.
INPUTLESS_RULES = ("OC007", "OC009", "OC010")
SILENT_SINCE_S10 = ("OC003", "OC005")  # enabled, but empty verdicts on corpus A
FIXTURE_FILES = 5

# Corpus B: the six contracted imports-edge endpoint pairs (suffix match
# on the fixture-relative paths) and the OC006-eligible file. The
# package's own test file is NOT a target (Go compiles an import without
# test files), and the dotted-host edge pins the VERBATIM mapping
# (dots are literal directory names -- Go's own; a Python-style
# dot-to-slash conversion would resolve nothing).
RESOLVER_EDGES = [
    ("cmd/main.go", "store/store.go"),
    ("cmd/main.go", "store/types.go"),
    ("cmd/main.go", "util/util.go"),
    ("cmd/main.go", "example.org/app/store/store.go"),
    ("a/a.go", "b/b.go"),
    ("b/b.go", "a/a.go"),
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
        and "go" in (meta.get("packs") or [])

    counts = {}
    unseen = set()
    oc006_main = False
    for f in findings:
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule in INPUTLESS_RULES or rule in SILENT_SINCE_S10:
            unseen.add(rule)
        if rule == "OC006" and f.get("file", "").endswith("main.go"):
            oc006_main = True
    crit3 = counts == EXPECTED_RULES and not unseen and not oc006_main

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
                                              "oc006_on_main_go": oc006_main},
        "4_unresolved_outside_the_analyzed_set": {"pass": crit4,
                                                  "import_edges": len(import_edges),
                                                  "file_nodes": len(file_nodes)},
    }
    return crits, walls, {"analyzed": analyzed, "parse_errs": parse_errs,
                          "packs": meta.get("packs"), "counts": counts,
                          "import_edges": len(import_edges)}


def check_corpus_b(oc, fixtures, packs, tmp, runs, wrap):
    """Resolver corpus: criterion 5 (exact v1-go semantics)."""
    # Determinism first: byte-identical reports.
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
    fanout_exact = len(edge_pairs) == len(RESOLVER_EDGES) and got == wanted

    crit5 = (
        analyzed == RESOLVER_FILES
        and parse_errs == 0
        and "go" in (meta.get("packs") or [])
        and stable
        and fanout_exact
        and counts.get("OC003") == 1
        and all("Import cycle among 2 files" in m for m in oc003_msgs)
        and counts.get("OC005", 0) == 0
        and counts.get("OC006") == 1
        and oc006_files == ["lonely.go"]
        and counts.get("OC010", 0) == 1  # decoy go.mod: no licence at module root
        and set(counts) <= {"OC003", "OC006", "OC010"}
    )
    crits = {
        "5_resolver_v1_go_exact": {
            "pass": crit5,
            "files_analyzed": analyzed,
            "files_with_parse_errors": parse_errs,
            "byte_identical_runs": runs,
            "imports_edges": len(edge_pairs),
            "edge_pairs": [f"{a} -> {b}" for a, b in got],
            "decoy_unresolved": ("mycorp/store", "cmd/main.go -> store/store.go"
                                 not in [f"{a} -> {b}" for a, b in got]),
            "counts": counts,
            "oc006_files": oc006_files,
            "oc003_message": oc003_msgs[0] if oc003_msgs else None,
        },
    }
    return crits, {"analyzed": analyzed, "parse_errs": parse_errs, "counts": counts,
                   "edge_pairs": got}


def check_coverprofile(oc, fixtures_a, packs, tmp, wrap):
    """Criterion 6 (SPRINT-11): a `go test -coverprofile` profile that
    covers every block measures every function, stays byte-identical and
    stays quiet (CRAP(m) == comp at 100 percent coverage)."""
    profile = f"{tmp}/coverprofile.txt"
    # Absolute record paths: the fixtures are analyzed under their
    # absolute path, so the record path matches EXACTLY (the suffix rule
    # would also accept an import-path-prefixed record -- pinned by the
    # unit tests; here we pin the exact form). One block per fixture
    # file, spanning the whole file (count > 0).
    blocks = []
    for root, _, files in os.walk(fixtures_a):
        for fn in sorted(files):
            if fn.endswith(".go"):
                full = os.path.join(root, fn)
                n_lines = sum(1 for _ in open(full, "rb"))
                blocks.append((os.path.abspath(full), n_lines))
    with open(profile, "w") as fh:
        fh.write("mode: set\n")
        for path, nl in sorted(blocks):
            fh.write(f"{path}:1.1,{nl}.1 1 1\n")

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
        and counts.get("OC007", 0) == 1  # CRAP(m) == comp at 100 percent: only
                                         # the comp-35 function stays over 30
        and counts.get("OC004", 0) == EXPECTED_RULES["OC004"]
    )
    return {"pass": crit6, "functions_total": covmeta.get("functions_total"),
            "functions_measured": covmeta.get("functions_measured"),
            "files_matched": covmeta.get("files_matched"),
            "distinct_sha256": len(set(hs)), "counts": counts}


def check_manifest_tier(oc, fixtures_b, packs, tmp, wrap):
    """Criterion 7 (SPRINT-11): the supply tier reads exactly one manifest
    (the decoy go.mod), OC010 anchors on it once, OC009 stays silent
    without a snapshot -- and the resolver edge set is unchanged."""
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
    crit7 = (
        supply.get("manifests_scanned", -1) == 1
        and counts.get("OC010", 0) == 1
        and oc010_files == ["go.mod"]
        and counts.get("OC009", 0) == 0  # no snapshot: never guessed, meta says so
        and counts.get("OC007", 0) == 0  # no profile passed
    )
    return {"pass": crit7, "manifests_scanned": supply.get("manifests_scanned"),
            "counts": counts, "oc010_files": oc010_files}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--fixtures-a", default=None,
                    help="structural corpus (default: <repo>/tests/fixtures/go)")
    ap.add_argument("--fixtures-b", default=None,
                    help="resolver corpus (default: <repo>/tests/fixtures/go_resolver)")
    ap.add_argument("--packs", default=None,
                    help="packs dir (default: the repo's own packs/ directory)")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    fixtures_a = os.path.abspath(args.fixtures_a or os.path.join(repo, "tests", "fixtures", "go"))
    fixtures_b = os.path.abspath(args.fixtures_b or
                                 os.path.join(repo, "tests", "fixtures", "go_resolver"))
    packs = os.path.abspath(args.packs or os.path.join(repo, "packs"))
    for p in (oc, fixtures_a, fixtures_b):
        if not os.path.exists(p):
            sys.exit(f"missing: {p}")

    tmp = "/tmp/omnicheck_lang_go_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    crits_a, walls, info_a = check_corpus_a(oc, fixtures_a, packs, tmp, args.runs, wrap)
    crits_b, info_b = check_corpus_b(oc, fixtures_b, packs, tmp, args.runs, wrap)
    crits_cov = {"6_coverprofile_composition": check_coverprofile(oc, fixtures_a, packs, tmp,
                                                                   wrap)}
    crits_man = {"7_manifest_tier": check_manifest_tier(oc, fixtures_b, packs, tmp, wrap)}

    print(f"corpus A: {fixtures_a}")
    print(f"corpus B: {fixtures_b}")
    print(f"criterion 1 (parse clean, go pack loaded): analyzed={info_a['analyzed']} "
          f"parse_errors={info_a['parse_errs']} -> "
          f"{'PASS' if crits_a['1_parse_clean_pack_loaded']['pass'] else 'FAIL'}")
    print(f"criterion 2 (byte-identical, {args.runs} runs): -> "
          f"{'PASS' if crits_a['2_byte_identical_reports']['pass'] else 'FAIL'}")
    print(f"criterion 3 (enabled rules, contracted counts): counts={info_a['counts']} -> "
          f"{'PASS' if crits_a['3_enabled_rules_contracted_counts']['pass'] else 'FAIL'}")
    print(f"criterion 4 (outside-the-set stays unresolved): "
          f"import_edges={info_a['import_edges']} -> "
          f"{'PASS' if crits_a['4_unresolved_outside_the_analyzed_set']['pass'] else 'FAIL'}")
    b = crits_b["5_resolver_v1_go_exact"]
    print(f"criterion 5 (resolver v1-go exact: 6 edges, decoy ignored, OC003=1, "
          f"OC005=0, OC006=lonely): edges={b['imports_edges']} counts={b['counts']} -> "
          f"{'PASS' if b['pass'] else 'FAIL'}")
    c6 = crits_cov["6_coverprofile_composition"]
    print(f"criterion 6 (coverprofile composition: measured={c6['functions_measured']}/"
          f"{c6['functions_total']}, matched={c6['files_matched']}, "
          f"OC007={c6['counts'].get('OC007', 0)} = CRAP==comp) -> "
          f"{'PASS' if c6['pass'] else 'FAIL'}")
    c7 = crits_man["7_manifest_tier"]
    print(f"criterion 7 (manifest tier: manifests={c7['manifests_scanned']}, "
          f"OC010={c7['counts'].get('OC010', 0)} on {c7['oc010_files']}, OC009=0) -> "
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
        "gate": "Go language adoption + resolver v1-go + closeout "
                "(SPRINT-9 spike, SPRINT-10 resolver, SPRINT-11 coverage+manifests)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "fixtures_structural": fixtures_a,
        "fixtures_resolver": fixtures_b,
        "contract": "docs/LANGUAGES.md (Go section)",
        "grammar_pin": "tree-sitter-grammars/tree-sitter-go v0.25.0",
        "criteria": all_crits,
        "wall_best_s": round(min(walls), 2),
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "lang_go_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
