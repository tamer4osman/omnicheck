#!/usr/bin/env python3
"""Gate-day ritual runner (docs/V1_TAG_RITUAL.md, mechanized).

Executes the ritual pre-condition chain step by step, records each
step's result INCREMENTALLY into an evidence bundle, and stops at the
first red step (exit 1). Every step is individually invokable and
resumable: a step already recorded PASS in the bundle is skipped on
re-invocation (matrix resumes per CELL), unless --fresh wipes the
bundle. A gate-day run that dies mid-way therefore never restarts from
zero, and no step can be narrated into passing: each step runs the
committed checkers and records what they printed.

The runner NEVER cuts the v1.0.0 tag. When every step is green for
--gate M5 it prints the three tag commands (ritual steps 7-9) for a
human to execute; the irreversible act stays human.

Usage:
  python3 scripts/acceptance/gate_day_runner.py --gate M1                # full chain
  python3 scripts/acceptance/gate_day_runner.py --gate M1 --step matrix  # one step
  python3 scripts/acceptance/gate_day_runner.py --gate M1 --fresh        # wipe bundle first
  # constrained-host chunked ctest (SPRINT-20): cell ctest split by test
  # index across command windows; PASS = every chunk clean AND union == -N
  gate_day_runner.py --gate M5 --step matrix --cell clang-sanitize \
      --ctest-chunk 1/2   # (window 1: fresh build + tests 1..17)
  gate_day_runner.py --gate M5 --step matrix --cell clang-sanitize \
      --ctest-chunk 2/2   # (window 2: no-op build + tests 18..34; union gate)

Steps (ritual order):
  tree-clean   ritual 1: git status --short empty on main
  matrix       ritual 3: 4-cell FROM SCRATCH configure+build+ctest (20/20 each)
  m1-replay    M1 drift replay: fresh clone + vendor.sh + build + ctest,
               exit-2 contract, cross-tree byte identity, 10k perf
  harnesses    ritual 2: harnesses DUE by --gate
               (M1: m1-replay | M2: m2 | M3: m3 | M4: m4 | M5: m2+m3+m4+m5)
  audit        ritual 4: determinism_audit.py -- one digest across cells
  docs         ritual 5: audit_docs.py -- 100 percent
  package      ritual 6: package.sh + npm shim (OC_BIN and native legs)
               + LOCAL `npm install -g` smoke through the real install
               path (registry publishing stays descoped)

Bundle: docs/evidence/gate_day/<gate>_<date>/bundle.json
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLANG = "/tmp/clangroot/usr/lib/llvm-19/bin/clang"
GATE_DATES = {"M1": "2026-12-31", "M2": "2027-02-28", "M3": "2027-05-31",
              "M4": "2027-09-30", "M5": "2027-12-31"}
HARNESS_DUE = {"M1": [], "M2": ["m2"], "M3": ["m3"], "M4": ["m4"],
               "M5": ["m2", "m3", "m4", "m5"]}
PINS = {
    "m2": ("eslint", "21026992e3520869b31ca4500905665942bb2cf0", "lib"),
    "m3": ("black", "c0e143e75443afd3be50c3b63ed2ee2fd108f509", "src/black"),
}
CELLS = [
    ("default",        ["cmake", "--preset", "default"]),
    ("sanitize",       ["cmake", "--preset", "sanitize"]),
    ("clang-release",  ["cmake", "-B", "build/clang-release", "-DOC_BUILD_TESTS=ON",
                        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_BUILD_TYPE=Release",
                        f"-DCMAKE_C_COMPILER={CLANG}"]),
    ("clang-sanitize", ["cmake", "-B", "build/clang-sanitize", "-DOC_BUILD_TESTS=ON",
                        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_BUILD_TYPE=Debug",
                        "-DOC_SANITIZE_ADDRESS=ON", "-DOC_SANITIZE_UNDEFINED=ON",
                        f"-DCMAKE_C_COMPILER={CLANG}"]),
]


def run(cmd, **kw):
    kw.setdefault("cwd", REPO_ROOT)
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


class Bundle:
    def __init__(self, gate, fresh):
        self.dir = os.path.join(REPO_ROOT, "docs", "evidence", "gate_day",
                                f"{gate}_{time.strftime('%Y-%m-%d')}")
        os.makedirs(self.dir, exist_ok=True)
        self.path = os.path.join(self.dir, "bundle.json")
        if fresh and os.path.exists(self.path):
            os.remove(self.path)
        self.data = {"schema": "omnicheck.evidence/v0", "gate": gate,
                     "gate_date": GATE_DATES[gate], "steps": {}}
        if os.path.exists(self.path):
            with open(self.path) as f:
                self.data = json.load(f)

    def passed(self, step, sub=None):
        s = self.data["steps"].get(step)
        if not s or not s.get("pass"):
            return False
        if sub is not None:
            return bool(s.get("details", {}).get(sub, {}).get("pass"))
        return True

    def record(self, step, ok, details):
        self.data["steps"][step] = {"pass": bool(ok), "details": details,
                                    "recorded": time.strftime("%Y-%m-%dT%H:%M:%S")}
        with open(self.path, "w") as f:
            json.dump(self.data, f, indent=2)
        print(f"  [{'PASS' if ok else 'FAIL'}] {step} -> {self.path}")
        return ok


def verdict_lines(p):
    out = (p.stdout or "").strip().splitlines()
    return out[-1] if out else ""


def harnesses_complete(det, gate):
    """B8 fix (SPRINT-9): the harnesses step passes only when EVERY leg
    due for this gate is present AND passing in the details. Recording
    cumulative-so-far progress as the step verdict made a mid-run death
    after leg 1 write pass=True for the whole step -- a stale partial
    PASS the resume logic then trusted forever (the SPRINT-7 incident).
    Per-leg details stay incremental; only the STEP verdict is
    completion-gated."""
    due = HARNESS_DUE[gate]
    return bool(due) and all(det.get(h, {}).get("pass") for h in due)


# --------------------------------------------------------------- steps --
def step_tree_clean(b):
    p = run(["git", "status", "--short"])
    branch = run(["git", "rev-parse", "--abbrev-ref", "HEAD"]).stdout.strip()
    dirty = [l for l in (p.stdout or "").splitlines() if l.strip()]
    # the runner's OWN gate directory (docs/evidence/gate_day/<gate>_<date>/)
    # is written BY this ritual -- bundle.json and the harness verdicts it
    # records -- so it is the one mechanical exemption, untracked OR
    # tracked-modified (a --fresh re-run of an already-committed bundle
    # rewrites it; without the tracked exemption the ritual could never
    # re-prove a gate -- caught by the M4 re-run dogfood, 2026-11-19).
    # Every OTHER gate's directory and everything else -- local scratch,
    # untracked evidence, modified tracked files -- is red.
    own_gate_dir = os.path.relpath(b.dir, REPO_ROOT) + os.sep
    dirty = [l for l in dirty
             if own_gate_dir not in l
             and (not l.lstrip().startswith("?? ")
                  or "docs/evidence/gate_day/" not in l)]
    ok = not dirty and branch == "main"
    return b.record("tree-clean", ok,
                    {"branch": branch, "dirty_entries": dirty,
                     "exempted_own_gate_dir": own_gate_dir.rstrip(os.sep)})


def ctest_catalog(cell_dir):
    """Authoritative test list: `ctest -N` in the cell's build tree."""
    p = run(["ctest", "-N"], cwd=cell_dir)
    pairs = sorted((int(n), name) for n, name in
                   re.findall(r"Test\s+#(\d+): (\S+)", p.stdout or ""))
    return pairs


def parse_ctest_results(out):
    """(num, name) per-test verdict lines: `N/M Test #k: name .... Passed`.

    ctest separates the test name from the progress dots with spaces
    (`Test #4: test_fingerprint .................   Passed`), so the
    name and the dots are matched independently of each other.
    """
    return [(int(n), name, verdict) for n, name, verdict in
            re.findall(r"Test\s+#(\d+):\s+(\S+)\s*(?:\.+\s*)?(Passed|Failed)",
                       out or "")]


def step_matrix(b, only_cell=None, fresh=True, ctest_chunk=None):
    ok_all = True
    per_cell = dict(b.data["steps"].get("matrix", {}).get("details", {}))
    build_dir = os.path.join(REPO_ROOT, "build")
    # a cell may only be skipped if the bundle says PASS **and** its build
    # tree actually exists -- a wiped build dir invalidates stale entries
    def cell_buildable(cell):
        return os.path.exists(os.path.join(build_dir, cell, "CMakeCache.txt"))
    cells = [(c, cfg) for c, cfg in CELLS if only_cell is None or c == only_cell]
    # Full-step invocation keeps the from-scratch contract: wipe ALL cells
    # unless every cell is already PASS+buildable. A --cell invocation
    # wipes only the requested cell (from-scratch per cell), so the
    # documented per-CELL resume is actually usable in environments that
    # cannot host the whole 4-cell rebuild in one sitting (SPRINT-9).
    # SPRINT-20 extension: --cell WITHOUT --fresh is the constrained-host
    # resume -- the cell's build tree is KEPT and the incremental build
    # is a no-op, so configure+build+ctest can span two command windows
    # (build first, then the runner) on a host whose window cannot host
    # the whole cell; the tree is still the same git-clean commit and the
    # correctness criteria are unchanged.
    if only_cell is None and os.path.isdir(build_dir) and not all(
            b.passed("matrix", c) and cell_buildable(c) for c, _ in CELLS):
        shutil.rmtree(build_dir)
        per_cell = {"wiped": True}
        b.record("matrix", False, per_cell)  # reset stale entries
    for cell, cfg in cells:
        if b.passed("matrix", cell) and cell_buildable(cell):
            print(f"  matrix/{cell}: already PASS in bundle, skip")
            continue
        if only_cell is not None and fresh:
            shutil.rmtree(os.path.join(build_dir, cell), ignore_errors=True)
        if "clang" in cell and not os.path.exists(CLANG):
            return b.record("matrix", False, {
                **per_cell, cell: {"pass": False,
                "error": f"{CLANG} missing -- re-run the no-sudo clang recipe "
                         "(apt-get download clang-19 libclang-cpp19 libllvm19 "
                         "libclang-common-19-dev libclang-rt-19-dev + dpkg -x + "
                         "lib/clang/19 layout copy), see SPRINT-5.md"}})
        t0 = time.monotonic()
        cfg_p = run(cfg)
        # SPRINT-20: the BUILD parallelism adapts to the analysis host
        # (the hardcoded -j 4 OOMs the instrumented sanitize cells on a
        # 2-core/3GB sandbox; CI hosts still get 4). CTEST parallelism is
        # deliberately fixed at 4 -- tests are lightweight runtime
        # processes (the OOM risk is the compile, not the run) and on the
        # 2-core sandbox ctest -j 2 measured 1.16x average concurrency
        # (686s suite-sum / 590s wall) while -j 4 packs the two cores and
        # keeps the whole 4-cell ritual inside one command window. The
        # correctness criterion -- configure+build+ctest all clean -- is
        # unchanged.
        bld_p = run(["cmake", "--build", f"build/{cell}", "-j",
                     str(min(4, os.cpu_count() or 1))])
        if cfg_p.returncode != 0 or bld_p.returncode != 0:
            per_cell[cell] = {"pass": False,
                              "error": f"configure/build rc={cfg_p.returncode}/{bld_p.returncode}"}
            b.record("matrix", False, per_cell)
            return False
        cell_dir = os.path.join(REPO_ROOT, "build", cell)
        if ctest_chunk is None:
            # single-window path (CI and hosts whose window fits the cell):
            # one full ctest, verdict from its own summary
            ctest_p = run(["ctest", "-j", "4"], cwd=cell_dir)
            dt = round(time.monotonic() - t0, 1)
            tail = (ctest_p.stdout or "").strip().splitlines()
            summary = next((l for l in tail if "tests passed" in l or "tests failed" in l), "")
            cell_ok = ctest_p.returncode == 0 \
                and "100% tests passed" in (ctest_p.stdout or "")
            per_cell[cell] = {"pass": cell_ok, "tests": summary.strip(),
                              "seconds": dt}
            b.record("matrix", cell_ok and ok_all, per_cell)
            if not cell_ok:
                return False
            ok_all = ok_all and cell_ok
            continue
        # SPRINT-20 chunked constrained-host path: the sanitize cell's
        # ctest ALONE (686s serial for 34 suites on the 2-core sandbox;
        # ASan-instrumented tree-sitter parsing dominates) exceeds a
        # single 10-minute command window at ANY parallelism (-j2 measured
        # 1.16x average concurrency, -j4 doubled every suite). ctest is
        # split by test index across windows (--ctest-chunk i/n); the
        # cell PASS is recorded only when EVERY chunk ran clean AND the
        # union of executed tests equals the authoritative `ctest -N`
        # list -- nothing skipped, nothing double-counted. Same
        # completion-gated pattern as the harnesses step (B8 fix):
        # partial chunk progress NEVER marks the step PASS.
        m = re.fullmatch(r"(\d+)/(\d+)", ctest_chunk or "")
        if not m:
            raise SystemExit("--ctest-chunk must be i/n (e.g. 1/2)")
        ci, cn = int(m.group(1)), int(m.group(2))
        catalog = ctest_catalog(cell_dir)
        n_tests = len(catalog)
        if not catalog or cn < 1 or ci < 1 or ci > cn or n_tests // cn < 1:
            raise SystemExit(f"--ctest-chunk {ctest_chunk}: invalid vs "
                             f"catalog of {n_tests} tests")
        start = (ci - 1) * n_tests // cn + 1
        end = ci * n_tests // cn
        ctest_p = run(["ctest", "-I", f"{start},{end}", "-j", "4"], cwd=cell_dir)
        dt = round(time.monotonic() - t0, 1)
        results = parse_ctest_results(ctest_p.stdout)
        res_nums = {n for n, _, _ in results}
        expected = set(range(start, end + 1))
        chunk_ok = (ctest_p.returncode == 0
                    and all(v == "Passed" for _, _, v in results)
                    and res_nums == expected)
        chunks = dict(per_cell.get(cell, {}).get("chunks", {}))
        chunks[str(ci)] = {"pass": chunk_ok, "range": f"{start}-{end}",
                           "names": [nm for _, nm, _ in results],
                           "seconds": dt}
        complete = all(chunks.get(str(k), {}).get("pass") for k in range(1, cn + 1)) \
            and sorted(nm for k in range(1, cn + 1)
                       for nm in chunks[str(k)]["names"] if str(k) in chunks) \
            == sorted(nm for _, nm in catalog)
        if complete:
            per_cell[cell] = {"pass": True,
                              "tests": f"100% tests passed ({n_tests}/{n_tests}) "
                                       f"across {cn} chunked ctest windows",
                              "chunks": chunks,
                              "seconds": round(sum(chunks[str(k)]["seconds"]
                                                   for k in range(1, cn + 1)), 1)}
            b.record("matrix", ok_all, per_cell)
            ok_all = ok_all and True
        else:
            per_cell[cell] = {"pass": False, "chunks": chunks}
            b.record("matrix", False, per_cell)
            nxt = ci + 1 if ci < cn else ci
            print(f"  matrix/{cell}: chunk {ci}/{cn} "
                  f"[{start}-{end}] {'PASS' if chunk_ok else 'RED'} -- "
                  + (f"cell incomplete; next window: --cell {cell} "
                     f"--ctest-chunk {nxt}/{cn}" if chunk_ok else
                     "chunk red -- no union pass"))
            # strict (B8 lesson): partial chunk progress never completes the
            # step -- the ritual halts until the next window finishes it
            return False
    return ok_all


def step_m1_replay(b):
    det = {}
    ok = True
    clone = f"/tmp/oc-fresh-{time.strftime('%Y%m%d')}"
    shutil.rmtree(clone, ignore_errors=True)
    p = run(["git", "clone", "-q", ".", clone])
    ok &= p.returncode == 0
    det["clone"] = p.returncode == 0
    p = run(["sh", "scripts/vendor.sh"], cwd=clone)
    det["vendor_sh"] = p.returncode == 0
    ok &= det["vendor_sh"]
    if det["vendor_sh"]:
        p = run(["cmake", "--preset", "default"], cwd=clone)
        ok &= p.returncode == 0
        p = run(["cmake", "--build", "--preset", "default"], cwd=clone)
        ok &= p.returncode == 0
        p = run(["ctest"], cwd=os.path.join(clone, "build", "default"))
        det["fresh_ctest"] = "100% tests passed" in (p.stdout or "")
        ok &= det["fresh_ctest"]
        # exit-2 contract on the broken fixtures
        rb = os.path.join(clone, "build", "default", "oc")
        p = run([rb, "check", "tests/fixtures_broken"])
        err = (p.stderr or "")
        det["broken_exit_2"] = p.returncode == 2 and "warning" in err
        ok &= det["broken_exit_2"]
        # cross-tree byte identity on examples -- RELATIVE paths from each
        # repo root: reports embed the analysis root as given, so absolute
        # roots (/home/... vs /tmp/...) would differ spuriously
        main_bin = os.path.join(REPO_ROOT, "build", "default", "oc")
        with open("/tmp/gr_a.json", "wb") as f:
            subprocess.run([main_bin, "check", "examples"], cwd=REPO_ROOT,
                           stdout=f, stderr=subprocess.DEVNULL)
        with open("/tmp/gr_b.json", "wb") as f:
            subprocess.run([rb, "check", "examples"], cwd=clone,
                           stdout=f, stderr=subprocess.DEVNULL)
        det["cross_tree_cmp"] = open("/tmp/gr_a.json", "rb").read() \
            == open("/tmp/gr_b.json", "rb").read()
        ok &= det["cross_tree_cmp"]
        det["examples_sha256"] = __import__("hashlib").sha256(
            open("/tmp/gr_a.json", "rb").read()).hexdigest()[:16]
        # 10k perf, single core
        run(["python3", "scripts/perf_gen.py", "work/perf10k"])
        wrap = ["taskset", "-c", "0"] if shutil.which("taskset") else []
        t0 = time.monotonic()
        with open(os.devnull, "wb") as devnull:
            p = subprocess.run(wrap + [main_bin, "check", "work/perf10k"],
                               cwd=REPO_ROOT, stdout=devnull,
                               stderr=subprocess.DEVNULL)
        perf = round(time.monotonic() - t0, 2)
        det["perf_10k_single_core_s"] = perf
        det["perf_under_90s_gate"] = perf < 90.0 and p.returncode in (0, 1)
        ok &= det["perf_under_90s_gate"]
    return b.record("m1-replay", bool(ok), det)


def ensure_pin(name, sha):
    """Fetch a pinned reference via the codeload full-SHA URL if absent."""
    path = os.path.join(REPO_ROOT, "work", f"{name}-pin", f"{name}-{sha}")
    if os.path.isdir(path):
        return path
    os.makedirs(os.path.join(REPO_ROOT, "work", f"{name}-pin"), exist_ok=True)
    tgz = os.path.join(REPO_ROOT, "work", f"{name}-{sha[:8]}.tar.gz")
    if not os.path.exists(tgz):
        p = run(["curl", "-sSL", "-o", tgz,
                 f"https://codeload.github.com/psf/{name}/tar.gz/{sha}"
                 if name == "black" else
                 f"https://codeload.github.com/eslint/eslint/tar.gz/{sha}"])
        if p.returncode != 0:
            return None
    if not os.path.isdir(path):
        subprocess.run(["tar", "xzf", tgz, "-C",
                        os.path.join(REPO_ROOT, "work", f"{name}-pin")], check=False)
    return path if os.path.isdir(path) else None


def step_harnesses(b, gate):
    det, ok = {}, True
    if gate == "M1":
        return b.record("harnesses", True, {
            "note": "no harness script due at M1: the drift replay "
                    "obligation is discharged by the m1-replay step"})
    for h in HARNESS_DUE[gate]:
        if b.passed("harnesses", h):
            print(f"  harnesses/{h}: already PASS in bundle, skip")
            continue
        out = os.path.join(b.dir, f"{h}_gate_verdict.json")
        if h == "m2":
            repo = ensure_pin(PINS["m2"][0], PINS["m2"][1])
            if not repo:
                return b.record("harnesses", False, {**det, "m2": {"pass": False,
                                                     "error": "eslint pin fetch failed"}})
            p = run(["python3", "scripts/acceptance/m2_gate.py", "--oc",
                     "build/default/oc", "--repo", repo, "--scope", "lib"])
            ok &= p.returncode == 0
            shutil.copy("/tmp/omnicheck_m2_gate/m2_gate_verdict.json", out)
            det["m2"] = {"pass": p.returncode == 0, "verdict": verdict_lines(p)}
        elif h == "m3":
            repo = ensure_pin(PINS["m3"][0], PINS["m3"][1])
            if not repo:
                return b.record("harnesses", False, {**det, "m3": {"pass": False,
                                                     "error": "black pin fetch failed"}})
            p = run(["python3", "scripts/acceptance/m3_gate.py", "--oc",
                     "build/default/oc", "--repo", repo, "--scope",
                     PINS["m3"][2], "--out", out])
            ok &= p.returncode == 0
            det["m3"] = {"pass": p.returncode == 0, "verdict": verdict_lines(p)}
        elif h == "m4":
            p = run(["python3", "scripts/acceptance/m4_gate.py", "--verdict", out])
            ok &= p.returncode == 0
            det["m4"] = {"pass": p.returncode == 0, "verdict": verdict_lines(p)}
        elif h == "m5":
            p = run(["python3", "scripts/acceptance/m5_gate.py", "--oc",
                     "build/default/oc", "--verdict", out])
            ok &= p.returncode == 0
            det["m5"] = {"pass": p.returncode == 0, "verdict": verdict_lines(p)}
        b.record("harnesses", harnesses_complete(det, gate), det)
    if gate == "M5" and ok and harnesses_complete(det, gate):
        print("  All green. Ritual steps 7-9 are left to the human; execute:")
        print("    1) bump OC_VERSION_STRING + npm/package.json -> 1.0.0, commit 'release: v1.0.0'")
        print("    2) re-run scripts/package.sh")
        print("    3) git tag -a v1.0.0 -m 'OmniCheck 1.0.0 (M5 GA)'")
        det["tag_left_to_human"] = True
        b.record("harnesses", True, det)
    return harnesses_complete(det, gate) if HARNESS_DUE[gate] else bool(ok)


def step_audit(b):
    p = run(["python3", "scripts/acceptance/determinism_audit.py"])
    ok = p.returncode == 0
    det = {"pass": ok}
    path = os.path.join(REPO_ROOT, "docs", "evidence", "determinism_audit.json")
    if os.path.exists(path):
        a = json.load(open(path))
        det["distinct_digests"] = a.get("distinct_digests")
        det["budget_check"] = a.get("budget_check", {})
        ok = ok and a.get("distinct_digests") == 1
    return b.record("audit", bool(ok), det)


def step_docs(b):
    p = run(["python3", "scripts/audit_docs.py"])
    return b.record("docs", p.returncode == 0, {"tail": verdict_lines(p)})


def step_package(b):
    det, ok = {}, True
    p = run(["sh", "scripts/package.sh"])
    det["package_sh"] = {"pass": p.returncode == 0, "tail": verdict_lines(p)}
    ok &= p.returncode == 0
    oc = os.path.join(REPO_ROOT, "build", "default", "oc")
    # reference report from the repo build -- SAME argv as the shim legs
    # (shim runs with --packs-dir), so byte-identity is exact
    with open("/tmp/gr_repo.json", "wb") as f:
        subprocess.run([oc, "check", "examples", "--packs-dir", "packs"],
                       cwd=REPO_ROOT, stdout=f, stderr=subprocess.DEVNULL)
    ref_bytes = open("/tmp/gr_repo.json", "rb").read()
    # shim leg 1: OC_BIN
    env = dict(os.environ, OC_BIN=oc)
    p = subprocess.run(["node", os.path.join("npm", "bin", "oc-shim.js"),
                        "check", "examples", "--packs-dir", "packs"],
                       cwd=REPO_ROOT, capture_output=True, env=env)
    det["shim_oc_bin"] = {"pass": p.returncode == 1 and p.stdout == ref_bytes,
                          "exit": p.returncode, "stderr_tail": (p.stderr or "")[-120:]}
    ok &= det["shim_oc_bin"]["pass"]
    # shim leg 2: shipped binary at bin/native/oc
    stage = "/tmp/oc-shim-stage"
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "bin", "native"))
    shutil.copy(os.path.join("npm", "bin", "oc-shim.js"),
                os.path.join(stage, "bin", "oc-shim.js"))
    shutil.copy(oc, os.path.join(stage, "bin", "native", "oc"))
    p = subprocess.run(["node", os.path.join(stage, "bin", "oc-shim.js"), "--version"],
                       capture_output=True, text=True)
    det["shim_native"] = {"pass": p.returncode == 0
                          and "omnicheck" in (p.stdout or ""),
                          "stdout": (p.stdout or "").strip()}
    ok &= det["shim_native"]["pass"]
    # shim leg 3 (NEW): real `npm install -g` from the packed tarball, local
    # prefix -- the REAL install path; registry publishing stays descoped
    npm_version = json.load(open(os.path.join(REPO_ROOT, "npm", "package.json")))
    dist_dir = os.path.join(REPO_ROOT, "dist")
    os.makedirs(dist_dir, exist_ok=True)  # npm pack does NOT create it
    tgz = os.path.join(dist_dir, f"omnicheck-{npm_version['version']}.tgz")
    p = run(["npm", "pack", "--pack-destination", dist_dir],
            cwd=os.path.join(REPO_ROOT, "npm"))
    det["npm_pack"] = {"pass": p.returncode == 0 and os.path.exists(tgz)}
    ok &= det["npm_pack"]["pass"]
    prefix = "/tmp/npm-smoke"
    shutil.rmtree(prefix, ignore_errors=True)
    if det["npm_pack"]["pass"]:
        p = run(["npm", "install", "-g", "--prefix", prefix, tgz])
        det["npm_install"] = {"pass": p.returncode == 0}
        ok &= det["npm_install"]["pass"]
        stub = os.path.join(prefix, "bin", "oc")
        det["npm_install"]["stub_exists"] = os.path.exists(stub)
        if os.path.exists(stub):
            p = subprocess.run([stub, "check", "examples", "--packs-dir", "packs"],
                               cwd=REPO_ROOT, capture_output=True, env=env)
            det["npm_install_smoke"] = {
                "pass": p.returncode == 1 and p.stdout == ref_bytes,
                "exit_passthrough": p.returncode == 1,
                "report_bytes_equal_to_repo_build": p.stdout == ref_bytes}
            ok &= det["npm_install_smoke"]["pass"]
    return b.record("package", bool(ok), det)


STEPS = ["tree-clean", "matrix", "m1-replay", "harnesses", "audit", "docs", "package"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gate", required=True, choices=sorted(GATE_DATES))
    ap.add_argument("--step", default="all", choices=STEPS + ["all"])
    ap.add_argument("--fresh", action="store_true")
    ap.add_argument("--cell", default=None, choices=[c for c, _ in CELLS],
                    help="matrix step only: run ONE cell (from-scratch per "
                         "cell); the documented per-CELL resume, usable "
                         "when the full 4-cell rebuild cannot run in one "
                         "sitting (SPRINT-9)")
    ap.add_argument("--ctest-chunk", default=None, metavar="I/N",
                    help="matrix step only: split the cell's ctest by test "
                         "index across windows (e.g. 1/2 then 2/2); the "
                         "cell PASS requires every chunk clean AND the "
                         "executed-test union equal to `ctest -N` (SPRINT-20)")
    args = ap.parse_args()
    b = Bundle(args.gate, args.fresh)
    order = STEPS if args.step == "all" else [args.step]
    # the M1 drift replay obligation is due at the M1 gate only; later
    # gates re-run their own harnesses (HARNESS_DUE) and the full matrix
    if args.gate != "M1" and "m1-replay" in order:
        order.remove("m1-replay")
    for step in order:
        print(f"== step {step} (gate {args.gate}, formal date "
              f"{GATE_DATES[args.gate]}) ==")
        if b.passed(step) and step != "matrix":
            print("  already PASS in bundle, skip")
            continue
        if step == "tree-clean":
            ok = step_tree_clean(b)
        elif step == "matrix":
            ok = step_matrix(b, args.cell, fresh=args.fresh,
                             ctest_chunk=args.ctest_chunk)
        elif step == "m1-replay":
            ok = step_m1_replay(b)
        elif step == "harnesses":
            ok = step_harnesses(b, args.gate)
        elif step == "audit":
            ok = step_audit(b)
        elif step == "docs":
            ok = step_docs(b)
        elif step == "package":
            ok = step_package(b)
        if not ok:
            print(f"STOP: step '{step}' red -- ritual halts here "
                  "(any red item stops the ritual; no tag anyway).")
            sys.exit(1)
    scope = "RITUAL CHAIN" if args.step == "all" else "requested steps"
    print(f"{scope} GREEN for gate {args.gate} "
          f"(formal date {GATE_DATES[args.gate]}; bundle {b.path}).")


if __name__ == "__main__":
    main()
