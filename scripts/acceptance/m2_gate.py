#!/usr/bin/env python3
"""M2 Alpha gate acceptance harness (plan Table 9, gate M2 2027-02-28).

Executes the four recorded acceptance criteria against a pinned
reference TS/JS repository and emits a machine-readable verdict:

  1. Full pipeline, three+ analyses enabled -- the report must carry
     findings from at least three distinct OC rules over a corpus with
     files_analyzed > 0 and zero parse errors (no silent partial
     analysis; M1 kill-switch criterion #2 carries forward).
  2. Fingerprints stable across ten consecutive runs -- the JSON report
     embeds per-finding fingerprints; byte-identical reports across all
     runs prove fingerprint stability (and full determinism).
  3. JSON and human output -- both surfaces are produced; the human
     report is non-empty, self-describing, and itself deterministic.
  4. Reference-repo wall time within the 60-second target -- measured
     single-core (taskset when available) on the reference scope
     (~100k LOC TS/JS; G3, plan Table 4).

Usage:
  python3 scripts/acceptance/m2_gate.py --oc build/default/oc \
      --repo /path/to/eslint --scope lib [--runs 10] [--budget 60]

Exit code 0 iff every criterion holds; each criterion's measured
numbers are printed and collected into the verdict JSON.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

TSJS_EXTS = (".js", ".ts", ".jsx", ".tsx", ".mjs", ".cts", ".mts", ".cjs")


def count_loc(root):
    files = lines = 0
    for r, d, f in os.walk(root):
        d[:] = [x for x in d if x not in ("node_modules", ".git")]
        for x in f:
            if x.endswith(TSJS_EXTS):
                files += 1
                try:
                    with open(os.path.join(r, x), "rb") as fh:
                        lines += sum(1 for _ in fh)
                except OSError:
                    pass
    return files, lines


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oc", required=True, help="path to the oc binary")
    ap.add_argument("--repo", required=True, help="reference repo checkout (pinned)")
    ap.add_argument("--scope", default="lib", help="analysis root inside the repo (default lib)")
    ap.add_argument("--packs", default=None,
                    help="packs dir (default: the repo's own packs/ directory)")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--budget", type=float, default=60.0)
    args = ap.parse_args()

    oc = os.path.abspath(args.oc)
    scope = os.path.join(os.path.abspath(args.repo), args.scope)
    if not os.path.isfile(oc):
        sys.exit(f"oc binary not found: {oc}")
    if not os.path.isdir(scope):
        sys.exit(f"scope not found: {scope}")
    packs = args.packs
    if not packs:
        packs = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__)))), "packs")

    files, loc = count_loc(scope)
    print(f"reference scope: {scope}")
    print(f"corpus: {files} file(s), {loc} LOC (TS/JS)")

    wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []
    print(f"single-core wrapper: {' '.join(wrap) if wrap else '(none available)'}")

    tmp = "/tmp/omnicheck_m2_gate"
    os.makedirs(tmp, exist_ok=True)

    # Criterion 2 + 4: N consecutive JSON runs, byte-identical, timed.
    hashes = []
    walls = []
    first_rc = None
    for i in range(args.runs):
        out = f"{tmp}/run{i}.json"
        t0 = time.monotonic()
        rc = subprocess.run(wrap + [oc, "check", scope, "--packs-dir", packs, "--format", "json",
                                    "--fail-on", "info"],
                            stdout=open(out, "wb"), stderr=subprocess.DEVNULL).returncode
        walls.append(time.monotonic() - t0)
        hashes.append(sha256_file(out))
        if i == 0:
            first_rc = rc
    stable = len(set(hashes)) == 1
    wall_best = min(walls)
    wall_median = sorted(walls)[len(walls) // 2]

    # Criterion 1: three+ analyses, honest counters.
    with open(f"{tmp}/run0.json", "rb") as fh:
        report = json.loads(fh.read().decode())
    rules = sorted({f["rule"] for f in report.get("findings", [])})
    meta = report.get("meta", {})
    analyzed = meta.get("files_analyzed", 0)
    parse_errs = meta.get("files_with_parse_errors", 1)
    crit1 = len(rules) >= 3 and analyzed > 0 and parse_errs == 0

    # Criterion 3: human output exists, self-describing, deterministic.
    h1, h2 = f"{tmp}/human1.txt", f"{tmp}/human2.txt"
    for out in (h1, h2):
        subprocess.run(wrap + [oc, "check", scope, "--packs-dir", packs, "--format", "human",
                               "--fail-on", "info"],
                       stdout=open(out, "wb"), stderr=subprocess.DEVNULL)
    human_bytes = os.path.getsize(h1)
    with open(h1, "rb") as fh:
        head = fh.read(200)
    human_deterministic = sha256_file(h1) == sha256_file(h2)
    crit3 = human_bytes > 0 and b"human report" in head and human_deterministic

    crit2 = stable and first_rc in (0, 1)
    crit4 = wall_best < args.budget
    dup = meta.get("dup", {})

    print("")
    print(f"criterion 1 (full pipeline, >=3 analyses): rules={rules} "
          f"analyzed={analyzed} parse_errors={parse_errs} -> {'PASS' if crit1 else 'FAIL'}")
    print(f"criterion 2 (fingerprints stable, {args.runs} runs): "
          f"distinct_hashes={len(set(hashes))} exit={first_rc} -> {'PASS' if crit2 else 'FAIL'}")
    print(f"criterion 3 (json + human output): human_bytes={human_bytes} "
          f"human_deterministic={human_deterministic} -> {'PASS' if crit3 else 'FAIL'}")
    print(f"criterion 4 (wall < {args.budget:g}s single-core): best={wall_best:.2f}s "
          f"median={wall_median:.2f}s -> {'PASS' if crit4 else 'FAIL'}")
    print(f"supporting: OC008 dup groups={dup.get('groups')} "
          f"truncated={dup.get('groups_truncated')} oversize_skipped={dup.get('files_skipped_oversize')}")

    verdict = {
        "gate": "M2 Alpha",
        "date_executed": time.strftime("%Y-%m-%d"),
        "scope": scope,
        "corpus_files": files,
        "corpus_loc": loc,
        "runs": args.runs,
        "criteria": {
            "1_three_analyses_no_silent_partial": {
                "pass": crit1, "rules": rules, "files_analyzed": analyzed,
                "files_with_parse_errors": parse_errs},
            "2_fingerprints_stable": {"pass": crit2, "runs": args.runs,
                                      "distinct_sha256": len(set(hashes))},
            "3_json_and_human_output": {"pass": crit3, "human_bytes": human_bytes,
                                        "human_deterministic": human_deterministic},
            "4_wall_time_under_budget": {"pass": crit4, "budget_s": args.budget,
                                         "best_s": round(wall_best, 2),
                                         "median_s": round(wall_median, 2),
                                         "single_core": bool(wrap)},
        },
        "dup": dup,
        "all_pass": crit1 and crit2 and crit3 and crit4,
    }
    out_json = f"{tmp}/m2_gate_verdict.json"
    with open(out_json, "w") as fh:
        json.dump(verdict, fh, indent=2)
    print(f"verdict: {'PASS' if verdict['all_pass'] else 'FAIL'} -> {out_json}")
    sys.exit(0 if verdict["all_pass"] else 1)


if __name__ == "__main__":
    main()
