#!/usr/bin/env python3
"""Rust reference-corpus gate (SPRINT-15, recipe step 8 -- the m2/m3
pattern applied to a REAL Rust corpus).

Pinned corpus: Debian source package `librust-tokio-dev` version
1.43.1-1 (upstream tokio-rs/tokio v1.43.1) -- a real-world, actively
maintained Rust crate: 516 .rs files across the runtime/io/fs/sync/net/
process/signal/task trees, cargo-registry layout under
usr/share/cargo/registry/tokio-1.43.1. The .deb is pinned by sha256 and
lives in work/; extraction is verified by a corpus digest (sha256 over
the sorted .rs paths + contents). No github access needed -- the Debian
archive ecosystem is reachable where the git protocol is not.

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned .deb hash matches, the extraction
     is present, and the corpus digest matches the pin.
  2. Parse clean -- every .rs file analyzed, ZERO parse errors: real
     world Rust parses with the pinned grammar (kill-switch criterion).
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver realism -- exactly 779 imports edges, all of them INSIDE
     the corpus (external crate names and the crate's own name from the
     integration tests contribute none -- the manifest is never read),
     247 distinct target files. Internal-consistency criterion: OC006's
     verdict set is EXACTLY the zero-in-degree files minus the tests/
     runner conventions minus the crate root (measured live from the
     graph + report, the Go gate's test-file discipline generalized).
  5. Rule honesty -- the measured rule counts are pinned: OC001 80,
     OC002 53, OC003 17, OC004 10, OC005 87, OC006 99, OC008 735.
     OC003 is NOT zero for Rust (the deliberate contrast with Go):
     Rust module cycles are LEGAL -- a parent module file declares
     `mod child;` while the child uses `crate::parent::item`, and the
     items resolve globally; one such SCC was verified by hand
     (src/fs/mod.rs + its children). OC005 verdicts are the honestly-
     unused exports at binding-name granularity. The supply tier joins
     the pin: exactly ONE manifest (the crate Cargo.toml), whose
     license field makes OC010 silent -- the licence clause is read
     and detected; OC007/OC009 stay silent (no profile, no snapshot).
  6. Wall budget -- full check under 60 s single-core (measured 1.6 s
     at pin time).

Usage:
  python3 scripts/acceptance/rust_corpus_gate.py --oc build/default/oc \
      [--runs 5] [--verdict PATH]

Exit code 0 iff every criterion holds; verdict JSON lands in
docs/evidence/ by default.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

DEB_NAME = "librust-tokio-dev_1.43.1-1_amd64.deb"
DEB_SHA256 = "e524b6230af1fad10489c2021eec35c1fa78d1c5cc041c4ba300638a31eef90a"
CORPUS_REL = os.path.join("usr", "share", "cargo", "registry", "tokio-1.43.1")
RUST_FILES = 516
# sha256 over the sorted repo-relative .rs paths + contents (measured at
# pin time; byte-deterministic because the corpus itself is pinned).
CORPUS_DIGEST = "0b77bb9b98847f041519b252b6f2e663ea62d443fae6f8175a388481d7a588d6"
EXPECTED_COUNTS = {"OC001": 80, "OC002": 53, "OC003": 17, "OC004": 10, "OC005": 87,
                   "OC006": 99, "OC008": 735}
EXPECTED_EDGES = 779
EXPECTED_TARGETS = 247
EXPECTED_MANIFESTS = 1
EXPECTED_OC010 = 0  # tokio's Cargo.toml declares license = "MIT": the clause is read
WALL_BUDGET_S = 60.0


def corpus_digest(root, base):
    # paths are hashed RELATIVE to the extraction root, so the digest is
    # independent of where work/ lives on the analysis host
    h = hashlib.sha256()
    paths = []
    for r, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(".rs"):
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
    extracted = os.path.join(work, "tokio-root")
    corpus_root = os.path.join(extracted, CORPUS_REL)
    if not os.path.exists(corpus_root):
        print(f"hint: provision {work}/{DEB_NAME} (sha256 {DEB_SHA256}) and extract with "
              f"`dpkg -x {DEB_NAME} {extracted}`; the gate never downloads anything "
              "(the analysis universe is the pinned archive).")
        sys.exit(2)
    if not os.path.exists(oc):
        sys.exit(f"missing: {oc}")

    tmp = "/tmp/omnicheck_rust_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # --- criterion 1: corpus integrity
    deb_ok = os.path.exists(deb) and sha256_file(deb) == DEB_SHA256
    digest, n_rs = corpus_digest(corpus_root, extracted)
    digest_ok = digest == CORPUS_DIGEST and n_rs == RUST_FILES
    crit1 = deb_ok and digest_ok
    print(f"criterion 1 (corpus integrity: deb sha256 + extraction digest {digest[:16]}..., "
          f"{n_rs} .rs files) -> {'PASS' if crit1 else 'FAIL'}")

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
    crit2 = analyzed == RUST_FILES and parse_errs == 0 and err_nodes == 0 \
        and "rust" in (meta.get("packs") or [])
    crit3 = stable
    print(f"criterion 2 (parse clean: analyzed={analyzed}, parse_errors={parse_errs}, "
          f"error_nodes={err_nodes}) -> {'PASS' if crit2 else 'FAIL'}")
    print(f"criterion 3 (byte-identical, {args.runs} runs) -> {'PASS' if crit3 else 'FAIL'}")

    # --- criterion 4: resolver realism + the OC006 internal consistency
    graph_path = f"{tmp}/graph.json"
    with open(graph_path, "wb") as fh:
        subprocess.run([oc, "graph", corpus_root, "--packs-dir", os.path.join(repo, "packs")],
                       stdout=fh, stderr=subprocess.DEVNULL)
    with open(graph_path) as fh:
        graph = json.load(fh)
    nodes = {n["id"]: n["path"] for n in graph.get("nodes", []) if n.get("kind") == "file"}
    edges = [(nodes[e["from"]], nodes[e["to"]]) for e in graph.get("edges", [])
             if e.get("kind") == "imports"]
    outside = [1 for a, b in edges if f"{os.sep}{CORPUS_REL}{os.sep}" not in a.replace("/", os.sep)
               and "/tokio-1.43.1/" not in a and "/tokio-1.43.1/" not in b]
    outside = [1 for a, b in edges if "/tokio-1.43.1/" not in a or "/tokio-1.43.1/" not in b]
    targets = set(b for _, b in edges)
    import collections
    ind = collections.Counter(b for _, b in edges)
    zero = set(p for p in nodes.values() if ind[p] == 0)

    def tests_convention(p):
        rel = p.split("/tokio-1.43.1/", 1)[1]
        d = os.path.dirname(rel)
        return d.split("/")[-1] == "tests" and rel.endswith(".rs")

    conv = sum(1 for p in zero if tests_convention(p))
    entries = sum(1 for p in zero if p.endswith("src/lib.rs"))
    oc006_files = set(f.get("file", "") for f in report.get("findings", [])
                      if f.get("rule") == "OC006")
    expected_oc006 = len(zero) - conv - entries
    crit4 = (len(edges) == EXPECTED_EDGES and len(targets) == EXPECTED_TARGETS
             and len(outside) == 0
             and len(oc006_files) == expected_oc006
             and all(ind[f] == 0 for f in oc006_files))
    print(f"criterion 4 (resolver realism: {len(edges)} edges inside the corpus, "
          f"{len(targets)} targets, OC006 verdict set == zero({len(zero)}) - "
          f"tests-conv({conv}) - entry({entries}) = {expected_oc006}) -> "
          f"{'PASS' if crit4 else 'FAIL'}")

    # --- criterion 5: rule honesty (measured counts pinned)
    counts = {}
    for f in report.get("findings", []):
        counts[f.get("rule", "")] = counts.get(f.get("rule", ""), 0) + 1
    supply = meta.get("supply", {})
    crit5 = (counts == EXPECTED_COUNTS
             and supply.get("manifests_scanned", -1) == EXPECTED_MANIFESTS
             and counts.get("OC010", 0) == EXPECTED_OC010
             and supply.get("osv_snapshot_date") is None
             and counts.get("OC007", 0) == 0
             and counts.get("OC009", 0) == 0)
    print(f"criterion 5 (rule honesty: {counts}; manifests={supply.get('manifests_scanned')}, "
          f"OC010={counts.get('OC010', 0)} -- the crate's licence clause is read and detected) "
          f"-> {'PASS' if crit5 else 'FAIL'}")

    # --- criterion 6: wall budget
    best = min(walls)
    crit6 = best < WALL_BUDGET_S
    print(f"criterion 6 (wall budget: best {best:.2f}s < {WALL_BUDGET_S:.0f}s single-core) -> "
          f"{'PASS' if crit6 else 'FAIL'}")

    all_crits = {
        "1_corpus_integrity": {"pass": crit1, "deb_sha256": deb_ok,
                               "digest": digest, "rs_files": n_rs},
        "2_parse_clean": {"pass": crit2, "analyzed": analyzed, "parse_errors": parse_errs,
                          "error_nodes": err_nodes},
        "3_determinism": {"pass": crit3, "runs": args.runs, "distinct_sha256": len(set(hashes))},
        "4_resolver_realism": {"pass": crit4, "edges": len(edges), "targets": len(targets),
                               "outside_edges": len(outside), "zero_in_degree": len(zero),
                               "tests_convention": conv, "entries": entries,
                               "oc006_expected": expected_oc006, "oc006_measured": len(oc006_files)},
        "5_rule_honesty": {"pass": crit5, "counts": counts, "supply": supply,
                           "note": "OC003=17: Rust module cycles are LEGAL (parent mod "
                                   "declaration + child crate:: use, items resolve globally); "
                                   "one SCC verified by hand (src/fs/). The deliberate "
                                   "contrast with the Go corpus's OC003=0."},
        "6_wall_budget": {"pass": crit6, "best_s": round(best, 2), "budget_s": WALL_BUDGET_S},
    }
    all_pass = all(c["pass"] for c in all_crits.values())
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "Rust reference corpus (SPRINT-15): Debian librust-tokio-dev 1.43.1-1, "
                "516 files",
        "date_executed": time.strftime("%Y-%m-%d"),
        "deb": DEB_NAME,
        "deb_sha256": DEB_SHA256,
        "corpus_digest": CORPUS_DIGEST,
        "contract": "docs/LANGUAGES.md (Rust section)",
        "grammar_pin": "tree-sitter/tree-sitter-rust v0.24.0",
        "criteria": all_crits,
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "rust_corpus_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
