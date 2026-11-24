#!/usr/bin/env python3
r"""C reference-corpus gate (SPRINT-20, recipe step 8 -- the m2/m3
pattern applied to a REAL C corpus).

Pinned corpus: Debian source package `curl` 7.88.1 (upstream curl at the
bookworm pin) -- a real-world, actively maintained, pure-C99 library:
799 .c/.h files across lib/, lib/vtls, lib/vssh, lib/vquic, src/,
include/curl, docs/examples, tests/ and packages/ (macro-heavy platform
shims, K&R-free but full of preprocessor-interleaved code, GNU
extensions, extern "C" guards). The tarball is pinned by sha256 and
lives in work/; extraction is verified by a corpus digest (sha256 over
the sorted extraction-relative .c/.h paths + contents). No github access
needed -- the Debian archive ecosystem is reachable where the git
protocol is not. The tree also carries 4 python test utilities
(tests/*.py), claimed by the PYTHON pack -- the multi-pack scan is part
of the honest measurement.

THE SPRINT-21 ROUTING DELTA (re-pinned, documented): with the cpp pack
REGISTERED (SPRINT-21), the approved .h treatment activates on this
corpus -- curl carries ONE .cpp file (docs/examples/htmltitle.cpp,
skipped as unknown-type in SPRINT-20, claimed by the cpp pack since),
so the project-root default routes ALL 232 inconclusive curl headers to
the cpp grammar. The consequences are pinned: analyzed 803 -> 804 (the
claimed .cpp example), parse-error FILES unchanged at 203 (the same
files error under both grammars), parse-error NODES 2444 -> 2383 (the
cpp grammar recovers the same shapes with fewer error nodes), OC008
1811 -> 1812 (the example joins a duplicate group), every other rule
count unchanged, and the header_routing block reports the treatment
(sniffed_cpp 0, sniffed_c 0, default_cpp 232, default_c 0 -- never
silent). The SPRINT-20 verdict (docs/evidence/c_corpus_gate_verdict.json
at the SPRINT-20 tag) remains the historical evidence of the pre-routing
numbers.

THE RAW-SOURCE BOUNDARY (the honest delta from the PHP corpus gate):
PHP files are self-contained parse units, so the phpseclib3 corpus
parsed with ZERO errors. C is different: the preprocessor is a core
part of the language and OmniCheck deliberately analyzes source AS
WRITTEN (no preprocessing -- docs/LANGUAGES.md C section 1). Real-world
C interleaves #ifdef INSIDE expressions (`flags = A #ifdef X | B #endif
;`), between `else` and its statement, closes `extern "C" {` blocks
inside `#ifdef __cplusplus` (unbalanced braces across preprocessor
boundaries), and uses GNU named-variadic macros and comment token
pasting. A raw-source grammar cannot fully parse those shapes; 203 of
the 799 C files carry at least one (measured, classified at pin time).
THE CRITERION IS THEREFORE THE PIN, not zero: a grammar or tool
regression MOVES the pinned error count / node count / per-file set,
which is exactly what a kill-switch must do. The 596-file clean subset
plus the classified error classes are the evidence that the errors are
the documented boundary, not tool breakage. (zlib 1.3.1: 46/53; musl
1.2.3: 382/2248; sqlite 3.40.1 tree: 230/436 -- no canonical C corpus
parses 100% clean; curl 7.88.1 at 203/799 = 25.4% with the error set
dominated by the extern-C guard class.)

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned tarball hash matches, the extraction
     is present, and the corpus digest matches the pin (799 files).
  2. Parse honesty -- files_analyzed == 804 (799 C + 1 C++ example file
     claimed by the cpp pack + 4 python pack files),
     files_with_parse_errors == 203 (the pinned raw-source boundary; the
     pack list carries c AND cpp AND python), parse_error_nodes == 2383
     (the SPRINT-21 re-pin: 232 headers route to cpp under the approved
     treatment; see the routing delta note above), and the
     header_routing block equals the pinned treatment counts.
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver honesty -- exactly 2218 imports edges, EVERY edge target
     inside the analyzed set (an edge always means "the target is part
     of this analysis"; system includes, macro paths and missing
     headers produce none), and OC006's verdict set is EXACTLY the
     zero-in-degree set minus the measured entry claims (the C main
     parse fact + the cpp main parse fact + the 3 python pack entry
     claims), measured live.
  5. Rule honesty -- the measured rule counts are pinned: OC001 614,
     OC002 41, OC003 4 (REAL include cycles between curl headers --
     measured, not fabricated), OC004 372, OC006 448, OC008 1812
     (SPRINT-21 re-pin: +1 from the claimed .cpp example), OC011 27
     (credential-shaped text in curl's test data and docs -- the
     line-based scan over source as written). OC005 empty by
     construction (every file with in-degree > 0 has all exports marked
     used by the whole-file include binds), OC007/OC009/OC010 silent
     (no profile, no snapshot, NO manifest tier exists for C or C++
     -- the honest empty supply tier, pinned by both lang gates).
  6. Wall budget -- full check under 60 s single-core (measured at pin
     time: 2.5 s).

Usage:
  python3 scripts/acceptance/c_corpus_gate.py --oc build/default/oc \
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

TARBALL_NAME = "curl_7.88.1.orig.tar.gz"
TARBALL_SHA256 = "cdb38b72e36bc5d33d5b8810f8018ece1baa29a8f215b4495e495ded82bbf3c7"
TARBALL_URL = ("http://deb.debian.org/debian/pool/main/c/curl/"
               + TARBALL_NAME)
CORPUS_DIR = "curl-7.88.1"
C_FILES = 799
ANALYZED_FILES = 804  # 799 C files + 1 cpp pack file (htmltitle.cpp, the
                      # SPRINT-21 claimed example) + 4 python pack files
PARSE_ERRORS = 203    # the pinned raw-source boundary (see module docstring)
PARSE_ERROR_NODES = 2383  # the SPRINT-21 re-pin (see the routing delta note)
EXPECTED_ROUTING = {"sniffed_cpp": 0, "sniffed_c": 0, "default_cpp": 232, "default_c": 0}
# sha256 over the sorted extraction-relative .c/.h paths + contents
# (measured at pin time; byte-deterministic because the corpus itself is
# pinned).
CORPUS_DIGEST = "d288deee29327ead32d9e51d64ce418012008c2b520874a31280294306c653ba"
EXPECTED_COUNTS = {"OC001": 614, "OC002": 41, "OC003": 4, "OC004": 372,
                   "OC006": 448, "OC008": 1812, "OC011": 27}
EXPECTED_EDGES = 2218
EXPECTED_PY_FILES = 4
WALL_BUDGET_S = 60.0


def corpus_digest(root, base):
    # paths are hashed RELATIVE to the extraction root, so the digest is
    # independent of where work/ lives on the analysis host
    h = hashlib.sha256()
    paths = []
    for r, _, fs in os.walk(root):
        for f in fs:
            if f.endswith((".c", ".h")):
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

    tmp = "/tmp/omnicheck_c_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # --- criterion 1: corpus integrity
    tar_ok = os.path.exists(tarball) and sha256_file(tarball) == TARBALL_SHA256
    digest, n_c = corpus_digest(corpus_root, corpus_root)
    digest_ok = digest == CORPUS_DIGEST and n_c == C_FILES
    crit1 = tar_ok and digest_ok
    print(f"criterion 1 (corpus integrity: tarball sha256 {tar_ok}, extraction digest "
          f"{digest[:16]}..., {n_c} .c/.h files) -> {'PASS' if crit1 else 'FAIL'}")

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
             and err_nodes == PARSE_ERROR_NODES and "c" in packs and "cpp" in packs
             and "python" in packs and routing == EXPECTED_ROUTING)
    crit3 = stable
    print(f"criterion 2 (parse honesty: analyzed={analyzed}, parse_errors={parse_errs} "
          f"[pinned raw-source boundary], error_nodes={err_nodes}, routing={routing}) -> "
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
    # OC006 finding covers are exactly the claimed ones (the C main
    # parse fact + the python pack's entry rules); every claimed C file
    # must carry a main-definition shape (the honest cross-check)
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
             and supply is None  # no C manifest tier: the block does not exist
             and counts.get("OC007", 0) == 0
             and counts.get("OC009", 0) == 0
             and counts.get("OC010", 0) == 0
             and counts.get("OC005", 0) == 0)
    print(f"criterion 5 (rule honesty: {dict(counts)}; supply block absent="
          f"{supply is None} -- the honest empty supply tier) -> "
          f"{'PASS' if crit5 else 'FAIL'}")

    # --- criterion 6: wall budget
    best = min(walls)
    crit6 = best < WALL_BUDGET_S
    print(f"criterion 6 (wall budget: best {best:.2f}s < {WALL_BUDGET_S:.0f}s single-core) -> "
          f"{'PASS' if crit6 else 'FAIL'}")

    all_crits = {
        "1_corpus_integrity": {"pass": crit1, "tarball_sha256": tar_ok,
                               "digest": digest, "c_files": n_c},
        "2_parse_honesty": {"pass": crit2, "analyzed": analyzed,
                            "parse_errors": parse_errs, "parse_error_nodes": err_nodes,
                            "header_routing": routing,
                            "boundary_note": "203/804 analyzed files carry at least one "
                                             "raw-source parse error (pinned): preprocessor-"
                                             "interleaved expressions and control flow, extern "
                                             "\"C\" guards closed inside #ifdef __cplusplus, GNU "
                                             "named-variadic macros, comment token pasting; the "
                                             "SPRINT-21 routing delta routes 232 inconclusive "
                                             "headers to the cpp grammar (fewer error nodes on "
                                             "the same files). No canonical C corpus parses "
                                             "100% clean as-written (zlib 46/53, musl 382/2248, "
                                             "sqlite tree 230/436); the pinned count IS the "
                                             "kill-switch -- a grammar or tool regression moves "
                                             "it."},
        "3_determinism": {"pass": crit3, "runs": args.runs, "distinct_sha256": len(set(hashes))},
        "4_resolver_honesty": {"pass": crit4, "edges": len(edges), "targets": len(targets),
                               "targets_outside_analyzed_set": len(outside),
                               "zero_in_degree": len(zero), "claimed": len(claimed),
                               "claimed_with_main_or_py_entry": main_shape,
                               "oc006_measured": len(oc006_files)},
        "5_rule_honesty": {"pass": crit5, "counts": dict(counts), "supply_block": supply,
                           "note": "OC003=4: REAL include cycles between curl headers "
                                   "(measured, not fabricated). OC005 is empty by "
                                   "construction: every file with in-degree > 0 has its "
                                   "exports marked used by the whole-file include binds. "
                                   "OC011=27: credential-shaped text in curl's test data "
                                   "and docs -- the line-based scan over source as written. "
                                   "No manifest tier exists for C: the supply block is "
                                   "absent and the meta says so."},
        "6_wall_budget": {"pass": crit6, "best_s": round(best, 2), "budget_s": WALL_BUDGET_S},
    }
    all_pass = all(c["pass"] for c in all_crits.values())
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "C reference corpus (SPRINT-20, re-pinned SPRINT-21 for the .h routing "
                "treatment): Debian curl 7.88.1 orig tarball, 799 .c/.h files (+1 cpp "
                "pack file, +4 python pack files)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "tarball": TARBALL_NAME,
        "tarball_sha256": TARBALL_SHA256,
        "tarball_url": TARBALL_URL,
        "corpus_digest": CORPUS_DIGEST,
        "contract": "docs/LANGUAGES.md (C section)",
        "grammar_pin": "tree-sitter/tree-sitter-c v0.23.4",
        "criteria": all_crits,
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "c_corpus_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
