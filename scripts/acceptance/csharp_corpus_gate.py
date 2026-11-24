#!/usr/bin/env python3
"""C# reference-corpus gate (SPRINT-13, recipe step 8 -- the m2/m3
pattern applied to a REAL C# corpus).

Pinned corpus: Debian source package `mono` version
6.8.0.105+dfsg -- the orig tarball ships the complete Mono class-
library sources. The gate analyzes the 26 mcs/class libraries of >= 40
product .cs files that parse with ZERO errors under the pinned grammar
at pin time (System.Configuration, System.IdentityModel, System.Design,
WindowsBase, Facades, Mono.CodeContracts, ...): 3,702 real-world,
actively-used C# files. The tarball is pinned by sha256 and lives in
work/; extraction is verified by a corpus digest (sha256 over the
sorted csharp-pin-relative paths + contents). No github access needed
-- the Debian archive ecosystem is reachable where the git protocol is
not.

Criteria (all measured facts at pin time, byte-deterministic):

  1. Corpus integrity -- the pinned tarball hash matches, the
     extraction is present, and the corpus digest matches the pin.
  2. Parse clean -- every one of the 3,702 .cs files analyzed with
     ZERO parse errors; the two .js AJAX resource files that share the
     tree are analyzed by the JS pack (3,704 analyzed total -- counted,
     not silent); mono's dotted single-directory namespace layout
     (System.Configuration.Internal/) is the corpus discovery that
     amended the resolver to the documented two-convention ladder.
  3. Determinism -- N consecutive reports byte-identical.
  4. Resolver realism -- exactly 89,313 imports edges, ALL of them
     INSIDE the corpus (cross-assembly usings and SDK framework
     namespaces contribute none -- the resolver never guesses), 1,959
     distinct target files.
  5. Rule honesty -- OC003 == 13: REAL import cycles exist in C#
     (compile-time legal within one assembly, unlike Go), the exact
     point of resolver v1-csharp. OC005 == 0 (the import-driven marking
     surface). OC007/OC009/OC010 silent (no coverage, no snapshot, no
     manifests inside the mcs/class trees). The remaining rule counts
     are pinned at their measured values.
  6. Wall budget -- full check under 60 s single-core.

Usage:
  python3 scripts/acceptance/csharp_corpus_gate.py --oc build/default/oc \
      [--runs 3] [--verdict PATH]

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

TAR_NAME = "mono_6.8.0.105+dfsg.orig.tar.xz"
TAR_SHA256 = "6cde75aefc19c55c4dcdae7f22a1f67bdc30f47e41079054bebafeaee3c4d900"
CORPUS_REL = os.path.join("mcs", "class")
# the 26 mcs/class libraries of >= 40 product .cs files that parse with
# ZERO errors under the pinned grammar (measured at pin time; the list
# IS the selection rule made explicit)
LIBS = [
    "System.EnterpriseServices", "System.ServiceModel.Discovery", "RabbitMQ.Client",
    "System.DirectoryServices", "System.Windows.Forms.DataVisualization",
    "Microsoft.Build.Tasks", "System.Web.Extensions", "Mono.WebBrowser",
    "System.Configuration", "System.IdentityModel", "System.ComponentModel.Composition.4.5",
    "System.Web.DynamicData", "WindowsBase", "Facades", "System.Design",
    "Mono.CodeContracts", "System.Transactions", "Commons.Xml.Relaxng",
    "ICSharpCode.SharpZipLib", "System.Runtime.Serialization", "System.Messaging",
    "System.Management", "Mono.Posix", "Microsoft.Build.Framework", "System.Data.Services",
    "System.DirectoryServices.Protocols",
]
CS_FILES = 3702
FILES_ANALYZED = 3704  # 3,702 .cs + 2 .js AJAX resource files (JS pack)
# sha256 over the sorted repo-relative paths + contents of the
# pinned libraries (m4 corpus-digest pattern, the java gate's own
# repo-relative convention)
CORPUS_DIGEST = "8a3146f15425cbc2467e11b7178d0980ab2a5e48d9bab03ac93be6f8265b6a20"
EXPECTED_COUNTS = {"OC001": 207, "OC002": 315, "OC003": 13, "OC004": 158, "OC006": 1738,
                   "OC008": 1895, "OC011": 4}
EXPECTED_EDGES = 89313
EXPECTED_TARGETS = 1959
WALL_BUDGET_S = 60.0


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def corpus_digest(root, repo):
    """sha256 over the sorted csharp-pin-relative paths + contents of
    the PINNED LIBRARIES (m4 corpus-digest pattern) -- pins the analyzed
    bytes, not just the archive. csharp-pin-relative so the digest is
    machine-independent."""
    files = []
    for lib in LIBS:
        for dp, _, fns in os.walk(os.path.join(root, lib)):
            for fn in fns:
                if fn.endswith(".cs"):
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
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--verdict", default=None, help="verdict JSON path")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    oc = os.path.abspath(args.oc)
    packs = os.path.abspath(args.packs or os.path.join(repo, "packs"))
    tar = os.path.join(repo, "work", TAR_NAME)
    extract = os.path.join(repo, "work", "csharp-pin")
    corpus = os.path.join(extract, CORPUS_REL)
    if not os.path.isfile(oc):
        sys.exit(f"oc binary not found: {oc}")

    tmp = "/tmp/omnicheck_csharp_corpus_gate"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []

    # Criterion 1: corpus integrity.
    tar_ok = os.path.isfile(tar) and sha256_file(tar) == TAR_SHA256
    if tar_ok and not os.path.isdir(corpus):
        subprocess.run(["tar", "xJf", tar, "-C", extract, "--strip-components", "1"],
                       check=False)
    roots = [os.path.join(corpus, lib) for lib in LIBS]
    missing = [r for r in roots if not os.path.isdir(r)]
    digest, n_files = ("", 0)
    if not missing:
        digest, n_files = corpus_digest(corpus, repo)
    digest_ok = digest == CORPUS_DIGEST and n_files == CS_FILES and not missing
    crit1 = tar_ok and digest_ok

    if not crit1:
        print(f"criterion 1 (corpus integrity): tar_ok={tar_ok} digest={digest[:16]} "
              f"files={n_files} missing={missing[:3]} -> FAIL")
        print("hint: provision work/%s (sha256 %s...) and re-run; the gate never "
              "downloads silently" % (TAR_NAME, TAR_SHA256[:16]))
        verdict = {"schema": "omnicheck.evidence/v0",
                   "gate": "C# reference corpus (mono 6.8.0.105+dfsg, SPRINT-13)",
                   "criteria": {"1_corpus_integrity": {"pass": False}}, "all_pass": False}
        out = args.verdict or os.path.join(repo, "docs", "evidence",
                                           "csharp_corpus_gate_verdict.json")
        with open(out, "w") as fh:
            json.dump(verdict, fh, indent=2)
            fh.write("\n")
        sys.exit(1)

    # Criterion 2 + 6: parse clean within budget.
    out0 = f"{tmp}/run0.json"
    t0 = time.monotonic()
    p = subprocess.run(wrap + [oc, "check", *roots, "--packs-dir", packs, "--format", "json"],
                       stdout=open(out0, "wb"), stderr=subprocess.DEVNULL)
    wall = time.monotonic() - t0
    with open(out0) as fh:
        report = json.load(fh)
    meta = report.get("meta", {})
    analyzed = meta.get("files_analyzed", 0)
    parse_errs = meta.get("files_with_parse_errors", 1)
    crit2 = (analyzed == FILES_ANALYZED and parse_errs == 0
             and "csharp" in (meta.get("packs") or []))

    # Criterion 3: determinism.
    hashes = [sha256_file(out0)]
    for i in range(1, args.runs):
        out = f"{tmp}/run{i}.json"
        subprocess.run(wrap + [oc, "check", *roots, "--packs-dir", packs, "--format", "json"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
        hashes.append(sha256_file(out))
    crit3 = len(set(hashes)) == 1

    # Rule counts (criterion 5).
    counts = {}
    for f in report.get("findings", []):
        counts[f.get("rule", "")] = counts.get(f.get("rule", ""), 0) + 1
    counts_pinned = {k: counts.get(k, 0) for k in EXPECTED_COUNTS} == EXPECTED_COUNTS
    supply = meta.get("supply") or {}
    # no manifests inside the mcs/class trees: the supply block may be
    # absent from the meta entirely -- absence IS zero here, honestly
    manifests = supply.get("manifests_scanned", 0)
    inputless_silent = (counts.get("OC007", 0) == 0 and counts.get("OC009", 0) == 0
                        and counts.get("OC010", 0) == 0 and counts.get("OC005", 0) == 0
                        and manifests == 0)

    # Criterion 4: resolver realism over the graph.
    graph_path = f"{tmp}/graph.json"
    with open(graph_path, "wb") as fh:
        subprocess.run(wrap + [oc, "graph", *roots, "--packs-dir", packs],
                       stdout=fh, stderr=subprocess.DEVNULL)
    with open(graph_path) as fh:
        graph = json.load(fh)
    path_by_id = {n.get("id"): n.get("path", "") for n in graph.get("nodes", [])
                  if n.get("kind") == "file"}
    edges = [e for e in graph.get("edges", []) if e.get("kind") == "imports"]
    targets = set()
    outside = 0
    corpus_prefix = os.path.join(repo, "work", "csharp-pin", "mcs", "class")
    for e in edges:
        tp = path_by_id.get(e.get("to"), "")
        targets.add(tp)
        if not tp.startswith(corpus_prefix):
            outside += 1
    crit4 = (len(edges) == EXPECTED_EDGES and len(targets) == EXPECTED_TARGETS
             and outside == 0)
    crit5 = counts_pinned and inputless_silent
    crit6 = wall < WALL_BUDGET_S

    print(f"corpus: {corpus} ({len(LIBS)} libraries)")
    print(f"criterion 1 (corpus integrity: tarball sha256 + digest pin, {n_files} files): "
          f"-> {'PASS' if crit1 else 'FAIL'}")
    print(f"criterion 2 (parse clean): analyzed={analyzed} (expect {FILES_ANALYZED}: "
          f"{CS_FILES} .cs + 2 .js) parse_errors={parse_errs} "
          f"-> {'PASS' if crit2 else 'FAIL'}")
    print(f"criterion 3 (byte-identical, {args.runs} runs): distinct={len(set(hashes))} "
          f"-> {'PASS' if crit3 else 'FAIL'}")
    print(f"criterion 4 (resolver realism): edges={len(edges)} (expect {EXPECTED_EDGES}), "
          f"target_files={len(targets)} (expect {EXPECTED_TARGETS}), outside_corpus={outside} "
          f"-> {'PASS' if crit4 else 'FAIL'}")
    print(f"criterion 5 (rule honesty: counts pinned, OC003=13 REAL C# cycles, "
          f"OC005/OC007/OC009/OC010 inputless-silent): counts={counts} "
          f"-> {'PASS' if crit5 else 'FAIL'}")
    print(f"criterion 6 (wall {wall:.2f}s < {WALL_BUDGET_S}s, single-core={bool(wrap)}) "
          f"-> {'PASS' if crit6 else 'FAIL'}")

    all_crits = {
        "1_corpus_integrity": {"pass": crit1, "tar_sha256": TAR_SHA256,
                               "corpus_digest": digest, "cs_files": n_files},
        "2_parse_clean": {"pass": crit2, "files_analyzed": analyzed,
                          "files_with_parse_errors": parse_errs, "packs": meta.get("packs")},
        "3_byte_identical_reports": {"pass": crit3, "runs": args.runs,
                                     "distinct_sha256": len(set(hashes))},
        "4_resolver_realism": {"pass": crit4, "imports_edges": len(edges),
                               "distinct_target_files": len(targets),
                               "outside_corpus": outside},
        "5_rule_honesty": {"pass": crit5, "counts": counts, "counts_pinned": EXPECTED_COUNTS,
                           "oc003_real_cycles": counts.get("OC003", 0),
                           "manifests_scanned": manifests},
        "6_wall_budget": {"pass": crit6, "wall_s": round(wall, 2), "budget_s": WALL_BUDGET_S},
    }
    verdict = {
        "schema": "omnicheck.evidence/v0",
        "gate": "C# reference corpus (mono 6.8.0.105+dfsg, 26 mcs/class libraries)",
        "date_executed": time.strftime("%Y-%m-%d"),
        "corpus": corpus,
        "corpus_digest": digest,
        "provenance": "Debian mono orig tarball pinned by sha256 in work/; the gate "
                      "analyzes the 26 libraries of >= 40 product .cs files that parse "
                      "with ZERO errors under the pinned grammar (measured at pin time); "
                      "no network fetch inside the gate -- a missing or mismatched "
                      "archive fails loudly",
        "criteria": all_crits,
        "all_pass": all(c["pass"] for c in all_crits.values()),
    }
    out = args.verdict or os.path.join(repo, "docs", "evidence", "csharp_corpus_gate_verdict.json")
    with open(out, "w") as fh:
        json.dump(verdict, fh, indent=2)
        fh.write("\n")
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
