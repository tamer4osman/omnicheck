#!/usr/bin/env python3
r"""Kotlin reference-corpus gate (SPRINT-22, recipe step 8 -- the m2/m3
pattern applied to a REAL Kotlin corpus).

Pinned corpus: ktorio/ktor 3.0.1 (the GitHub tag tarball) -- a
real-world, actively maintained, production Kotlin framework (server,
client, http, io, websockets; the canonical multiplatform Kotlin
codebase). The tarball is pinned by sha256 and lives in work/;
extraction is verified by a corpus digest (sha256 over the sorted
extraction-relative path+content pairs). 1861 .kt + 129 .kts files are
the Kotlin pack's material; 23 vendored C-interop headers (.h) route to
the C pack by the approved root default (ktor carries no .cpp file) and
carry the C pack's own pinned boundary with them.

THE RAW-SOURCE BOUNDARY (the honest delta, the SPRINT-20/21 shape): a
grammar pinned at fwcd/tree-sitter-kotlin v0.3.8 (June 2024, generated
against Kotlin 2.0-era sources) cannot fully parse every Kotlin-2.x
idiom as-written. 119 of the 2015 analyzed files carry at least one
parse error (5.9 percent; classified at pin time, 1922 error nodes):
42 the function-type-with-receiver shape (`RequestConfig.Builder.() ->
Unit` -- the pinned grammar's largest gap), 21 Kotlin-1.9+ syntax
(expect/actual declarations, `fun interface`, the `..<` range-until
operator, delegation-by), 6 the suspend function-type shape, 19 the
vendored curl cinterop headers (the C pack's own pinned boundary), 5
missing-token-only files (zero error nodes; the automatic-semicolon
scanner), 1 the gradle-kts `the<T>` reified call, and 25 residual
(first-error-heuristic locations -- the abseil "residual" class). No
canonical Kotlin corpus parses 100 percent clean as-written with this
pin. THE CRITERION IS THEREFORE THE PIN, not zero: a grammar or tool
regression MOVES the pinned 119/1922 numbers and the per-file set,
which is exactly what a kill-switch must do; the classified error
classes are the evidence the errors are the documented boundary, not
tool breakage.

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned tarball hash matches, the extraction
     is present, and the corpus digest matches the pin (1861 .kt +
     129 .kts files).
  2. Parse honesty -- files_analyzed == 2015 (1990 Kotlin-pack files +
     23 .h routed to the C pack + 2 .api contract files claimed by the
     c pack... measured live: the pack list carries kotlin AND c),
     files_with_parse_errors == 119 (the pinned raw-source boundary,
     classified as above), parse_error_nodes == 1922.
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver honesty -- the declared-fact resolver fans out honestly:
     EVERY edge target inside the analyzed set (an edge always means
     "the target is part of this analysis"; the wildcard fan-out over
     ktor's big packages produces the measured 114635 edges -- the Go
     package fan-out discipline at Kotlin package scale), and OC006's
     verdict set is EXACTLY the zero-in-degree set minus the measured
     entry claims (1: the test example defining `fun main`, measured
     live with the main-definition shape).
  5. Rule honesty -- the measured rule counts are pinned: OC001 115,
     OC002 36, OC003 18 (the declared-fact resolver finds REAL import
     cycles on the corpus -- the first pack after Java with live cycles
     at corpus scale), OC004 35, OC005 0 (the declared-fact marking
     claims every importable export -- the honest fail-open direction
     of the wildcard + NULL-bind whole-file claims), OC006 571,
     OC007/OC009/OC010 silent, OC008 628, OC011 32. The supply meta
     block reports the JAVA pack's gradle tier (124 build.gradle.kts
     manifests scanned -- the multi-pack scan; the KOTLIN pack
     contributes zero rows: no Kotlin manifest reader exists in v1 and
     none is invented), and no OSV snapshot is loaded.
  6. Header-routing honesty -- ktor's 23 .h files route through the
     approved treatment: default_c == 23, default_cpp == 0, sniffed ==
     0 (ktor carries no cpp-pack extension, so the root default routes
     every header to C).
  7. Wall budget -- best wall <= 60 seconds single-core (the SPRINT-20
     budget; ktor measured ~10s at pin time).

Usage:
  python3 scripts/acceptance/kotlin_corpus_gate.py --oc build/default/oc \
      [--runs 5] [--verdict PATH]

Exit code 0 iff every criterion holds; the verdict JSON lands next to
the other gate verdicts by default (docs/evidence/).
"""
import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

TARBALL_NAME = "ktor_3.0.1.tar.gz"
TARBALL_SHA256 = "aa0d7b80261cb14f48f7384ad2bd5ff7f3e0da5104fcf1201a4beb12823acc20"
TARBALL_URL = "https://github.com/ktorio/ktor/archive/refs/tags/3.0.1.tar.gz"
CORPUS_DIR = "ktor-3.0.1"
CORPUS_DIGEST = "ab00070cd53d1719fa0596231d311d72aeabd5e1dd7dcdd02e508d4cab9a1afb"
KT_FILES = 1861
KTS_FILES = 129
ANALYZED_FILES = 2015
PARSE_ERRORS = 119
PARSE_ERROR_NODES = 1922
EXPECTED_EDGES = 114635
EXPECTED_COUNTS = {
    "OC001": 115, "OC002": 36, "OC003": 18, "OC004": 35, "OC005": 0, "OC006": 571,
    "OC007": 0, "OC008": 628, "OC009": 0, "OC010": 0, "OC011": 32,
}
EXPECTED_ROUTING = {"sniffed_cpp": 0, "sniffed_c": 0, "default_cpp": 0, "default_c": 23}
WALL_BUDGET = 60.0


def corpus_digest(root, base):
    h = hashlib.sha256()
    n = 0
    kt = 0
    kts = 0
    for dirpath, dirnames, filenames in sorted(os.walk(root)):
        dirnames.sort()
        for fn in sorted(filenames):
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(p, base)
            h.update(rel.encode())
            with open(p, "rb") as f:
                h.update(f.read())
            n += 1
            if fn.endswith(".kt"):
                kt += 1
            if fn.endswith(".kts"):
                kts += 1
    return h.hexdigest(), n, kt, kts


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def classify_errors(report, corpus_root):
    """The pinned error-class tally (the docstring's classification)."""
    tally = collections.Counter()
    for e in report.get("parse_errors", []):
        p = e.get("file", "")
        ln = e.get("first_line", 0)
        if "/include/curl/" in p or p.endswith(".h"):
            tally["c_interop_header_c_pack_boundary"] += 1
            continue
        if e.get("error_nodes", 1) == 0:
            tally["missing_token_only"] += 1
            continue
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                lines = f.readlines()
            line = lines[ln - 1].rstrip() if 0 < ln <= len(lines) else ""
        except OSError:
            line = ""
        if ".the<" in line:
            tally["gradle_kts_the_T"] += 1
        elif ".() ->" in line:
            tally["function_type_with_receiver"] += 1
        elif "suspend" in line and ("->" in line or "(" in line):
            tally["suspend_fun_type"] += 1
        elif ("expect " in line or "actual " in line or "fun interface" in line
              or "..<" in line or " by " in line):
            tally["kotlin_19_plus_syntax"] += 1
        else:
            tally["residual"] += 1
    return tally


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--workdir", default=None, help="work/ dir holding the pinned tarball")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    work = os.path.abspath(args.workdir or repo)
    tarball = os.path.join(work, TARBALL_NAME)
    corpus_root = os.path.join(work, CORPUS_DIR)
    if not os.path.isdir(corpus_root):
        print(f"hint: provision {work}/{TARBALL_NAME} (sha256 {TARBALL_SHA256}) from "
              f"{TARBALL_URL} and extract with `tar xzf {TARBALL_NAME}` in {work}; the gate "
              "never downloads anything (the analysis universe is the pinned archive).")
        sys.exit(2)
    if not os.path.exists(oc):
        sys.exit(f"missing: {oc}")

    tmp = "/tmp/omnicheck_kt_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # --- criterion 1: corpus integrity
    tar_ok = os.path.exists(tarball) and sha256_file(tarball) == TARBALL_SHA256
    digest, n_files, n_kt, n_kts = corpus_digest(corpus_root, corpus_root)
    digest_ok = digest == CORPUS_DIGEST and n_kt == KT_FILES and n_kts == KTS_FILES
    crit1 = tar_ok and digest_ok
    print(f"criterion 1 (corpus integrity: tarball sha256 {tar_ok}, extraction digest "
          f"{digest[:16]}..., {n_kt} .kt + {n_kts} .kts files) -> "
          f"{'PASS' if crit1 else 'FAIL'}")

    # --- criteria 2-3: parse honesty + determinism
    hashes = []
    walls = []
    report = None
    for i in range(args.runs):
        out = f"{tmp}/run{i}.json"
        t0 = time.monotonic()
        subprocess.run(wrap + [oc, "check", corpus_root, "--packs-dir",
                               os.path.join(repo, "packs"), "--format", "json"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
        walls.append(time.monotonic() - t0)
        hashes.append(sha256_file(out))
        if i == 0:
            with open(out) as fh:
                report = json.load(fh)
    stable = len(set(hashes)) == 1
    meta = report.get("meta", {})
    analyzed = meta.get("files_analyzed", 0)
    parse_errs = meta.get("files_with_parse_errors", -1)
    err_nodes = meta.get("parse_error_nodes", -1)
    packs = meta.get("packs") or []
    routing = meta.get("header_routing")
    crit2 = (analyzed == ANALYZED_FILES and parse_errs == PARSE_ERRORS
             and err_nodes == PARSE_ERROR_NODES and "kotlin" in packs and "c" in packs)
    crit3 = stable
    print(f"criterion 2 (parse honesty: analyzed={analyzed}, parse_errors={parse_errs} "
          f"[pinned raw-source boundary], error_nodes={err_nodes}, packs carry "
          f"kotlin+c={all(p in packs for p in ('kotlin', 'c'))}) -> "
          f"{'PASS' if crit2 else 'FAIL'}")
    print(f"criterion 3 (byte-identical, {args.runs} runs) -> {'PASS' if crit3 else 'FAIL'}")

    # --- criterion 4: resolver honesty + the OC006 internal consistency
    graph_path = f"{tmp}/graph.json"
    with open(graph_path, "wb") as fh:
        subprocess.run([oc, "graph", corpus_root, "--packs-dir", os.path.join(repo, "packs")],
                       stdout=fh, stderr=subprocess.DEVNULL)
    with open(graph_path) as fh:
        graph = json.load(fh)
    nodes = {n["id"]: n["path"] for n in graph.get("nodes", []) if n.get("kind") == "file"}
    edges = [(nodes[e["from"]], nodes[e["to"]]) for e in graph.get("edges", [])
             if e.get("kind") == "imports"]
    targets = set(b for _, b in edges)
    outside = targets - set(nodes.values())
    ind = collections.Counter(b for _, b in edges)
    zero = set(p for p in nodes.values() if ind[p] == 0)

    oc006_files = set(f.get("file", "") for f in report.get("findings", [])
                      if f.get("rule") == "OC006")
    # the entry claims are measured LIVE: zero-in-degree files that no
    # OC006 finding covers are exactly the claimed ones (the Kotlin main
    # parse fact); every claimed file must carry a main-definition shape
    # (the honest cross-check)
    claimed = zero - oc006_files
    main_shape = 0
    for p in claimed:
        try:
            txt = open(p, "rb").read().decode("utf-8", "replace")
        except OSError:
            continue
        if re.search(r"\bfun\s+main\s*\(", txt):
            main_shape += 1
    crit4 = (len(edges) == EXPECTED_EDGES
             and len(outside) == 0
             and len(oc006_files) == len(zero) - len(claimed)
             and all(ind[f] == 0 for f in oc006_files)
             and main_shape == len(claimed))
    print(f"criterion 4 (resolver honesty: {len(edges)} edges, targets outside the "
          f"analyzed set: {len(outside)}, OC006 == zero({len(zero)}) - claimed "
          f"({len(claimed)}, all with a main-definition shape) -> "
          f"{'PASS' if crit4 else 'FAIL'}")

    # --- criterion 5: rule honesty (measured counts pinned) + supply tier
    counts = collections.Counter(f.get("rule", "") for f in report.get("findings", []))
    supply = meta.get("supply", {}) or {}
    got_counts = {k: counts.get(k, 0) for k in EXPECTED_COUNTS}
    crit5 = (got_counts == EXPECTED_COUNTS
             and supply.get("manifests_scanned") == 124  # the JAVA gradle tier
             and supply.get("osv_snapshot_date") is None
             and counts.get("OC007", 0) == 0
             and counts.get("OC009", 0) == 0
             and counts.get("OC010", 0) == 0)
    print(f"criterion 5 (rule honesty: {dict(counts)}; supply = the Java gradle tier "
          f"(manifests_scanned={supply.get('manifests_scanned')}, the kotlin pack "
          f"contributes zero rows), no OSV snapshot loaded) -> "
          f"{'PASS' if crit5 else 'FAIL'}")

    # --- criterion 6: header-routing honesty
    crit6 = routing == EXPECTED_ROUTING
    print(f"criterion 6 (header routing: {routing}; ktor carries no cpp extension, "
          f"every .h takes the root default to C) -> {'PASS' if crit6 else 'FAIL'}")

    # --- criterion 7: wall budget
    best = min(walls)
    crit7 = best <= WALL_BUDGET
    print(f"criterion 7 (wall budget: best {best:.2f}s <= {WALL_BUDGET}s single-core) -> "
          f"{'PASS' if crit7 else 'FAIL'}")

    # --- the pinned error-class tally (supporting evidence)
    tally = classify_errors(report, corpus_root)
    print("error classes (pinned):", dict(tally))

    ok = crit1 and crit2 and crit3 and crit4 and crit5 and crit6 and crit7
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "Kotlin reference corpus (SPRINT-22): ktorio/ktor 3.0.1 tag tarball, "
                "1861 .kt + 129 .kts files (+23 .h routed to the C pack)",
        "date_executed": "2026-11-23",
        "tarball": TARBALL_NAME,
        "tarball_sha256": TARBALL_SHA256,
        "tarball_url": TARBALL_URL,
        "corpus_digest": CORPUS_DIGEST,
        "contract": "docs/LANGUAGES.md (Kotlin section)",
        "grammar_pin": "fwcd/tree-sitter-kotlin 0.3.8",
        "criteria": {
            "1_corpus_integrity": {"pass": crit1, "tarball_sha256_ok": tar_ok,
                                   "digest_ok": digest_ok, "kt_files": n_kt,
                                   "kts_files": n_kts},
            "2_parse_honesty": {"pass": crit2, "analyzed": analyzed,
                                "parse_errors": parse_errs, "error_nodes": err_nodes,
                                "packs_kotlin_and_c": all(p in packs for p in ("kotlin", "c"))},
            "3_determinism": {"pass": crit3, "runs": args.runs},
            "4_resolver_honesty": {"pass": crit4, "edges": len(edges),
                                   "targets_outside": len(outside),
                                   "zero_in_degree": len(zero), "oc006": len(oc006_files),
                                   "claimed": len(claimed),
                                   "claimed_main_shape": main_shape},
            "5_rule_honesty": {"pass": crit5, "counts": dict(counts),
                               "supply": supply,
                               "kotlin_pack_rows": 0},
            "6_header_routing": {"pass": crit6, "routing": routing},
            "7_wall_budget": {"pass": crit7, "best_wall_seconds": round(best, 2),
                              "budget": WALL_BUDGET},
        },
        "parse_error_classes": dict(tally),
        "pass": ok,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence",
                                       "kotlin_corpus_gate_verdict.json")
    with open(out, "w") as f:
        json.dump(verdict, f, indent=1, sort_keys=True)
    print("verdict:", "PASS" if ok else "FAIL", "->", out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
