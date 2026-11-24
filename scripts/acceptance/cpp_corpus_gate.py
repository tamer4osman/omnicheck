#!/usr/bin/env python3
r"""C++ reference-corpus gate (SPRINT-21, recipe step 8 -- the m2/m3
pattern applied to a REAL C++ corpus).

Pinned corpus: Debian source package `abseil` 20240722.0 (upstream
abseil-cpp at the Debian pin) -- a real-world, actively maintained,
production C++ library: 811 .cc/.h files across absl/ (container, hash,
log, numeric, profiler, random, status, strings, synchronization, time,
types, flags, base). The tarball is pinned by sha256 and lives in work/;
extraction is verified by a corpus digest (sha256 over the sorted
extraction-relative .cc/.h paths + contents). No github access needed --
the Debian archive ecosystem is reachable where the git protocol is
not. The tree also carries 5 python test utilities and 1 C compat file
(absl/base/c_header_test.c) claimed by the python and C packs -- the
multi-pack scan is part of the honest measurement.

THE RAW-SOURCE BOUNDARY (the honest delta, SPRINT-20's C shape): real
C++ interleaves macros INSIDE declarations at a density C never reaches.
Abseil's canonical idiom builds its namespaces from macros
(ABSL_NAMESPACE_BEGIN/END expand to `namespace absl {`), decorates
declarations with ABSL_ATTRIBUTE_* / ABSL_FLAG_* / ABSL_INTERNAL_* macro
families, and uses token pasting and X-macros in its test support. A
raw-source grammar cannot fully parse those shapes. 646 of the 817
analyzed files carry at least one parse error (334 headers, 312
sources; classified at pin time: 581 carry the macro-namespace class,
207+ the macro attribute/flag classes, ~20 token pasting / X-macros /
residual). No canonical C++ corpus parses 100% clean as-written
(measured at pin time: catch2 3.7.1 135/427 = 31.6%, spdlog 1.15.2
74/132 = 56.1%; abseil is the metaprogramming-heaviest of the pool).
THE CRITERION IS THEREFORE THE PIN, not zero: a grammar or tool
regression MOVES the pinned 646/4606 numbers and the per-file set,
which is exactly what a kill-switch must do; the classified error
classes are the evidence the errors are the documented boundary, not
tool breakage.

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned tarball hash matches, the extraction
     is present, and the corpus digest matches the pin (811 .cc/.h
     files: 452 .cc + 359 .h).
  2. Parse honesty -- files_analyzed == 817 (811 C-family + 5 python
     test utilities + 1 C compat file), files_with_parse_errors == 646
     (the pinned raw-source boundary; the pack list carries cpp AND c
     AND python), parse_error_nodes == 4606.
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver honesty -- exactly 21 imports edges, EVERY edge target
     inside the analyzed set (an edge always means "the target is part
     of this analysis"; abseil's canonical root-relative quoted include
     style -- `#include "absl/base/config.h"` from absl/strings/ --
     resolves only the same-directory payload forms under the C
     preprocessor's own file-relative rule, so the honest edge count is
     small and every unresolved system/macro/root-relative include stays
     recorded-and-unresolved), and OC006's verdict set is EXACTLY the
     zero-in-degree set minus the measured entry claims (18: the .cc
     test drivers defining main -- all 18 carry the main-definition
     shape or a python pack entry rule, measured live).
  5. Rule honesty -- the measured rule counts are pinned: OC001 568,
     OC002 199, OC003 0 (no same-dir include cycle exists in this
     corpus), OC004 152, OC005 1 (a PYTHON-pack finding: copts.py's
     GccStyleFilterAndCombine -- the python pack's named-bind semantics,
     measured, not fabricated), OC006 791, OC008 1724. OC007/OC009/
     OC010/OC011 silent (no profile, no snapshot, NO manifest tier
     exists for C++ includes -- the honest empty supply tier, pinned by
     the lang gate).
  6. Header-routing honesty -- every .h of the corpus routes through the
     approved treatment with the pinned block: sniffed_cpp == 1 (the
     literal-namespace header), sniffed_c == 0, default_cpp == 358 (the
     root default: the corpus carries .cc sources; the macro-built
     namespaces make most headers inconclusive under the sniff),
     default_c == 0.
  7. Wall budget -- full check under 60 s single-core (measured at pin
     time: ~5 s).

Usage:
  python3 scripts/acceptance/cpp_corpus_gate.py --oc build/default/oc \
      [--runs 5] [--verdict PATH]

Exit code 0 iff every criterion holds; verdict JSON lands in
docs/evidence/ by default.
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

TARBALL_NAME = "abseil_20240722.0.orig.tar.gz"
TARBALL_SHA256 = "df6d1f5ef540725f3851201176a60486aab332410788465c9877910bb471c34a"
TARBALL_URL = ("http://deb.debian.org/debian/pool/main/a/abseil/"
               + TARBALL_NAME)
CORPUS_DIR = "abseil-20240722.0"
C_FAMILY_FILES = 811  # 452 .cc + 359 .h
ANALYZED_FILES = 817  # 811 C-family + 5 python pack files + 1 C pack file
PARSE_ERRORS = 646    # the pinned raw-source boundary (see module docstring)
PARSE_ERROR_NODES = 4606
# sha256 over the sorted extraction-relative .cc/.h paths + contents
# (measured at pin time; byte-deterministic because the corpus itself is
# pinned).
CORPUS_DIGEST = "a855f0723c683e5a735c72c8f4bb98d1ee2bae943ee11b7211515b6747d2dc70"
EXPECTED_COUNTS = {"OC001": 568, "OC002": 199, "OC004": 152,
                   "OC005": 1, "OC006": 791, "OC008": 1724}
# OC003 is absent from the measured dict == 0 occurrences (no same-dir
# include cycle in this corpus); OC007/OC009/OC010/OC011 likewise.
EXPECTED_EDGES = 21
EXPECTED_ROUTING = {"sniffed_cpp": 1, "sniffed_c": 0, "default_cpp": 358, "default_c": 0}
WALL_BUDGET_S = 60.0


def corpus_digest(root, base):
    # paths are hashed RELATIVE to the extraction root, so the digest is
    # independent of where work/ lives on the analysis host
    h = hashlib.sha256()
    paths = []
    for r, _, fs in os.walk(root):
        for f in fs:
            if f.endswith((".cc", ".h")):
                paths.append(os.path.join(r, f))
    paths.sort()
    for p in paths:
        rel = os.path.relpath(p, base)
        h.update(rel.encode())
        with open(p, "rb") as fh:
            h.update(fh.read())
    return h.hexdigest(), len(paths)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


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

    tmp = "/tmp/omnicheck_cpp_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # --- criterion 1: corpus integrity
    tar_ok = os.path.exists(tarball) and sha256_file(tarball) == TARBALL_SHA256
    digest, n_c = corpus_digest(corpus_root, corpus_root)
    digest_ok = digest == CORPUS_DIGEST and n_c == C_FAMILY_FILES
    crit1 = tar_ok and digest_ok
    print(f"criterion 1 (corpus integrity: tarball sha256 {tar_ok}, extraction digest "
          f"{digest[:16]}..., {n_c} .cc/.h files) -> {'PASS' if crit1 else 'FAIL'}")

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
             and err_nodes == PARSE_ERROR_NODES and "cpp" in packs and "c" in packs
             and "python" in packs)
    crit3 = stable
    print(f"criterion 2 (parse honesty: analyzed={analyzed}, parse_errors={parse_errs} "
          f"[pinned raw-source boundary], error_nodes={err_nodes}, packs carry "
          f"cpp+c+python={all(p in packs for p in ('cpp', 'c', 'python'))}) -> "
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
    # OC006 finding covers are exactly the claimed ones (the C++ main
    # parse fact + the python pack's entry rules); every claimed C-family
    # file must carry a main-definition shape (the honest cross-check)
    claimed = zero - oc006_files
    main_shape = 0
    import re
    for p in claimed:
        if p.endswith(".py"):
            main_shape += 1  # the python pack's own entry rules claim them
            continue
        try:
            txt = open(p, "rb").read().decode("utf-8", "replace")
        except OSError:
            continue
        if re.search(r"\bmain\s*\([^)]*\)\s*\{", txt, re.S):
            main_shape += 1
    crit4 = (len(edges) == EXPECTED_EDGES
             and len(outside) == 0
             and len(oc006_files) == len(zero) - len(claimed)
             and all(ind[f] == 0 for f in oc006_files)
             and main_shape == len(claimed))
    print(f"criterion 4 (resolver honesty: {len(edges)} edges, targets outside the "
          f"analyzed set: {len(outside)}, OC006 == zero({len(zero)}) - claimed "
          f"({len(claimed)}, all with a main definition or a python pack entry rule) "
          f"-> {'PASS' if crit4 else 'FAIL'}")

    # --- criterion 5: rule honesty (measured counts pinned)
    counts = collections.Counter(f.get("rule", "") for f in report.get("findings", []))
    supply = meta.get("supply")
    crit5 = (dict(counts) == EXPECTED_COUNTS
             and supply is None  # no C++ manifest tier: the block does not exist
             and counts.get("OC007", 0) == 0
             and counts.get("OC009", 0) == 0
             and counts.get("OC010", 0) == 0
             and counts.get("OC011", 0) == 0)
    print(f"criterion 5 (rule honesty: {dict(counts)}; supply block absent="
          f"{supply is None} -- the honest empty supply tier) -> "
          f"{'PASS' if crit5 else 'FAIL'}")

    # --- criterion 6: header-routing honesty
    crit6 = routing == EXPECTED_ROUTING
    print(f"criterion 6 (header routing: {routing}; every .h routed through the "
          f"approved treatment) -> {'PASS' if crit6 else 'FAIL'}")

    # --- criterion 7: wall budget
    best = min(walls)
    crit7 = best < WALL_BUDGET_S
    print(f"criterion 7 (wall budget: best {best:.2f}s < {WALL_BUDGET_S:.0f}s single-core) -> "
          f"{'PASS' if crit7 else 'FAIL'}")

    all_crits = {
        "1_corpus_integrity": {"pass": crit1, "tarball_sha256": tar_ok,
                               "digest": digest, "cc_h_files": n_c},
        "2_parse_honesty": {"pass": crit2, "analyzed": analyzed,
                            "parse_errors": parse_errs, "parse_error_nodes": err_nodes,
                            "packs_note": "5 python test utilities + 1 C compat file "
                                          "(absl/base/c_header_test.c) join the C-family "
                                          "files -- the multi-pack scan is part of the "
                                          "honest measurement",
                            "boundary_note": "646/817 analyzed files carry at least one "
                                             "raw-source parse error (pinned): 581 carry "
                                             "the macro-namespace class (ABSL_NAMESPACE_"
                                             "BEGIN/END expand to `namespace absl {`), "
                                             "207+ the ABSL_ATTRIBUTE_*/ABSL_FLAG_* macro "
                                             "families, token pasting and X-macros in the "
                                             "test support, ~20 residual. No canonical "
                                             "C++ corpus parses 100% clean as-written "
                                             "(catch2 3.7.1 135/427, spdlog 1.15.2 "
                                             "74/132); the pinned count IS the "
                                             "kill-switch -- a grammar or tool regression "
                                             "moves it."},
        "3_determinism": {"pass": crit3, "runs": args.runs, "distinct_sha256": len(set(hashes))},
        "4_resolver_honesty": {"pass": crit4, "edges": len(edges), "targets": len(targets),
                               "targets_outside_analyzed_set": len(outside),
                               "zero_in_degree": len(zero), "claimed": len(claimed),
                               "claimed_with_main_or_py_entry": main_shape,
                               "oc006_measured": len(oc006_files),
                               "note": "abseil's canonical ROOT-RELATIVE quoted include "
                                       "style resolves only the same-directory payload "
                                       "forms under the file-relative rule; every "
                                       "unresolved system/macro/root-relative include "
                                       "stays recorded-and-unresolved."},
        "5_rule_honesty": {"pass": crit5, "counts": dict(counts), "supply_block": supply,
                           "note": "OC003=0: no same-dir include cycle exists in this "
                                   "corpus (the resolver-cycle semantics are pinned by "
                                   "the lang gate corpus B). OC005=1: a PYTHON-pack "
                                   "finding (copts.py's GccStyleFilterAndCombine -- the "
                                   "python pack's named-bind semantics), measured, not "
                                   "fabricated. No manifest tier exists for C++: the "
                                   "supply block is absent and the meta says so."},
        "6_header_routing": {"pass": crit6, "routing": routing,
                             "note": "every .h routed through the approved treatment: the "
                                     "literal-namespace header sniffs cpp, the macro-"
                                     "namespace headers are inconclusive (both grammars "
                                     "error on the raw macro tokens) and the ROOT "
                                     "DEFAULT claims them for cpp (the corpus carries "
                                     ".cc sources)."},
        "7_wall_budget": {"pass": crit7, "best_s": round(best, 2), "budget_s": WALL_BUDGET_S},
    }
    all_pass = all(c["pass"] for c in all_crits.values())
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "C++ reference corpus (SPRINT-21): Debian abseil 20240722.0 orig tarball, "
                "811 .cc/.h files (+5 python pack files, +1 C pack file)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "tarball": TARBALL_NAME,
        "tarball_sha256": TARBALL_SHA256,
        "tarball_url": TARBALL_URL,
        "corpus_digest": CORPUS_DIGEST,
        "contract": "docs/LANGUAGES.md (C++ section)",
        "grammar_pin": "tree-sitter/tree-sitter-cpp v0.23.4",
        "criteria": all_crits,
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "cpp_corpus_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
