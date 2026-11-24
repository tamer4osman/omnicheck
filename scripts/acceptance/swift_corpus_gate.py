#!/usr/bin/env python3
r"""Swift reference-corpus gate (SPRINT-23, recipe step 8 -- the m2/m3
pattern applied to a REAL Swift corpus).

Pinned corpus: apple/swift-argument-parser 1.5.0 (the GitHub tag
tarball) -- the canonical, actively maintained SPM package for command-
line argument parsing (Apple's own tooling dependency; the multi-target
Package.swift exercises the declared-fact module resolver at corpus
scale: explicit paths AND the default Sources/<name> rule, plus a
manifest with REAL top-level code). The tarball is pinned by sha256 and
lives in work/; extraction is verified by a corpus digest (sha256 over
the sorted extraction-relative path+content pairs). 146 .swift files
are the Swift pack's material; no other pack's extension exists in the
tree (no header routing block, no supply tier).

THE RAW-SOURCE BOUNDARY (the honest delta, the SPRINT-20/21/22 shape):
a grammar pinned at alex-pinkus/tree-sitter-swift 0.7.1-with-generated-
files (June 2025) cannot fully parse every Swift-6-era idiom
as-written. 28 of the 146 analyzed files carry at least one parse error
(19.2 percent; classified at pin time, 142 error nodes): 27 the
ATTRIBUTE-EMPTY-PARENS shape (`@Flag()` / `@Argument()` / `@Option()` /
`@OptionGroup()` -- the pinned grammar's property-wrapper attribute
with an empty argument list, its largest gap on this corpus; the
argument-bearing spellings parse) and 1 directive-adjacent (the
first-error line sits on a `#endif`). No canonical Swift corpus parses
100 percent clean as-written with this pin. THE CRITERION IS THEREFORE
THE PIN, not zero: a grammar or tool regression MOVES the pinned
28/142 numbers and the per-file set, which is exactly what a
kill-switch must do; the classified error classes are the evidence the
errors are the documented boundary, not tool breakage.

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned tarball hash matches, the extraction
     is present, and the corpus digest matches the pin (194 files, 146
     .swift).
  2. Parse honesty -- files_analyzed == 146, files_with_parse_errors ==
     28 (the pinned raw-source boundary, classified as above),
     parse_error_nodes == 142, the swift pack loaded.
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver honesty -- the declared-fact MODULE resolver fans out
     honestly: 3294 edges, EVERY edge target inside the analyzed set
     (an edge always means "the target is part of this analysis"; the
     whole-module wholesale fan-out over the manifest's targets), and
     OC006's verdict set is EXACTLY the zero-in-degree set minus the
     measured entry claims (3, each carrying a real top-level statement
     shape -- the test-example main.swift and the two manifests whose
     `package.targets.append` IS top-level code, measured live).
  5. Rule honesty -- the measured rule counts are pinned: OC001 18,
     OC002 38, OC003 0 (SPM forbids target dependency cycles at build
     time and the corpus is acyclic -- the declared-fact resolver
     honestly finds none), OC004 7, OC005 0 (the declared-fact marking
     claims every importable export -- the honest fail-open direction
     of the whole-module NULL-bind claims), OC006 98, OC007/OC009/OC010
     silent, OC008 900 (the near-identical generated Examples/ family),
     OC011 0. The supply meta block is ABSENT (a Swift-only corpus: the
     swift pack contributes zero manifest rows and Package.swift is not
     a supply manifest) and no OSV snapshot is loaded.
  6. No header routing -- the tree carries no C/C++-pack extension, so
     the header_routing block is ABSENT (the Kotlin corpus's routing
     honesty, Swift spelling).
  7. Wall budget -- best wall <= 60 seconds single-core.

Usage:
  python3 scripts/acceptance/swift_corpus_gate.py --oc build/default/oc \
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

TARBALL_NAME = "swift-argument-parser_1.5.0.tar.gz"
TARBALL_SHA256 = "946a4cf7bdd2e4f0f8b82864c56332238ba3f0a929c6d1a15f55affdb10634e6"
TARBALL_URL = "https://github.com/apple/swift-argument-parser/archive/refs/tags/1.5.0.tar.gz"
CORPUS_DIR = "swift-argument-parser-1.5.0"
CORPUS_DIGEST = "3bb0ac6fce3b582eb3b5d37ba4c21ff4a2bff77be1b3e773e8058bb8943665fa"
SWIFT_FILES = 146
TOTAL_FILES = 194
ANALYZED_FILES = 146
PARSE_ERRORS = 28
PARSE_ERROR_NODES = 142
EXPECTED_EDGES = 3294
EXPECTED_COUNTS = {
    "OC001": 18, "OC002": 38, "OC003": 0, "OC004": 7, "OC005": 0, "OC006": 98,
    "OC007": 0, "OC008": 900, "OC009": 0, "OC010": 0, "OC011": 0,
}
WALL_BUDGET = 60.0

# the top-level-statement shapes an honestly-claimed file must carry
# (the same shapes the Q_SW_TOP parse fact fires on, as-written)
TOPLEVEL_RE = re.compile(r"^[a-zA-Z_][a-zA-Z0-9_.]*\(")


def corpus_digest(root, base):
    h = hashlib.sha256()
    n = 0
    swift = 0
    for dirpath, dirnames, filenames in sorted(os.walk(root)):
        dirnames.sort()
        for fn in sorted(filenames):
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(p, base)
            h.update(rel.encode())
            with open(p, "rb") as f:
                h.update(f.read())
            n += 1
            if fn.endswith(".swift"):
                swift += 1
    return h.hexdigest(), n, swift


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def classify_errors(report):
    """The pinned error-class tally (the docstring's classification)."""
    tally = collections.Counter()
    for e in report.get("parse_errors", []):
        p = e.get("file", "")
        ln = e.get("first_line", 0)
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                lines = f.readlines()
            line = lines[ln - 1].rstrip() if 0 < ln <= len(lines) else ""
        except OSError:
            line = ""
        if any(w in line for w in ("@Flag()", "@Argument()", "@Option()",
                                   "@OptionGroup()")):
            tally["attribute_empty_parens"] += 1
        elif line.startswith("#"):
            tally["directive_adjacent"] += 1
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

    tmp = "/tmp/omnicheck_swift_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # --- criterion 1: corpus integrity
    tar_ok = os.path.exists(tarball) and sha256_file(tarball) == TARBALL_SHA256
    digest, n_files, n_swift = corpus_digest(corpus_root, corpus_root)
    digest_ok = digest == CORPUS_DIGEST and n_swift == SWIFT_FILES and n_files == TOTAL_FILES
    crit1 = tar_ok and digest_ok
    print(f"criterion 1 (corpus integrity: tarball sha256 {tar_ok}, extraction digest "
          f"{digest[:16]}..., {n_files} files ({n_swift} .swift)) -> "
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
    crit2 = (analyzed == ANALYZED_FILES and parse_errs == PARSE_ERRORS
             and err_nodes == PARSE_ERROR_NODES and "swift" in packs)
    crit3 = stable
    print(f"criterion 2 (parse honesty: analyzed={analyzed}, parse_errors={parse_errs} "
          f"[pinned raw-source boundary], error_nodes={err_nodes}, swift pack loaded="
          f"{'swift' in packs}) -> {'PASS' if crit2 else 'FAIL'}")
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
    # OC006 finding covers are exactly the claimed ones (the Swift
    # top-level-statement parse fact); every claimed file must carry a
    # top-level statement shape (the honest cross-check)
    claimed = zero - oc006_files
    toplevel_shape = 0
    for p in claimed:
        try:
            txt = open(p, "rb").read().decode("utf-8", "replace")
        except OSError:
            continue
        if any(TOPLEVEL_RE.match(l) for l in txt.splitlines()):
            toplevel_shape += 1
    crit4 = (len(edges) == EXPECTED_EDGES
             and len(outside) == 0
             and len(oc006_files) == len(zero) - len(claimed)
             and all(ind[f] == 0 for f in oc006_files)
             and toplevel_shape == len(claimed))
    print(f"criterion 4 (resolver honesty: {len(edges)} edges, targets outside the "
          f"analyzed set: {len(outside)}, OC006 == zero({len(zero)}) - claimed "
          f"({len(claimed)}, all with a top-level-statement shape) -> "
          f"{'PASS' if crit4 else 'FAIL'}")

    # --- criterion 5: rule honesty (measured counts pinned) + supply tier
    counts = collections.Counter(f.get("rule", "") for f in report.get("findings", []))
    got_counts = {k: counts.get(k, 0) for k in EXPECTED_COUNTS}
    crit5 = (got_counts == EXPECTED_COUNTS
             and meta.get("supply") is None
             and meta.get("manifests_scanned") is None)
    print(f"criterion 5 (rule honesty: {dict(counts)}; supply tier ABSENT on the "
          f"Swift-only corpus (manifests_scanned key absent: Package.swift is not a "
          f"supply manifest), no OSV snapshot loaded) -> {'PASS' if crit5 else 'FAIL'}")

    # --- criterion 6: no header routing (no C/C++ extension in the tree)
    crit6 = meta.get("header_routing") is None
    print(f"criterion 6 (no header routing: the tree carries no C/C++-pack extension) "
          f"-> {'PASS' if crit6 else 'FAIL'}")

    # --- criterion 7: wall budget
    best = min(walls)
    crit7 = best <= WALL_BUDGET
    print(f"criterion 7 (wall budget: best {best:.2f}s <= {WALL_BUDGET}s single-core) -> "
          f"{'PASS' if crit7 else 'FAIL'}")

    # --- the pinned error-class tally (supporting evidence)
    tally = classify_errors(report)
    print("error classes (pinned):", dict(tally))

    ok = crit1 and crit2 and crit3 and crit4 and crit5 and crit6 and crit7
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "Swift reference corpus (SPRINT-23): apple/swift-argument-parser 1.5.0 "
                "tag tarball, 146 .swift files (the multi-target SPM tree; no other "
                "pack's extension exists)",
        "date_executed": "2026-11-23",
        "tarball": TARBALL_NAME,
        "tarball_sha256": TARBALL_SHA256,
        "tarball_url": TARBALL_URL,
        "corpus_digest": CORPUS_DIGEST,
        "contract": "docs/LANGUAGES.md (Swift section)",
        "grammar_pin": "alex-pinkus/tree-sitter-swift 0.7.1-with-generated-files",
        "criteria": {
            "1_corpus_integrity": {"pass": crit1, "tarball_sha256_ok": tar_ok,
                                   "digest_ok": digest_ok, "swift_files": n_swift,
                                   "total_files": n_files},
            "2_parse_honesty": {"pass": crit2, "analyzed": analyzed,
                                "parse_errors": parse_errs, "error_nodes": err_nodes,
                                "swift_pack_loaded": "swift" in packs},
            "3_determinism": {"pass": crit3, "runs": args.runs},
            "4_resolver_honesty": {"pass": crit4, "edges": len(edges),
                                   "targets_outside": len(outside),
                                   "zero_in_degree": len(zero), "oc006": len(oc006_files),
                                   "claimed": len(claimed),
                                   "claimed_toplevel_shape": toplevel_shape},
            "5_rule_honesty": {"pass": crit5, "counts": dict(counts),
                               "supply_block_absent": meta.get("supply") is None,
                               "swift_pack_rows": 0},
            "6_no_header_routing": {"pass": crit6, "routing": meta.get("header_routing")},
            "7_wall_budget": {"pass": crit7, "best_wall_seconds": round(best, 2),
                              "budget": WALL_BUDGET},
        },
        "parse_error_classes": dict(tally),
        "pass": ok,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence",
                                       "swift_corpus_gate_verdict.json")
    with open(out, "w") as f:
        json.dump(verdict, f, indent=1, sort_keys=True)
    print("verdict:", "PASS" if ok else "FAIL", "->", out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
