#!/usr/bin/env python3
"""Mutation CI-budget re-measure (Sprint 5 carried item, near gate dates).

Measures, on the CURRENT tree, the wall time of `oc mutate` runs and
writes a machine-readable verdict JSON:

1. the committed demo project (tests/fixtures/mutation/pyproj) with its
   REAL test command -- the budget claim of Sprint 5/6 (0.62 s vs the
   10-minute Table-3 target), re-asserted here;
2. the two PINNED reference repositories (eslint lib/, black src/black)
   with a bounded mutant count and a minimal per-mutant test command
   (`python3 -c pass` / `node -e 0`): this isolates the ENGINE cost per
   mutant. The repos' real suites were deliberately NOT executed (same
   descope as Sprint 5); the budget implication is stated as arithmetic:
   a suite that costs T seconds per run affords floor(600/T) - 1
   mutants per CI run inside the 600 s target.

Usage:  python3 scripts/acceptance/mutation_budget_remeasure.py [--oc BIN]
Verdict: docs/evidence/mutation_budget_remeasure.json
"""
import argparse
import json
import os
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO_ROOT, "docs", "evidence", "mutation_budget_remeasure.json")
DEMO = os.path.join(REPO_ROOT, "tests", "fixtures", "mutation", "pyproj")
PINS = {
    "black": ("work/black-pin",
              "black-c0e143e75443afd3be50c3b63ed2ee2fd108f509",
              "src/black", "python3 -c pass"),
    "eslint": ("work/eslint-pin",
               "eslint-21026992e3520869b31ca4500905665942bb2cf0",
               "lib", "node -e 0"),
}
MUTANTS_PER_REF = 10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", default=os.path.join(REPO_ROOT, "build", "default", "oc"))
    args = ap.parse_args()
    oc = os.path.abspath(args.oc) if os.path.isabs(args.oc) \
        else os.path.join(REPO_ROOT, args.oc)
    packs = os.path.join(REPO_ROOT, "packs")
    verdict = {"schema": "omnicheck.evidence/v0",
               "audit": "mutation CI-budget re-measure (carried item, Sprint 5)",
               "oc": os.path.relpath(oc, REPO_ROOT), "runs": []}
    ok = True

    def run_mutate(cwd, test_cmd, max_mutants):
        cmd = [oc, "mutate", ".", "--packs-dir", packs,
               "--test-cmd", test_cmd, "--timeout", "60"]
        if max_mutants:
            cmd += ["--max-mutants", str(max_mutants)]
        t0 = time.time()
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd,
                           timeout=900)
        wall = time.time() - t0
        try:
            rep = json.loads(p.stdout)
        except json.JSONDecodeError:
            rep = {"error": (p.stderr or p.stdout)[:300]}
        return p.returncode, rep, wall

    # 1) demo project, REAL test command (the Sprint 5/6 budget claim)
    rc, rep, wall = run_mutate(DEMO, "python3 test_calc.py", None)
    demo_ok = (rc == 1 and rep.get("mutants_total") == 10
               and rep.get("killed") == 9 and rep.get("survived") == 1
               and rep.get("survivors", rep.get("mutants", [])) != [])
    survivor_ops = [m["op"] for m in rep.get("mutants", [])
                    if m.get("status") == "survived"]
    demo_ok = demo_ok and survivor_ops == ["arg-swap"]
    verdict["runs"].append({
        "scope": "demo (tests/fixtures/mutation/pyproj)",
        "test_cmd": "python3 test_calc.py (REAL suite)",
        "mutants_total": rep.get("mutants_total"), "killed": rep.get("killed"),
        "survived": rep.get("survived"), "survivor_ops": survivor_ops,
        "wall_seconds": round(wall, 2), "engine_wall_seconds": rep.get("wall_seconds"),
        "budget_target_s": 600.0, "within_budget": wall < 600.0,
        "pass": bool(demo_ok)})
    ok &= demo_ok

    # 2) pinned reference repos: engine cost per mutant, minimal test cmd
    for name, (base, dirlit, scope, cheap) in PINS.items():
        repo = os.path.join(REPO_ROOT, base, dirlit)
        if not os.path.isdir(os.path.join(repo, scope)):
            verdict["runs"].append({"scope": name, "pass": False,
                                    "error": f"pin missing: {repo}"})
            ok = False
            continue
        rc, rep, wall = run_mutate(os.path.join(repo, scope), cheap, MUTANTS_PER_REF)
        executed = rep.get("executed", 0)
        engine_wall = rep.get("wall_seconds", 0.0)
        per_mutant = (engine_wall / executed) if executed else None
        # budget arithmetic with the repo's REAL suite cost (recorded
        # literature-level reference, NOT measured here -- descoped):
        # affordable = floor(600 / T) - 1 mutants per run
        verdict["runs"].append({
            "scope": f"{name} @ pin ({scope})",
            "test_cmd": f"{cheap} (minimal -- isolates engine cost; REAL suite "
                        f"execution stays descoped, per Sprint 5)",
            "mutants_total": rep.get("mutants_total"), "executed": executed,
            "killed": rep.get("killed"), "survived": rep.get("survived"),
            "wall_seconds": round(wall, 2), "engine_wall_seconds": engine_wall,
            "engine_s_per_mutant": round(per_mutant, 4) if per_mutant else None,
            "note": "budget math: a real suite costing T s/run affords "
                    "floor(600/T)-1 mutants per CI run inside the 600 s target; "
                    "survival counts here are NOT quality claims (the minimal "
                    "test command kills nothing by design)",
            "pass": rc in (0, 1) and executed > 0 and wall < 600.0})
        ok &= verdict["runs"][-1]["pass"]

    verdict["all_pass"] = bool(ok)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(verdict, f, indent=1, sort_keys=True)
        f.write("\n")
    for r in verdict["runs"]:
        print(f"  {r['scope']}: pass={r['pass']} wall={r['wall_seconds']}s")
    print("verdict written:", os.path.relpath(OUT, REPO_ROOT))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
