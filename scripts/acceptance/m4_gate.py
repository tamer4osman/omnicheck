#!/usr/bin/env python3
"""M4 Release Candidate gate acceptance harness (plan Table 9, gate M4
2027-09-30).

Executes the four recorded acceptance criteria plus the M4-verified KPIs
(plan Table 3) and emits a machine-readable verdict:

  1. CRAP leaderboard live on both packs via coverage adapters -- the
     V8 adapter (JS pack) and the coverage.py adapter (Python pack)
     compose against the example corpus; the report carries a
     crap_leaderboard ranked by the integer CRAP composition, with the
     process_order/processOrder ranking proving coverage sensitivity.
  2. Mutation run completes inside the CI budget with a survival
     report -- the committed demo project runs through `oc mutate`:
     pre-flight clean-suite check, operators executed, the designed
     arg-swap survivor present in the survival report, wall time
     recorded against the 10-minute CI budget target (Table 3).
  3. Breaking-change gate demonstrated on a synthetic version bump --
     `oc compat` on the committed v1->v2 fixtures: BREAKING verdict +
     exit 1 for the removed export/function, SAFE verdict + exit 0 for
     the additions-only bump.
  4. Supply-chain findings raised on a seeded fixture -- `oc check`
     with the vendored OSV snapshot: OC009 (pinned vulnerable deps +
     unpinned), OC010 (missing licence) and OC011 (credential patterns)
     all fire on the committed seed.

KPIs checked at M4 (Table 3):
  - Analysis count >= 10 shipped and enabled on both packs.
  - CRAP coverage: every function with complexity >= 10 carries a CRAP
    score in output when coverage is composed.
  - Mutation CI budget: full mutation run under 10 minutes (target;
    measured on the demo project here and re-measured on the reference
    repository near the gate date).

Usage:
  python3 scripts/acceptance/m4_gate.py --oc build/default/oc

Exit code 0 iff every criterion and KPI holds; measured numbers are
printed and collected into the verdict JSON (--verdict out.json).
"""
import argparse
import json
import os
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run(cmd, cwd=None, timeout=600):
    return subprocess.run(cmd, capture_output=True, cwd=cwd, timeout=timeout, text=True)


def load_json(path):
    with open(path) as f:
        return json.load(f)


def check(name, ok, detail):
    status = "PASS" if ok else "FAIL"
    print(f"  [{status}] {name}: {detail}")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", default="build/default/oc", help="path to the oc binary")
    ap.add_argument("--verdict", default="docs/evidence/m4_gate_verdict.json")
    args = ap.parse_args()
    oc = os.path.abspath(args.oc) if os.path.sep in args.oc else args.oc
    packs = os.path.join(REPO_ROOT, "packs")
    fixtures = os.path.join(REPO_ROOT, "tests", "fixtures")

    verdict = {"gate": "M4 Release Candidate", "criteria": {}, "kpis": {}}
    all_ok = True

    # ------------------------------------------------ 1) leaderboard ---
    print("Criterion 1: CRAP leaderboard live on both packs via coverage adapters")
    # The tracked coverage fixtures carry the @@OC_REPO_ROOT@@ placeholder
    # (host-path hygiene, SPRINT-18): the engine never resolves the token
    # -- every consumer substitutes it into a /tmp copy first
    # (fixture_subst() in the test suites). The harness legs do the same
    # here; feeding the raw template makes the V8 url unreadable and the
    # scan exits 2 with an empty report (caught by the SPRINT-20 M5
    # gate-day: the m4 harness leg had never run on a fresh restore).
    def fixture_subst(relpath):
        src = os.path.join(fixtures, relpath)
        dst_dir = os.path.join("/tmp", "oc_m4_fixtures")
        os.makedirs(dst_dir, exist_ok=True)
        dst = os.path.join(dst_dir, os.path.basename(relpath))
        with open(src) as fh:
            text = fh.read()
        with open(dst, "w") as fh:
            fh.write(text.replace("@@OC_REPO_ROOT@@", REPO_ROOT))
        return dst

    cov_v8 = fixture_subst(os.path.join("coverage", "v8.json"))
    cov_py = fixture_subst(os.path.join("coverage", "pycov.json"))
    p1 = run([oc, "check", "examples", "--packs-dir", packs,
              "--coverage", cov_v8, "--coverage", cov_py], cwd=REPO_ROOT)
    r1 = json.loads(p1.stdout)
    lb = r1.get("crap_leaderboard") or []
    cov_meta = r1["meta"]["coverage"]
    ok1 = (
        cov_meta["files_loaded"] == 2
        and cov_meta["files_matched"] == 2
        and cov_meta["functions_measured"] == cov_meta["functions_total"] == 4
        and len(lb) == 4
        and all(
            lb[i]["crap"] >= lb[i + 1]["crap"] for i in range(len(lb) - 1)
        )
        and lb[0]["symbol"] == "process_order"
        and lb[0]["crap"] == 18 * 18 * (100 - 66) // 100 + 18
    )
    all_ok &= check("both adapters compose (V8 + coverage.py)", ok1,
                    f"files_loaded=2, functions {cov_meta['functions_measured']}/"
                    f"{cov_meta['functions_total']} measured")
    # JS pack side and Python pack side separately
    p1js = run([oc, "check", "examples/js", "--packs-dir", packs, "--coverage", cov_v8],
               cwd=REPO_ROOT)
    r1js = json.loads(p1js.stdout)
    ok1js = len(r1js.get("crap_leaderboard") or []) == 2 and \
        r1js["crap_leaderboard"][0]["symbol"] == "processOrder"
    all_ok &= check("JS pack leaderboard (V8 adapter)", ok1js,
                    "top=" + r1js["crap_leaderboard"][0]["symbol"])
    p1py = run([oc, "check", "examples/py", "--packs-dir", packs, "--coverage", cov_py],
               cwd=REPO_ROOT)
    r1py = json.loads(p1py.stdout)
    ok1py = len(r1py.get("crap_leaderboard") or []) == 2 and \
        r1py["crap_leaderboard"][0]["symbol"] == "process_order"
    all_ok &= check("Python pack leaderboard (coverage.py adapter)", ok1py,
                    "top=" + r1py["crap_leaderboard"][0]["symbol"])
    verdict["criteria"]["c1_leaderboard"] = {
        "pass": bool(ok1 and ok1js and ok1py),
        "rows": lb,
        "coverage_meta": cov_meta,
    }

    # ------------------------------------------------ 2) mutation -----
    print("Criterion 2: mutation run completes inside the CI budget with a survival report")
    proj = os.path.join(fixtures, "mutation", "pyproj")
    t0 = time.time()
    p2 = run([oc, "mutate", ".", "--packs-dir", packs,
              "--test-cmd", "python3 test_calc.py", "--timeout", "60"], cwd=proj)
    wall = time.time() - t0
    r2 = json.loads(p2.stdout)
    survivors = [m for m in r2["mutants"] if m["status"] == "survived"]
    ok2 = (
        p2.returncode == 1
        and r2["schema"] == "omnicheck.mutation_report/v0"
        and r2["mutants_total"] == 10
        and r2["killed"] == 9
        and len(survivors) == 1
        and survivors[0]["op"] == "arg-swap"
        and r2["wall_seconds"] < 600.0
    )
    all_ok &= check("mutation run with survival report", ok2,
                    f"{r2['executed']} executed: {r2['killed']} killed, "
                    f"{r2['survived']} survived (arg-swap on pair_sum); "
                    f"wall {r2['wall_seconds']:.2f}s + harness {wall - r2['wall_seconds']:.2f}s")
    all_ok &= check("mutation CI budget (<10 min target, Table 3)", wall < 600.0,
                    f"total {wall:.2f}s vs 600s budget")
    verdict["criteria"]["c2_mutation"] = {
        "pass": bool(ok2 and wall < 600.0),
        "report": r2,
        "harness_wall_seconds": round(wall, 3),
    }

    # ------------------------------------------------ 3) compat gate ---
    print("Criterion 3: breaking-change gate demonstrated on a synthetic version bump")
    p3_break = run([oc, "compat", "--baseline", os.path.join(fixtures, "compat", "v1"),
                    "--current", os.path.join(fixtures, "compat", "v2"),
                    "--packs-dir", packs], cwd=REPO_ROOT)
    r3_break = json.loads(p3_break.stdout)
    kinds = sorted(c["kind"] for c in r3_break["breaking"])
    ok3a = (
        p3_break.returncode == 1
        and r3_break["verdict"] == "BREAKING"
        and kinds == ["removed-export", "removed-function"]
        and r3_break["breaking"][0]["symbol"] == "going_away"
    )
    all_ok &= check("v1 -> v2 is BREAKING (exit 1)", ok3a, f"kinds={kinds}")
    p3_safe = run([oc, "compat", "--baseline", os.path.join(fixtures, "compat", "v1"),
                   "--current", os.path.join(fixtures, "compat", "v2safe"),
                   "--packs-dir", packs], cwd=REPO_ROOT)
    r3_safe = json.loads(p3_safe.stdout)
    ok3b = p3_safe.returncode == 0 and r3_safe["verdict"] == "SAFE" \
        and len(r3_safe["breaking"]) == 0 and len(r3_safe["informational"]) == 2
    all_ok &= check("v1 -> v2safe (additions only) is SAFE (exit 0)", ok3b,
                    "informational=" +
                    str(sorted(c["kind"] for c in r3_safe["informational"])))
    verdict["criteria"]["c3_compat_gate"] = {
        "pass": bool(ok3a and ok3b),
        "breaking_report": r3_break,
        "safe_report": r3_safe,
    }

    # ---------------------------------------- 3b) signature diffs ----
    # SPRINT-17 (B3): the binding policy table end to end on the
    # per-family signature fixture pairs (docs/SPRINT-17.md section 3).
    # Expected BREAKING-kind multisets and informational counts per
    # family; the v0 pairs above must stay untouched on the v1 schema.
    print("Criterion 3b: compat signature diffs on the per-family pairs (SPRINT-17)")
    sig_expect = {
        "py":     (["changed-param-count", "changed-param-type", "param-renamed",
                    "param-reordered"], 1),
        "js":     (["changed-param-count", "param-reordered"], 2),
        "ts":     (["changed-param-type", "param-reordered"], 1),
        "go":     (["changed-param-count", "changed-param-count", "changed-param-type",
                    "param-reordered"], 0),
        "java":   (["changed-param-count", "changed-param-type"], 2),
        "csharp": (["changed-param-count", "changed-param-count", "changed-param-type",
                    "param-renamed"], 2),
        "rust":   (["changed-param-count", "changed-param-type", "param-reordered"], 0),
        "ruby":   (["changed-param-count", "changed-param-count", "param-reordered"], 1),
    }
    sig_reports = {}
    ok3c = True
    for fam, (want_kinds, want_info) in sorted(sig_expect.items()):
        p3s = run([oc, "compat",
                   "--baseline", os.path.join(fixtures, "compat", "sig", fam, "v1"),
                   "--current", os.path.join(fixtures, "compat", "sig", fam, "v2"),
                   "--packs-dir", packs], cwd=REPO_ROOT)
        r3s = json.loads(p3s.stdout)
        got = sorted(c["kind"] for c in r3s["breaking"])
        info = sorted(c["kind"] for c in r3s["informational"])
        fam_ok = (
            p3s.returncode == 1
            and r3s["verdict"] == "BREAKING"
            and r3s["schema"] == "omnicheck.compat_report/v1"
            and got == want_kinds
            and len(info) == want_info
        )
        ok3c = ok3c and fam_ok
        all_ok = all_ok and check(f"sig pair {fam}: BREAKING with the pinned kinds", fam_ok,
                                  f"kinds={got} info={info}")
        sig_reports[fam] = {"kinds": got, "informational": info}
    verdict["criteria"]["c3b_signature_diffs"] = {
        "pass": bool(ok3c),
        "pairs": sig_reports,
    }

    # ------------------------------------------------ 4) supply tier ---
    print("Criterion 4: supply-chain findings raised on a seeded fixture")
    osv = os.path.join(fixtures, "supply", "osv_snapshot.json")
    p4 = run([oc, "check", "tests/fixtures/supply", "--packs-dir", packs,
              "--osv-snapshot", osv], cwd=REPO_ROOT)
    r4 = json.loads(p4.stdout)
    by_rule = {}
    for f in r4["findings"]:
        by_rule.setdefault(f["rule"], []).append(f)
    oc011_sev = {f["severity"] for f in by_rule.get("OC011", [])}
    ok4 = (
        r4["meta"]["supply"]["manifests_scanned"] == 3
        and r4["meta"]["supply"]["osv_snapshot_date"] == "2026-11-19"
        and len(by_rule.get("OC009", [])) == 4      # 3 error + click unpinned warn
        and len(by_rule.get("OC010", [])) == 2      # both manifests unlicensed
        and len(by_rule.get("OC011", [])) == 4      # aws/ghp/xox error + password warn
        and oc011_sev == {"error", "warn"}
    )
    all_ok &= check("seeded supply findings (OC009/OC010/OC011)", ok4,
                    f"counts={ {k: len(v) for k, v in sorted(by_rule.items())} }")
    verdict["criteria"]["c4_supply"] = {
        "pass": bool(ok4),
        "counts": {k: len(v) for k, v in sorted(by_rule.items())},
        "supply_meta": r4["meta"]["supply"],
    }

    # ------------------------------------------------ KPI block -------
    print("KPIs verified at M4 (plan Table 3)")
    pk = run([oc, "packs", "--packs-dir", packs], cwd=REPO_ROOT)
    packs_json = json.loads(pk.stdout)
    packs_json = packs_json["packs"] if isinstance(packs_json, dict) else packs_json
    ok_kpi1 = len(packs_json) >= 2 and all(
        set(p["enabled_passes"]) >= {"OC001", "OC002", "OC003", "OC004", "OC005", "OC006",
                                     "OC007", "OC008", "OC009", "OC010", "OC011"}
        for p in packs_json if p["id"] in ("ts-js", "python")
    )
    n_passes = min(
        len([p for p in packs_json if p["id"] == pid][0]["enabled_passes"])
        for pid in ("ts-js", "python")
    )
    all_ok &= check("analysis count >= 10 on both packs", ok_kpi1,
                    f"ts-js/python enable {n_passes} passes each")
    verdict["kpis"]["analysis_count"] = {"pass": bool(ok_kpi1), "per_pack": n_passes}

    # CRAP coverage KPI: every complexity>=10 function carries CRAP output
    # when the coverage composition is complete (both adapters).
    graph_p = run([oc, "graph", "examples", "--packs-dir", packs,
                   "--coverage", cov_v8, "--coverage", cov_py],
                  cwd=REPO_ROOT)
    g = json.loads(graph_p.stdout)
    high_comp = [n for n in g["nodes"]
                 if n.get("kind") == "function" and n.get("complexity", 0) >= 10]
    ok_kpi2 = len(high_comp) > 0 and all(n.get("crap") is not None for n in high_comp)
    all_ok &= check("CRAP coverage: comp>=10 functions carry CRAP in output", ok_kpi2,
                    f"{len(high_comp)} such function(s), all scored")
    verdict["kpis"]["crap_coverage"] = {
        "pass": bool(ok_kpi2),
        "functions_checked": [n["name"] for n in high_comp],
    }

    verdict["kpis"]["mutation_budget"] = {"pass": bool(wall < 600.0),
                                          "measured_seconds": round(wall, 3)}
    verdict["all_pass"] = bool(all_ok)
    verdict["oc_binary"] = oc

    print()
    print(f"M4 gate verdict: {'PASS on all criteria + KPIs' if all_ok else 'FAIL'}")
    out = os.path.join(REPO_ROOT, args.verdict)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(verdict, f, indent=1, sort_keys=True)
        f.write("\n")
    print("verdict written:", os.path.relpath(out, REPO_ROOT))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
