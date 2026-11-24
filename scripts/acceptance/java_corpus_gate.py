#!/usr/bin/env python3
"""Java reference-corpus gate (SPRINT-11, recipe step -- the m2/m3
pattern applied to a REAL Java corpus).

Pinned corpus: google/gson at tag `gson-parent-2.11.0` (codeload tarball,
sha256-pinned in work/; tag commit 3b7c2ad8ce8dd471449ccbaee6d22f64c9816a38,
verified with `git ls-remote` at adoption time; Apache-2.0) -- a real-world,
actively maintained Java library: 249 .java files, 202 of them under the
Maven source roots gson/src/main/java (116) and gson/src/test/java (86).
The scan roots are the two SOURCE ROOTS, exactly the v1-java contract's
"under any analysis root" model (dotted qualifiers are classpath-relative,
so repo-root scans resolve nothing by design). The tarball is pinned by
sha256 and lives in work/; extraction is verified by a corpus digest
(sha256 over the sorted repo-relative .java paths + contents). The gate
never downloads silently -- a missing or mismatched archive fails loudly.

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned tarball hash matches, the extraction
     is present, and the corpus digest matches the pin.
  2. Parse clean -- every .java file under the source roots analyzed,
     ZERO parse errors: real-world Java parses with the pinned grammar
     (kill-switch criterion; generics, records, switch expressions,
     text blocks and nested classes included).
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver realism -- exactly 820 imports edges, ALL of them inside
     the corpus (JDK, gradle and other external names contribute none;
     the resolver never reads classpath, build files or manifests), 77
     distinct target files, and the wildcard package claims are visible.
  5. Rule honesty -- OC003 == 2: Java allows compile-level import
     cycles, and gson genuinely has them (a 30-file cycle around the
     internal/bind packages and a 2-file cycle) -- a resolver that
     fabricated MORE would be lying, and the two measured SCCs are the
     honest verdict. OC005 == 0 at scale (the empty verdict set: every
     resolved import binds the class name Java's class-file model
     requires, wildcard imports fail-open -- same documented consequence
     as Go, docs/LANGUAGES.md §Java.6). OC006 claims test classes through
     the Surefire runner convention (no *Test.java flood). The remaining
     rule counts are pinned at their measured values.
  6. Wall budget -- full check under 60 s single-core.

Usage:
  python3 scripts/acceptance/java_corpus_gate.py --oc build/default/oc \
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

TGZ_NAME = "gson-parent-2.11.0.tgz"
TGZ_SHA256 = "29eaca60adf107a057ed292e92eb0b01f2d569e858ecfb0306659841181c298f"
TAG = "gson-parent-2.11.0"
TAG_COMMIT = "3b7c2ad8ce8dd471449ccbaee6d22f64c9816a38"
EXTRACT_REL = os.path.join("work", "java-pin", f"gson-{TAG}")
JAVA_FILES = 249
MAIN_ROOT = os.path.join(EXTRACT_REL, "gson", "src", "main", "java")
TEST_ROOT = os.path.join(EXTRACT_REL, "gson", "src", "test", "java")
ANALYZED_FILES = 202
# sha256 over the sorted repo-relative .java paths + contents (measured at
# pin time; byte-deterministic because the corpus itself is pinned).
CORPUS_DIGEST = "d4bce44ace56060d132ae5306e86150078bf74154fb13dd80907e72ce2b5eb4d"
EXPECTED_COUNTS = {"OC001": 29, "OC002": 16, "OC003": 2, "OC004": 16, "OC006": 13,
                   "OC008": 1205}
EXPECTED_EDGES = 820
EXPECTED_TARGETS = 77
WALL_BUDGET_S = 60.0


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def corpus_digest(root, repo):
    """sha256 over the sorted repo-relative .java paths + contents (m4
    corpus-digest pattern) -- pins the analyzed bytes, not just the
    archive. Repo-relative so the digest is machine-independent."""
    files = []
    for dp, _, fns in os.walk(root):
        for fn in fns:
            if fn.endswith(".java"):
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
    tgz = os.path.join(repo, "work", TGZ_NAME)
    extract = os.path.join(repo, EXTRACT_REL)
    main_root = os.path.join(repo, MAIN_ROOT)
    test_root = os.path.join(repo, TEST_ROOT)
    if not os.path.isfile(oc):
        sys.exit(f"oc binary not found: {oc}")

    tmp = "/tmp/omnicheck_java_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # Criterion 1: corpus integrity.
    tgz_ok = os.path.isfile(tgz) and sha256_file(tgz) == TGZ_SHA256
    if tgz_ok and not os.path.isdir(extract):
        os.makedirs(os.path.dirname(extract), exist_ok=True)
        subprocess.run(["tar", "xzf", tgz, "-C", os.path.dirname(extract)], check=False)
    digest, n_files = ("", 0)
    if os.path.isdir(extract):
        digest, n_files = corpus_digest(extract, repo)
    digest_ok = digest == CORPUS_DIGEST and n_files == JAVA_FILES
    crit1 = tgz_ok and digest_ok

    if not crit1:
        print(f"criterion 1 (corpus integrity): tgz_ok={tgz_ok} digest={digest[:16]} "
              f"files={n_files} -> FAIL")
        print("hint: provision work/%s (sha256 %s...) and re-run; the gate never "
              "downloads silently" % (TGZ_NAME, TGZ_SHA256[:16]))
        verdict = {"schema": "omnicheck.evidence/v0",
                   "gate": "Java reference corpus (google/gson, SPRINT-11)",
                   "criteria": {"1_corpus_integrity": {"pass": False}}, "all_pass": False}
        out = args.verdict or os.path.join(repo, "docs", "evidence", "java_corpus_gate_verdict.json")
        with open(out, "w") as fh:
            json.dump(verdict, fh, indent=2)
            fh.write("\n")
        sys.exit(1)

    # Criterion 2 + 6: parse clean within budget. Two roots: the Maven
    # source roots, main first (root order is the resolver's probe order).
    out0 = f"{tmp}/run0.json"
    t0 = time.monotonic()
    p = subprocess.run(wrap + [oc, "check", main_root, test_root, "--packs-dir", packs,
                               "--format", "json"],
                       stdout=open(out0, "wb"), stderr=subprocess.DEVNULL)
    wall = time.monotonic() - t0
    with open(out0) as fh:
        report = json.load(fh)
    meta = report.get("meta", {})
    analyzed = meta.get("files_analyzed", 0)
    parse_errs = meta.get("files_with_parse_errors", 1)
    crit2 = analyzed == ANALYZED_FILES and parse_errs == 0 and "java" in (meta.get("packs") or [])

    # Criterion 3: determinism.
    hashes = [sha256_file(out0)]
    for i in range(1, args.runs):
        out = f"{tmp}/run{i}.json"
        subprocess.run(wrap + [oc, "check", main_root, test_root, "--packs-dir", packs,
                               "--format", "json"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
        hashes.append(sha256_file(out))
    crit3 = len(set(hashes)) == 1

    # Rule counts (criterion 5).
    counts = {}
    for f in report.get("findings", []):
        counts[f.get("rule", "")] = counts.get(f.get("rule", ""), 0) + 1
    oc003 = counts.get("OC003", 0)
    oc005_zero = counts.get("OC005", 0) == 0
    oc006 = counts.get("OC006", 0)
    counts_pinned = {k: counts.get(k, 0) for k in EXPECTED_COUNTS} == EXPECTED_COUNTS
    # The Surefire convention must keep the OC006 verdicts off the runner-
    # collected test classes: no *Test.java file may appear.
    oc006_clean = 0
    for f in report.get("findings", []):
        if f.get("rule") == "OC006" and os.path.basename(f.get("file", "")).endswith(
                ("Test.java", "Tests.java", "TestCase.java")):
            oc006_clean += 1

    # Criterion 4: resolver realism over the graph.
    graph_path = f"{tmp}/graph.json"
    with open(graph_path, "wb") as fh:
        subprocess.run(wrap + [oc, "graph", main_root, test_root, "--packs-dir", packs],
                       stdout=fh, stderr=subprocess.DEVNULL)
    with open(graph_path) as fh:
        graph = json.load(fh)
    path_by_id = {n.get("id"): n.get("path", "") for n in graph.get("nodes", [])
                  if n.get("kind") == "file"}
    edges = [e for e in graph.get("edges", []) if e.get("kind") == "imports"]
    targets = set()
    outside = 0
    for e in edges:
        tp = path_by_id.get(e.get("to"), "")
        targets.add(tp)
        if f"/gson-{TAG}/" not in tp:
            outside += 1
    crit4 = (len(edges) == EXPECTED_EDGES and len(targets) == EXPECTED_TARGETS
             and outside == 0)
    crit5 = (oc003 == EXPECTED_COUNTS["OC003"] and oc005_zero and oc006_clean == 0
             and counts_pinned)
    crit6 = wall < WALL_BUDGET_S

    print(f"corpus: {extract}")
    print(f"criterion 1 (corpus integrity: tarball sha256 + digest pin, {n_files} files): "
          f"-> {'PASS' if crit1 else 'FAIL'}")
    print(f"criterion 2 (parse clean): analyzed={analyzed} parse_errors={parse_errs} "
          f"-> {'PASS' if crit2 else 'FAIL'}")
    print(f"criterion 3 (byte-identical, {args.runs} runs): distinct={len(set(hashes))} "
          f"-> {'PASS' if crit3 else 'FAIL'}")
    print(f"criterion 4 (resolver realism): edges={len(edges)} (expect {EXPECTED_EDGES}), "
          f"target_files={len(targets)} (expect {EXPECTED_TARGETS}), "
          f"outside_corpus={outside} "
          f"-> {'PASS' if crit4 else 'FAIL'}")
    print(f"criterion 5 (rule honesty: OC003={oc003} pinned, OC005=0, "
          f"OC006 test-class flood={oc006_clean}, counts pinned): counts={counts} "
          f"-> {'PASS' if crit5 else 'FAIL'}")
    print(f"criterion 6 (wall {wall:.2f}s < {WALL_BUDGET_S}s, single-core={bool(wrap)}) "
          f"-> {'PASS' if crit6 else 'FAIL'}")

    all_crits = {
        "1_corpus_integrity": {"pass": crit1, "tarball_sha256": TGZ_SHA256,
                               "tag": TAG, "tag_commit": TAG_COMMIT,
                               "corpus_digest": digest, "java_files": n_files},
        "2_parse_clean": {"pass": crit2, "files_analyzed": analyzed,
                          "files_with_parse_errors": parse_errs, "packs": meta.get("packs")},
        "3_byte_identical_reports": {"pass": crit3, "runs": args.runs,
                                     "distinct_sha256": len(set(hashes))},
        "4_resolver_realism": {"pass": crit4, "imports_edges": len(edges),
                               "distinct_target_files": len(targets),
                               "outside_corpus": outside},
        "5_rule_honesty": {"pass": crit5, "oc003_findings": oc003,
                           "oc005_findings": counts.get("OC005", 0),
                           "oc006_test_class_findings": oc006_clean,
                           "counts": counts, "counts_pinned": EXPECTED_COUNTS},
        "6_wall_budget": {"pass": crit6, "wall_s": round(wall, 2), "budget_s": WALL_BUDGET_S},
    }
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "Java reference corpus (google/gson gson-parent-2.11.0, codeload tarball)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "corpus": extract,
        "corpus_digest": digest,
        "provenance": "Codeload tarball of the pinned upstream tag (sha256 in work/), "
                      "Apache-2.0; no network fetch inside the gate -- a missing or "
                      "mismatched archive fails loudly",
        "criteria": all_crits,
        "all_pass": all(c["pass"] for c in all_crits.values()),
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "java_corpus_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
