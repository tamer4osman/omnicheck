#!/usr/bin/env python3
"""PHP reference-corpus gate (SPRINT-19, recipe step 8 -- the m2/m3
pattern applied to a REAL PHP corpus).

Pinned corpus: Debian source package `php-phpseclib3` version
3.0.19-1+deb12u6 (upstream phpseclib 3.0.19, phpseclib/phpseclib) -- a
real-world, actively maintained pure-PHP library: 334 .php files across
the Composer src tree under usr/share/php/phpseclib3 (Crypt, File, Math,
Net, Process, System -- PSR-4 class layout, namespaced classes, PHP 8
syntax, match arms, property promotion). The .deb is pinned by sha256
and lives in work/; extraction is verified by a corpus digest (sha256
over the sorted .php paths + contents). No github access needed -- the
Debian archive ecosystem is reachable where the git protocol is not.

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned .deb hash matches, the extraction
     is present, and the corpus digest matches the pin.
  2. Parse clean -- every .php file analyzed, ZERO parse errors: real
     world PHP parses with the pinned grammar (kill-switch criterion).
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver honesty -- exactly ZERO imports edges: phpseclib3 names
     NO sibling/parent file through a static require/include (the one
     static-string include in the corpus sits behind the argument guard
     shapes) and every class load rides the Composer autoloader at
     runtime -- the metaprogramming boundary of docs/LANGUAGES.md PHP
     section 8, accepted in the over-report direction. Internal-
     consistency criterion: OC006's verdict set is EXACTLY the zero-
     in-degree set (measured live from the graph + report).
  5. Rule honesty -- the measured rule counts are pinned: OC001 114,
     OC002 2, OC004 71, OC006 334, OC008 878, OC011 48. OC003 is zero
     (no edges), OC005 empty by construction (no file is imported --
     the OC005 precondition bounds the verdict set), OC007/OC009/OC010
     stay silent (no profile, no snapshot, and the install tree carries
     NO composer manifests -- the manifest tier is pinned by the lang
     gate instead).
  6. Wall budget -- full check under 60 s single-core (measured at pin
     time: 0.8 s).

Usage:
  python3 scripts/acceptance/php_corpus_gate.py --oc build/default/oc \
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

DEB_NAME = "php-phpseclib3_3.0.19-1+deb12u6_all.deb"
DEB_SHA256 = "0bb8c9a03ac30f90230d71cce6d5dd26fa0049cdc59f907a94ece2fe415417d6"
DEB_URL = ("http://deb.debian.org/debian/pool/main/p/php-phpseclib3/"
           + DEB_NAME)
CORPUS_REL = os.path.join("usr", "share", "php")
PHP_FILES = 334
# sha256 over the sorted extraction-relative .php paths + contents
# (measured at pin time; byte-deterministic because the corpus itself is
# pinned).
CORPUS_DIGEST = "e6bba4549ea2a1b8bdd61d8eafedb4670a568249dd74e1b319b0e13c113894ed"
EXPECTED_COUNTS = {"OC001": 114, "OC002": 2, "OC004": 71, "OC006": 334,
                   "OC008": 878, "OC011": 48}
EXPECTED_EDGES = 0
EXPECTED_MANIFESTS = 0
WALL_BUDGET_S = 60.0


def corpus_digest(root, base):
    # paths are hashed RELATIVE to the extraction root, so the digest is
    # independent of where work/ lives on the analysis host
    h = hashlib.sha256()
    paths = []
    for r, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(".php"):
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
    ap.add_argument("--workdir", default=None, help="work/ dir holding the pinned .deb")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    work = os.path.abspath(args.workdir or os.path.join(repo, "work"))
    deb = os.path.join(work, DEB_NAME)
    extracted = os.path.join(work, "phpseclib-root")
    corpus_root = os.path.join(extracted, CORPUS_REL)
    if not os.path.exists(corpus_root):
        print(f"hint: provision {work}/{DEB_NAME} (sha256 {DEB_SHA256}) from {DEB_URL} "
              f"and extract with `dpkg-deb -x {DEB_NAME} {extracted}`; the gate never "
              "downloads anything (the analysis universe is the pinned archive).")
        sys.exit(2)
    if not os.path.exists(oc):
        sys.exit(f"missing: {oc}")

    tmp = "/tmp/omnicheck_php_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # --- criterion 1: corpus integrity
    deb_ok = os.path.exists(deb) and sha256_file(deb) == DEB_SHA256
    digest, n_php = corpus_digest(extracted, extracted)
    digest_ok = digest == CORPUS_DIGEST and n_php == PHP_FILES
    crit1 = deb_ok and digest_ok
    print(f"criterion 1 (corpus integrity: deb sha256 + extraction digest {digest[:16]}..., "
          f"{n_php} .php files) -> {'PASS' if crit1 else 'FAIL'}")

    # --- criteria 2-3: parse clean + determinism
    hashes = []
    walls = []
    report = None
    for i in range(args.runs):
        out = f"{tmp}/run{i}.json"
        t0 = time.monotonic()
        subprocess.run(wrap + [oc, "check", corpus_root, "--packs-dir", os.path.join(repo, "packs"),
                               "--format", "json"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
        walls.append(time.monotonic() - t0)
        hashes.append(sha256_file(out))
        if i == 0:
            with open(out) as fh:
                report = json.load(fh)
    stable = len(set(hashes)) == 1
    meta = report.get("meta", {})
    analyzed = meta.get("files_analyzed", 0)
    parse_errs = meta.get("files_with_parse_errors", 1)
    err_nodes = meta.get("parse_error_nodes", 1)
    crit2 = analyzed == PHP_FILES and parse_errs == 0 and err_nodes == 0 \
        and "php" in (meta.get("packs") or [])
    crit3 = stable
    print(f"criterion 2 (parse clean: analyzed={analyzed}, parse_errors={parse_errs}, "
          f"error_nodes={err_nodes}) -> {'PASS' if crit2 else 'FAIL'}")
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
    ind = collections.Counter(b for _, b in edges)
    zero = set(p for p in nodes.values() if ind[p] == 0)

    def tests_convention(p):
        # the PHP pack registers NO test NAME conventions in v1
        return False

    def entry_claim(p):
        # the pack entry-point globs (index.php bare name, **/bin/*) +
        # NO shebang hint (PHP has none, the contract). The corpus
        # carries no index.php and no bin/ tree, so the expected claim
        # count is zero -- measured live the way the tool claims.
        rel = os.path.relpath(p, corpus_root)
        if os.path.basename(rel) == "index.php":
            return True
        return rel.startswith("bin/")

    entries = sum(1 for p in zero if entry_claim(p))
    conv = sum(1 for p in zero if tests_convention(p))
    oc006_files = set(f.get("file", "") for f in report.get("findings", [])
                      if f.get("rule") == "OC006")
    expected_oc006 = len(zero) - conv - entries
    crit4 = (len(edges) == EXPECTED_EDGES
             and len(oc006_files) == expected_oc006
             and all(ind[f] == 0 for f in oc006_files))
    print(f"criterion 4 (resolver honesty: {len(edges)} edges, OC006 verdict set == "
          f"zero({len(zero)}) - tests-conv({conv}) - entry({entries}) = {expected_oc006}) -> "
          f"{'PASS' if crit4 else 'FAIL'}")

    # --- criterion 5: rule honesty (measured counts pinned)
    counts = {}
    for f in report.get("findings", []):
        counts[f.get("rule", "")] = counts.get(f.get("rule", ""), 0) + 1
    supply = meta.get("supply", {})
    crit5 = (counts == EXPECTED_COUNTS
             and (supply.get("manifests_scanned") or 0) == EXPECTED_MANIFESTS
             and supply.get("osv_snapshot_date") is None
             and counts.get("OC007", 0) == 0
             and counts.get("OC009", 0) == 0
             and counts.get("OC010", 0) == 0
             and counts.get("OC003", 0) == 0
             and counts.get("OC005", 0) == 0)
    print(f"criterion 5 (rule honesty: {counts}; manifests={supply.get('manifests_scanned')}, "
          f"OC010={counts.get('OC010', 0)} -- the install tree carries no composer tier) "
          f"-> {'PASS' if crit5 else 'FAIL'}")

    # --- criterion 6: wall budget
    best = min(walls)
    crit6 = best < WALL_BUDGET_S
    print(f"criterion 6 (wall budget: best {best:.2f}s < {WALL_BUDGET_S:.0f}s single-core) -> "
          f"{'PASS' if crit6 else 'FAIL'}")

    all_crits = {
        "1_corpus_integrity": {"pass": crit1, "deb_sha256": deb_ok,
                               "digest": digest, "php_files": n_php},
        "2_parse_clean": {"pass": crit2, "analyzed": analyzed, "parse_errors": parse_errs,
                          "error_nodes": err_nodes},
        "3_determinism": {"pass": crit3, "runs": args.runs, "distinct_sha256": len(set(hashes))},
        "4_resolver_honesty": {"pass": crit4, "edges": len(edges), "targets": len(targets),
                               "zero_in_degree": len(zero),
                               "tests_convention": conv, "entries": entries,
                               "oc006_expected": expected_oc006, "oc006_measured": len(oc006_files)},
        "5_rule_honesty": {"pass": crit5, "counts": counts, "supply": supply,
                           "note": "OC006=334: phpseclib3 loads every class through the "
                                   "Composer autoloader at runtime (no static require/include "
                                   "of an analyzed file) -- the metaprogramming boundary of "
                                   "docs/LANGUAGES.md PHP section 8; the over-report direction "
                                   "is the accepted one. OC005 is empty by construction (no "
                                   "file is imported, so the OC005 precondition bounds the "
                                   "verdict set). OC011=48: PEM examples and token-shaped "
                                   "docblock text -- the scan is line-based over source as "
                                   "written (the honest first-match discipline)."},
        "6_wall_budget": {"pass": crit6, "best_s": round(best, 2), "budget_s": WALL_BUDGET_S},
    }
    all_pass = all(c["pass"] for c in all_crits.values())
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "PHP reference corpus (SPRINT-19): Debian php-phpseclib3 3.0.19-1+deb12u6, "
                "334 files",
        "date_executed": time.strftime("%Y-%m-%d"),
        "deb": DEB_NAME,
        "deb_sha256": DEB_SHA256,
        "deb_url": DEB_URL,
        "corpus_digest": CORPUS_DIGEST,
        "contract": "docs/LANGUAGES.md (PHP section)",
        "grammar_pin": "tree-sitter/tree-sitter-php v0.24.2",
        "criteria": all_crits,
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "php_corpus_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
