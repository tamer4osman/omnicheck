#!/usr/bin/env python3
"""Go reference-corpus gate (SPRINT-10, recipe step 8 -- the m2/m3
pattern applied to a REAL Go corpus).

Pinned corpus: Debian source package `golang-golang-x-net-dev` version
1:0.27.0-2 (upstream golang.org/x/net v0.27.0, dfsg repack) -- a
real-world, actively maintained Go library: 676 .go files across 24
packages (bpf, html, http2, idna, websocket, ...), GOPATH-style layout
under usr/share/gocode/src/golang.org/x/net. The .deb is pinned by
sha256 and lives in work/; extraction is verified by a corpus digest
(sha256 over the sorted .go paths + contents). No github access needed
-- the archive ecosystem is reachable where the git protocol is not.

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned .deb hash matches, the extraction
     is present, and the corpus digest matches the pin.
  2. Parse clean -- every .go file analyzed, ZERO parse errors: real
     world Go parses with the pinned grammar (kill-switch criterion).
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver realism -- exactly 11,719 imports edges, all of them
     INSIDE the corpus (stdlib and remote specifiers contribute none),
     no _test.go file is ever an edge target (Go compiles an import
     without test files), and the whole-package fan-out is visible
     (distinct target files = 377).
  5. Rule honesty -- OC003 == 0: import cycles are compile errors in
     Go, so a resolver that fabricated even one SCC would be lying;
     this invariant forced the fan-out's test-file exclusion (the
     directory model alone produced four false SCCs here). OC005 == 0
     (the package-wide fail-open empty verdict set, at scale). The
     remaining rule counts are pinned at their measured values.
     SPRINT-11: the supply tier joins the pin -- exactly ONE manifest
     (the module's go.mod; the dfsg repack strips the upstream LICENSE
     file from the source tree, so OC010 fires exactly once, the honest
     warn; OC007/OC009 stay silent -- no profile, no snapshot passed).
  6. Wall budget -- full check under 60 s single-core.

Usage:
  python3 scripts/acceptance/go_corpus_gate.py --oc build/default/oc \
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

DEB_NAME = "golang-x-net-0.27.0.deb"
DEB_SHA256 = "1f7576dfed2b293220186dd8a9c5c97b0858334ad67a700eb8569252b97ff395"
CORPUS_REL = os.path.join("usr", "share", "gocode", "src")
GO_FILES = 676
# SPRINT-14 (docs/LANGUAGES.md §Go.8): build constraints are evaluated
# under the go pack's DECLARED build context (linux/amd64/gc/cgo=false).
# 237 of the 676 corpus files carry GOOS/GOARCH/cgo/release-tag constraints
# that are false there; they leave the analyzed set (what go build does)
# and the report meta counts them. The analyzed remainder is pinned, and
# the EXCLUSION COUNT is pinned -- excluded, never silent.
EXPECTED_ANALYZED = 439
EXPECTED_EXCLUDED = 237
# sha256 over the sorted repo-relative .go paths + contents (measured at
# pin time; byte-deterministic because the corpus itself is pinned).
CORPUS_DIGEST = "508979551c7968d81d49f39d91b32948e47d66437aa833022d5ea322a9b82273"
EXPECTED_COUNTS = {"OC001": 195, "OC002": 141, "OC004": 135, "OC006": 39, "OC008": 854,
                   "OC011": 3}
# SPRINT-11: the go pack enables OC007/OC009/OC010. On this corpus the
# inputless ones (OC007 coverage, OC009 OSV) never fire; OC010 fires
# exactly once -- the module's go.mod carries no licence field and the
# dfsg repack strips the upstream LICENSE from the source tree (the
# copyright lives in usr/share/doc, outside the analyzed src/ root).
EXPECTED_OC010 = 1
EXPECTED_MANIFESTS = 1
EXPECTED_EDGES = 11719
EXPECTED_TARGETS = 377
WALL_BUDGET_S = 60.0


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def corpus_digest(root, repo):
    """sha256 over the sorted repo-relative .go paths + contents (m4
    corpus-digest pattern) -- pins the analyzed bytes, not just the
    archive. Repo-relative so the digest is machine-independent."""
    files = []
    for dp, _, fns in os.walk(root):
        for fn in fns:
            if fn.endswith(".go"):
                files.append(os.path.join(dp, fn))
    h = hashlib.sha256()
    for p in sorted(files):
        h.update(os.path.relpath(p, repo).encode())
        with open(p, "rb") as fh:
            h.update(fh.read())
    return h.hexdigest(), len(files)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--packs", default=None, help="packs dir (default: repo packs/)")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    packs = os.path.abspath(args.packs or os.path.join(repo, "packs"))
    deb = os.path.join(repo, "work", DEB_NAME)
    extract = os.path.join(repo, "work", "go-pin")
    corpus = os.path.join(extract, CORPUS_REL)
    if not os.path.isfile(oc):
        sys.exit(f"oc binary not found: {oc}")

    tmp = "/tmp/omnicheck_go_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # Criterion 1: corpus integrity.
    deb_ok = os.path.isfile(deb) and sha256_file(deb) == DEB_SHA256
    if deb_ok and not os.path.isdir(corpus):
        subprocess.run(["dpkg", "-x", deb, extract], check=False)
    digest, n_files = ("", 0)
    if os.path.isdir(corpus):
        digest, n_files = corpus_digest(corpus, repo)
    digest_ok = digest == CORPUS_DIGEST and n_files == GO_FILES
    crit1 = deb_ok and digest_ok

    if not crit1:
        print(f"criterion 1 (corpus integrity): deb_ok={deb_ok} digest={digest[:16]} "
              f"files={n_files} -> FAIL")
        print("hint: provision work/%s (sha256 %s...) and re-run; the gate never "
              "downloads silently" % (DEB_NAME, DEB_SHA256[:16]))
        verdict = {"schema": "omnicheck.evidence/v0",
                   "gate": "Go reference corpus (golang.org/x/net, SPRINT-10)",
                   "criteria": {"1_corpus_integrity": {"pass": False}}, "all_pass": False}
        out = args.verdict or os.path.join(repo, "docs", "evidence", "go_corpus_gate_verdict.json")
        with open(out, "w") as fh:
            json.dump(verdict, fh, indent=2)
            fh.write("\n")
        sys.exit(1)

    # Criterion 2 + 6: parse clean within budget.
    out0 = f"{tmp}/run0.json"
    t0 = time.monotonic()
    p = subprocess.run(wrap + [oc, "check", corpus, "--packs-dir", packs, "--format", "json"],
                       stdout=open(out0, "wb"), stderr=subprocess.DEVNULL)
    wall = time.monotonic() - t0
    with open(out0) as fh:
        report = json.load(fh)
    meta = report.get("meta", {})
    analyzed = meta.get("files_analyzed", 0)
    excluded = meta.get("files_excluded_build_tag", -1)
    parse_errs = meta.get("files_with_parse_errors", 1)
    crit2 = (analyzed == EXPECTED_ANALYZED and excluded == EXPECTED_EXCLUDED
             and parse_errs == 0 and "go" in (meta.get("packs") or []))

    # Criterion 3: determinism.
    hashes = [sha256_file(out0)]
    for i in range(1, args.runs):
        out = f"{tmp}/run{i}.json"
        subprocess.run(wrap + [oc, "check", corpus, "--packs-dir", packs, "--format", "json"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
        hashes.append(sha256_file(out))
    crit3 = len(set(hashes)) == 1

    # Rule counts (criterion 5).
    counts = {}
    oc010_files = []
    for f in report.get("findings", []):
        rule = f.get("rule", "")
        counts[rule] = counts.get(rule, 0) + 1
        if rule == "OC010":
            oc010_files.append(os.path.basename(f.get("file", "")))
    oc003_zero = counts.get("OC003", 0) == 0
    oc005_zero = counts.get("OC005", 0) == 0
    counts_pinned = {k: counts.get(k, 0) for k in EXPECTED_COUNTS} == EXPECTED_COUNTS
    supply = meta.get("supply", {})
    oc010_ok = (counts.get("OC010", 0) == EXPECTED_OC010
                and oc010_files == ["go.mod"]
                and supply.get("manifests_scanned", -1) == EXPECTED_MANIFESTS)
    oc007_009_silent = counts.get("OC007", 0) == 0 and counts.get("OC009", 0) == 0

    # Criterion 4: resolver realism over the graph.
    graph_path = f"{tmp}/graph.json"
    with open(graph_path, "wb") as fh:
        subprocess.run(wrap + [oc, "graph", corpus, "--packs-dir", packs],
                       stdout=fh, stderr=subprocess.DEVNULL)
    with open(graph_path) as fh:
        graph = json.load(fh)
    path_by_id = {n.get("id"): n.get("path", "") for n in graph.get("nodes", [])
                  if n.get("kind") == "file"}
    edges = [e for e in graph.get("edges", []) if e.get("kind") == "imports"]
    targets = set()
    test_targets = 0
    outside = 0
    for e in edges:
        tp = path_by_id.get(e.get("to"), "")
        targets.add(tp)
        if os.path.basename(tp).endswith("_test.go"):
            test_targets += 1
        if "/golang.org/x/net/" not in tp:
            outside += 1
    crit4 = (len(edges) == EXPECTED_EDGES and len(targets) == EXPECTED_TARGETS
             and test_targets == 0 and outside == 0)
    crit5 = oc003_zero and oc005_zero and counts_pinned and oc010_ok and oc007_009_silent
    crit6 = wall < WALL_BUDGET_S

    print(f"corpus: {corpus}")
    print(f"criterion 1 (corpus integrity: deb sha256 + digest pin, {n_files} files): "
          f"-> {'PASS' if crit1 else 'FAIL'}")
    print(f"criterion 2 (parse clean, declared-context exclusions): analyzed={analyzed} "
          f"(expect {EXPECTED_ANALYZED}), build_tag_excluded={excluded} "
          f"(expect {EXPECTED_EXCLUDED}), parse_errors={parse_errs} "
          f"-> {'PASS' if crit2 else 'FAIL'}")
    print(f"criterion 3 (byte-identical, {args.runs} runs): distinct={len(set(hashes))} "
          f"-> {'PASS' if crit3 else 'FAIL'}")
    print(f"criterion 4 (resolver realism): edges={len(edges)} (expect {EXPECTED_EDGES}), "
          f"target_files={len(targets)} (expect {EXPECTED_TARGETS}), "
          f"test_file_targets={test_targets}, outside_corpus={outside} "
          f"-> {'PASS' if crit4 else 'FAIL'}")
    print(f"criterion 5 (rule honesty: OC003=0, OC005=0, counts pinned, OC010="
          f"{counts.get('OC010', 0)} on {oc010_files}, manifests="
          f"{supply.get('manifests_scanned', -1)}, OC007/OC009 silent): counts={counts} "
          f"-> {'PASS' if crit5 else 'FAIL'}")
    print(f"criterion 6 (wall {wall:.2f}s < {WALL_BUDGET_S}s, single-core={bool(wrap)}) "
          f"-> {'PASS' if crit6 else 'FAIL'}")

    all_crits = {
        "1_corpus_integrity": {"pass": crit1, "deb_sha256": DEB_SHA256,
                               "corpus_digest": digest, "go_files": n_files},
        "2_parse_clean": {"pass": crit2, "files_analyzed": analyzed,
                          "build_tag_excluded": excluded,
                          "files_with_parse_errors": parse_errs, "packs": meta.get("packs")},
        "3_byte_identical_reports": {"pass": crit3, "runs": args.runs,
                                     "distinct_sha256": len(set(hashes))},
        "4_resolver_realism": {"pass": crit4, "imports_edges": len(edges),
                               "distinct_target_files": len(targets),
                               "test_file_targets": test_targets, "outside_corpus": outside},
        "5_rule_honesty": {"pass": crit5, "oc003_findings": counts.get("OC003", 0),
                           "oc005_findings": counts.get("OC005", 0),
                           "counts": counts, "counts_pinned": EXPECTED_COUNTS,
                           "oc010_findings": counts.get("OC010", 0),
                           "oc010_files": oc010_files,
                           "manifests_scanned": supply.get("manifests_scanned"),
                           "osv_snapshot_date": supply.get("osv_snapshot_date")},
        "6_wall_budget": {"pass": crit6, "wall_s": round(wall, 2), "budget_s": WALL_BUDGET_S},
    }
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "Go reference corpus (golang.org/x/net v0.27.0, Debian 1:0.27.0-2)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "corpus": corpus,
        "corpus_digest": digest,
        "provenance": "Debian source package (dfsg) pinned by sha256 in work/; no "
                      "network fetch inside the gate -- a missing or mismatched "
                      "archive fails loudly",
        "criteria": all_crits,
        "all_pass": all(c["pass"] for c in all_crits.values()),
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "go_corpus_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
