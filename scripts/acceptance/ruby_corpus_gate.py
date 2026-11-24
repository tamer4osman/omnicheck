#!/usr/bin/env python3
"""Ruby reference-corpus gate (SPRINT-16, recipe step 8 -- the m2/m3
pattern applied to a REAL Ruby corpus).

Pinned corpus: Debian source package `ruby-sequel` version 5.63.0-2
(upstream sequel 5.63.0, jeremyevans/sequel) -- a real-world, actively
maintained pure-Ruby gem: 280 .rb files across lib/sequel (core, model,
adapters, extensions, migration, thread_localization), the Debian
vendor_ruby layout under usr/lib/ruby/vendor_ruby. The .deb is pinned by
sha256 and lives in work/; extraction is verified by a corpus digest
(sha256 over the sorted .rb paths + contents). No github access needed
-- the Debian archive ecosystem is reachable where the git protocol is
not.

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned .deb hash matches, the extraction
     is present, and the corpus digest matches the pin.
  2. Parse clean -- every .rb file analyzed, ZERO parse errors: real
     world Ruby parses with the pinned grammar (kill-switch criterion).
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver realism -- exactly 94 imports edges, all of them INSIDE
     the corpus, 61 distinct target files: the require_relative web of
     the gem (sibling/child channels; sequel's requires are a DAG, so
     OC003 stays silent -- the recorded contrast with the Rust corpus
     whose module cycles are legal). Internal-consistency criterion:
     OC006's verdict set is EXACTLY the zero-in-degree files minus the
     runner-collected NAME conventions minus the entry claims (measured
     live from the graph + report): sequel loads most extension/adapter
     files DYNAMICALLY (Sequel.extension / plugin autoloads are runtime
     requires -- the metaprogramming boundary, docs/LANGUAGES.md Ruby
     section 2/8), so the honest zero-in-degree count is large and the
     over-report direction is the accepted one.
  5. Rule honesty -- the measured rule counts are pinned: OC001 33,
     OC002 3, OC004 55, OC006 219, OC008 109, OC011 1. OC003 is zero
     (a DAG), OC005 empty by construction (the whole-file claim). The
     supply tier joins the pin: ZERO manifests in the vendor_ruby
     install tree (Gemfile/Gemfile.lock are repo-root data, not install
     data -- the manifest tier is pinned by the lang gate instead), so
     OC010 stays silent; OC007/OC009 stay silent (no profile, no
     snapshot).
  6. Wall budget -- full check under 60 s single-core (measured at pin
     time).

Usage:
  python3 scripts/acceptance/ruby_corpus_gate.py --oc build/default/oc \
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

DEB_NAME = "ruby-sequel_5.63.0-2_all.deb"
DEB_SHA256 = "c2ec42e71d5538c9438cbd1faeee755653fe5eda70688887c76b297dd0b0567c"
CORPUS_REL = os.path.join("usr", "lib", "ruby", "vendor_ruby")
RUBY_FILES = 280
# sha256 over the sorted repo-relative .rb paths + contents (measured at
# pin time; byte-deterministic because the corpus itself is pinned).
CORPUS_DIGEST = "395923ce6d7f1e7afd496d3499a3be79264349d193d85c9389bb1f5c88073e31"
EXPECTED_COUNTS = {"OC001": 33, "OC002": 3, "OC004": 55, "OC006": 219, "OC008": 109,
                   "OC011": 1}
EXPECTED_EDGES = 94
EXPECTED_TARGETS = 61
EXPECTED_MANIFESTS = 0
WALL_BUDGET_S = 60.0


def corpus_digest(root, base):
    # paths are hashed RELATIVE to the extraction root, so the digest is
    # independent of where work/ lives on the analysis host
    h = hashlib.sha256()
    paths = []
    for r, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(".rb"):
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
    extracted = os.path.join(work, "sequel-root")
    corpus_root = os.path.join(extracted, CORPUS_REL)
    if not os.path.exists(corpus_root):
        print(f"hint: provision {work}/{DEB_NAME} (sha256 {DEB_SHA256}) and extract with "
              f"`dpkg -x {DEB_NAME} {extracted}`; the gate never downloads anything "
              "(the analysis universe is the pinned archive).")
        sys.exit(2)
    if not os.path.exists(oc):
        sys.exit(f"missing: {oc}")

    tmp = "/tmp/omnicheck_ruby_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # --- criterion 1: corpus integrity
    deb_ok = os.path.exists(deb) and sha256_file(deb) == DEB_SHA256
    digest, n_rb = corpus_digest(corpus_root, extracted)
    digest_ok = digest == CORPUS_DIGEST and n_rb == RUBY_FILES
    crit1 = deb_ok and digest_ok
    print(f"criterion 1 (corpus integrity: deb sha256 + extraction digest {digest[:16]}..., "
          f"{n_rb} .rb files) -> {'PASS' if crit1 else 'FAIL'}")

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
    crit2 = analyzed == RUBY_FILES and parse_errs == 0 and err_nodes == 0 \
        and "ruby" in (meta.get("packs") or [])
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
    outside = [1 for a, b in edges if "/vendor_ruby/" not in a or "/vendor_ruby/" not in b]
    targets = set(b for _, b in edges)
    import collections
    ind = collections.Counter(b for _, b in edges)
    zero = set(p for p in nodes.values() if ind[p] == 0)

    def tests_convention(p):
        rel = p.split("/vendor_ruby/", 1)[1]
        base = rel.rsplit("/", 1)[-1]
        if not base.endswith(".rb"):
            return False
        stem = base[:-3]
        return stem.startswith("test_") or stem.endswith("_test") or stem.endswith("_spec")

    conv = sum(1 for p in zero if tests_convention(p))

    def entry_claim(p):
        # the pack entry-point globs (bin/*, exe/*) + the shebang hint,
        # measured the way the tool claims entries
        rel = p.split("/vendor_ruby/", 1)[1]
        if rel.startswith("bin/") or rel.startswith("exe/"):
            return True
        try:
            with open(p, "rb") as fh:
                head = fh.read(2)
            return head == b"#!"
        except OSError:
            return False

    entries = sum(1 for p in zero if entry_claim(p))
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
             and (supply.get("manifests_scanned") or 0) == EXPECTED_MANIFESTS
             and supply.get("osv_snapshot_date") is None
             and counts.get("OC007", 0) == 0
             and counts.get("OC009", 0) == 0)
    print(f"criterion 5 (rule honesty: {counts}; manifests={supply.get('manifests_scanned')}, "
          f"OC010={counts.get('OC010', 0)} -- the install tree carries no Gemfile tier) "
          f"-> {'PASS' if crit5 else 'FAIL'}")

    # --- criterion 6: wall budget
    best = min(walls)
    crit6 = best < WALL_BUDGET_S
    print(f"criterion 6 (wall budget: best {best:.2f}s < {WALL_BUDGET_S:.0f}s single-core) -> "
          f"{'PASS' if crit6 else 'FAIL'}")

    all_crits = {
        "1_corpus_integrity": {"pass": crit1, "deb_sha256": deb_ok,
                               "digest": digest, "rb_files": n_rb},
        "2_parse_clean": {"pass": crit2, "analyzed": analyzed, "parse_errors": parse_errs,
                          "error_nodes": err_nodes},
        "3_determinism": {"pass": crit3, "runs": args.runs, "distinct_sha256": len(set(hashes))},
        "4_resolver_realism": {"pass": crit4, "edges": len(edges), "targets": len(targets),
                               "outside_edges": len(outside), "zero_in_degree": len(zero),
                               "tests_convention": conv, "entries": entries,
                               "oc006_expected": expected_oc006, "oc006_measured": len(oc006_files)},
        "5_rule_honesty": {"pass": crit5, "counts": counts, "supply": supply,
                           "note": "OC003=0: sequel's require_relative web is a DAG (the "
                                   "recorded contrast with the Rust corpus). OC006=219: sequel "
                                   "loads most extension/adapter files DYNAMICALLY "
                                   "(Sequel.extension / plugin autoloads are runtime requires "
                                   "-- the metaprogramming boundary), so the honest "
                                   "zero-in-degree count is large; the over-report direction "
                                   "is the accepted one. OC005 is empty by construction (the "
                                   "whole-file require claim)."},
        "6_wall_budget": {"pass": crit6, "best_s": round(best, 2), "budget_s": WALL_BUDGET_S},
    }
    all_pass = all(c["pass"] for c in all_crits.values())
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "Ruby reference corpus (SPRINT-16): Debian ruby-sequel 5.63.0-2, "
                "280 files",
        "date_executed": time.strftime("%Y-%m-%d"),
        "deb": DEB_NAME,
        "deb_sha256": DEB_SHA256,
        "corpus_digest": CORPUS_DIGEST,
        "contract": "docs/LANGUAGES.md (Ruby section)",
        "grammar_pin": "tree-sitter/tree-sitter-ruby v0.23.1",
        "criteria": all_crits,
        "all_pass": all_pass,
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "ruby_corpus_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
