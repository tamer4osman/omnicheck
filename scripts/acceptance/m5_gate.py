#!/usr/bin/env python3
"""M5 GA gate acceptance harness (plan Table 9 / P4; formal gate date
2027-12-31 by the plan's milestone cadence -- M1 2026-12-31, M2
2027-02-28, M3 2027-05-31, M4 2027-09-30). This rehearsal executes the
GA criteria early (2026-11-19) like every prior gate; the formal date
and its re-run obligation stay in force.

Criteria:

  1. Determinism audit on the FULL corpus -- scripts/acceptance/
     determinism_audit.py regenerates the 10k corpus and scans it with
     every available build cell (gcc/clang x Release/Sanitize), twice
     each; ALL report digests must be identical. Re-affirms the perf
     budgets on Release cells (single-core, taskset-pinned).
  2. Fuzz suite green (WBS 5.1) -- the committed adversarial corpus and
     the seeded mutation harness pass in the Release cell (the sanitize
     cells run the same suite under ASan/UBSan via ctest).
  3. Docs completeness 100 percent (plan Table 3 M5) -- scripts/
     audit_docs.py derives the shipped surface from the code (rules,
     CLI commands, schemas) and requires every item documented.
  4. Packaged binaries + npm shim -- scripts/package.sh builds the
     release tarball with a sha256 manifest and a byte-identical report
     smoke; the npm shim resolves the binary through all paths (OC_BIN,
     bin/native/oc) and forwards exit codes; requires node for the shim
     leg (recorded honestly when absent).

Usage:
  python3 scripts/acceptance/m5_gate.py --oc build/default/oc
  python3 scripts/acceptance/m5_gate.py --skip-audit   # not for a gate

Exit 0 iff every criterion holds; verdict JSON carries the measured
numbers (--verdict, default docs/evidence/m5_gate_verdict.json).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run(cmd, cwd=None, timeout=3600):
    return subprocess.run(cmd, capture_output=True, cwd=cwd, timeout=timeout,
                          text=True)


def check(name, ok, detail):
    status = "PASS" if ok else "FAIL"
    print(f"  [{status}] {name}: {detail}")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", default="build/default/oc")
    ap.add_argument("--verdict", default="docs/evidence/m5_gate_verdict.json")
    ap.add_argument("--skip-audit", action="store_true",
                    help="skip criterion 1 (NEVER for a formal gate run)")
    args = ap.parse_args()
    oc = os.path.abspath(args.oc) if os.path.sep in args.oc else args.oc
    if not os.path.exists(oc):
        oc = os.path.join(REPO_ROOT, args.oc)

    verdict = {"gate": "M5 GA (formal date 2027-12-31; rehearsal executed early)",
               "rehearsal_date": time.strftime("%Y-%m-%d"),
               "criteria": {}, "kpis": {}}
    all_ok = True

    # ------------------------------------------- 1) determinism audit ---
    if args.skip_audit:
        print("Criterion 1: SKIPPED by flag (a formal gate run must not skip)")
        verdict["criteria"]["c1_determinism_audit"] = {
            "pass": False, "skipped": True,
            "note": "skipped via --skip-audit; formal gate runs never skip"}
        all_ok = False
    else:
        print("Criterion 1: determinism audit on the full corpus (4 cells x 2 runs)")
        p1 = run([sys.executable,
                  os.path.join(REPO_ROOT, "scripts", "acceptance",
                               "determinism_audit.py")],
                 timeout=3600)
        ok1 = p1.returncode == 0
        audit = {}
        path = os.path.join(REPO_ROOT, "docs", "evidence",
                            "determinism_audit.json")
        if os.path.exists(path):
            with open(path) as f:
                audit = json.load(f)
        digests = audit.get("distinct_digests")
        budget = audit.get("budget_check", {})
        ok1 = ok1 and digests == 1
        all_ok &= check("full-corpus byte-identity across build cells", ok1,
                        f"{len(audit.get('runs', []))} runs, "
                        f"{digests} distinct digest(s)")
        all_ok &= check("single-core perf budgets re-affirmed",
                        budget.get("sprint_gate_90s", False)
                        and budget.get("m2_target_60s", False),
                        f"release worst {budget.get('worst_release_wall_seconds')}s "
                        f"(gate 90s, target 60s)")
        verdict["criteria"]["c1_determinism_audit"] = {
            "pass": bool(ok1), "audit": audit}

    # ------------------------------------------------ 2) fuzz suite -----
    print("Criterion 2: fuzz suite green (WBS 5.1, Release cell)")
    fuzz_bin = os.path.join(REPO_ROOT, "build", "default", "tests", "test_fuzz")
    p2 = run([fuzz_bin], timeout=600)
    err = p2.stderr
    ok2 = p2.returncode == 0
    all_ok &= check("seeded mutations + adversarial corpus + loaders", ok2,
                    next((line.strip() for line in err.splitlines()
                          if "checks," in line), f"exit {p2.returncode}"))
    fuzz_stats = {line.strip().split(":")[0].replace("  ", ""): line.strip()
                  for line in err.splitlines() if line.strip().startswith("  ")}
    verdict["criteria"]["c2_fuzz_suite"] = {
        "pass": bool(ok2), "summary": err.strip().splitlines()[-1] if err else "",
        "layers": fuzz_stats}

    # ------------------------------------------ 3) docs completeness ---
    print("Criterion 3: docs completeness 100 percent (plan Table 3 M5)")
    p3 = run([sys.executable, os.path.join(REPO_ROOT, "scripts", "audit_docs.py")])
    ok3 = p3.returncode == 0
    all_ok &= check("every rule / CLI command / schema documented", ok3,
                    p3.stdout.strip().splitlines()[0]
                    if p3.stdout.strip() else f"exit {p3.returncode}")
    verdict["criteria"]["c3_docs_completeness"] = {"pass": bool(ok3)}

    # ------------------------------------------------- 4) packaging -----
    print("Criterion 4: packaged binaries + npm shim")
    p4 = run(["bash", os.path.join(REPO_ROOT, "scripts", "package.sh")],
             timeout=600)
    ok4 = p4.returncode == 0
    tarball_line = next((l for l in p4.stdout.splitlines()
                         if l.startswith("packaged:")), "")
    all_ok &= check("release tarball + sha256 manifest + report smoke", ok4,
                    tarball_line.replace("packaged: ", "")
                    or f"exit {p4.returncode}: {p4.stderr.strip()[:200]}")
    verdict["criteria"]["c4_packaging_tarball"] = {
        "pass": bool(ok4), "artifact": tarball_line.replace("packaged: ", "")}

    node = shutil.which("node")
    if not node:
        print("  [SKIP] npm shim verification: node not on PATH (recorded honestly)")
        verdict["criteria"]["c4_npm_shim"] = {
            "pass": False, "skipped": True,
            "note": "node not available; shim not verified in this session"}
        all_ok = False
    else:
        dist = os.path.join(REPO_ROOT, "dist")
        tarballs = sorted(f for f in os.listdir(dist) if f.endswith(".tar.gz")) \
            if os.path.isdir(dist) else []
        smoke_dir = tempfile.mkdtemp(prefix="oc_shim_")
        shim_ok = False
        detail = ""
        if tarballs:
            subprocess.run(["tar", "-C", smoke_dir, "-xzf",
                            os.path.join(dist, tarballs[-1])], check=True)
            pkg_dir = os.path.join(
                smoke_dir, tarballs[-1].replace(".tar.gz", ""))
            npmroot = os.path.join(smoke_dir, "npmroot")
            os.makedirs(os.path.join(npmroot, "bin", "native"))
            shutil.copy(os.path.join(REPO_ROOT, "npm", "bin", "oc-shim.js"),
                        os.path.join(npmroot, "bin", "oc-shim.js"))
            shutil.copy(os.path.join(pkg_dir, "bin", "oc"),
                        os.path.join(npmroot, "bin", "native", "oc"))
            examples = os.path.join(REPO_ROOT, "examples")
            packs = os.path.join(pkg_dir, "packs")
            shim = run([node, os.path.join(npmroot, "bin", "oc-shim.js"),
                        "check", examples, "--packs-dir", packs])
            direct = run([oc, "check", examples, "--packs-dir", packs],
                         cwd=REPO_ROOT)
            # same argument FORM (absolute root both sides) => same bytes
            direct_abs = run([oc, "check", examples,
                              "--packs-dir", os.path.join(REPO_ROOT, "packs")])
            shim_ok = (shim.returncode in (0, 1, 2)
                       and direct_abs.returncode == shim.returncode
                       and shim.stdout == direct_abs.stdout)
            detail = (f"shim exit {shim.returncode}, report "
                      f"{len(shim.stdout)} bytes, "
                      f"identical={shim.stdout == direct_abs.stdout}")
            # OC_BIN resolution path
            env = dict(os.environ, OC_BIN=os.path.join(pkg_dir, "bin", "oc"))
            p_ver = subprocess.run([node, os.path.join(npmroot, "bin",
                                                       "oc-shim.js"),
                                    "--version"], capture_output=True,
                                   text=True, env=env)
            shim_ok &= p_ver.returncode == 0 and "omnicheck" in p_ver.stdout
        else:
            detail = "no tarball found in dist/"
        shutil.rmtree(smoke_dir, ignore_errors=True)
        all_ok &= check("npm shim exec path + OC_BIN path + exit passthrough",
                        shim_ok, detail)
        verdict["criteria"]["c4_npm_shim"] = {"pass": bool(shim_ok),
                                              "detail": detail}

    # ----------------------------------------------------- KPI block ----
    print("KPIs verified at M5 (plan Table 3)")
    pk = run([oc, "packs", "--packs-dir", os.path.join(REPO_ROOT, "packs")],
             cwd=REPO_ROOT)
    packs_json = json.loads(pk.stdout)
    packs_json = packs_json["packs"] if isinstance(packs_json, dict) else packs_json
    n_rules = min(len(p["enabled_passes"]) for p in packs_json
                  if p["id"] in ("ts-js", "python"))
    ok_k = n_rules >= 11
    all_ok &= check("analysis count >= 11 shipped and enabled on both packs",
                    ok_k, f"{n_rules} rules per pack")
    verdict["kpis"]["analysis_count"] = {"pass": bool(ok_k), "per_pack": n_rules}

    docs_ev = os.path.join(REPO_ROOT, "docs", "evidence",
                           "docs_completeness.json")
    if os.path.exists(docs_ev):
        with open(docs_ev) as f:
            d = json.load(f)
        verdict["kpis"]["docs_completeness_pct"] = d.get("coverage_pct")

    verdict["all_pass"] = bool(all_ok)
    verdict["oc_binary"] = oc

    print()
    print(f"M5 GA rehearsal verdict: "
          f"{'PASS on all criteria + KPIs' if all_ok else 'FAIL'}")
    out = os.path.join(REPO_ROOT, args.verdict)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(verdict, f, indent=1, sort_keys=True)
        f.write("\n")
    print("verdict written:", os.path.relpath(out, REPO_ROOT))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
