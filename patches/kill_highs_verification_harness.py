#!/usr/bin/env python3
"""PoC harness for OmniCheck High findings H-1..H-5 (kill-verification).

Rebuilds the five adversarial fixtures from the assessment report
(T1, T5, T6b, T7, T8) and runs them against a chosen `oc` binary with
wait4() rusage measurement. RLIMIT_AS keeps the sandbox alive.

Usage:
  python3 oc_kill_highs_poc.py <oc-binary> [--baseline|--verify] [--quick]

Fixtures live in /home/z/my-project/poc/<name>/ and are regenerated if missing.
"""
import json
import os
import resource
import subprocess
import sys
import time

PACKS = "/home/z/my-project/repo-omnicheck/packs"
POC = "/home/z/my-project/poc"
AS_LIMIT_MB = 2500  # RLIMIT_AS cap: big enough to demonstrate the quadratic
                    # blowup, small enough to never kill the sandbox


def gen_t1():
    """T1: 30,000 pinned deps in package.json (JSON flat bomb)."""
    d = f"{POC}/t1"
    os.makedirs(d, exist_ok=True)
    p = f"{d}/package.json"
    if not os.path.exists(p):
        deps = ",\n".join(
            f'    "pkg-{i:05d}": "1.0.{i}"' for i in range(30000))
        with open(p, "w") as f:
            f.write('{\n  "name": "bomb",\n  "version": "1.0.0",\n'
                    '  "dependencies": {\n' + deps + '\n  }\n}\n')
    return d


def gen_t5():
    """T5: 47-byte go coverprofile with a 1e9 line span."""
    d = f"{POC}/t5"
    os.makedirs(d, exist_ok=True)
    cov = f"{d}/cov.out"
    if not os.path.exists(cov):
        with open(cov, "w") as f:
            f.write("mode: set\nsvc/handler.go:1.1,1000000000.0 1 1\n")
    with open(f"{d}/handler.go", "w") as f:
        f.write("package svc\n\nfunc Do() int {\n\tif 1 > 0 {\n\t\treturn 1\n\t}\n\treturn 0\n}\n")
    return d


def gen_t6b(depth):
    """T6b: JS nested-call chain f(f(f(...))). depth = nesting levels."""
    d = f"{POC}/t6b_{depth}"
    os.makedirs(d, exist_ok=True)
    p = f"{d}/chain.js"
    if not os.path.exists(p):
        with open(p, "w") as f:
            f.write("function wrapper() {\n  return " + "fn(" * depth + "1" + ")" * depth +
                    ";\n}\n")
    return d


def gen_t6c(depth):
    """T6c: nested arrow functions (()=>(()=>...)) — exercises per-function
    complexity re-walk specifically."""
    d = f"{POC}/t6c_{depth}"
    os.makedirs(d, exist_ok=True)
    p = f"{d}/nest.js"
    if not os.path.exists(p):
        with open(p, "w") as f:
            f.write("const v = " + "(()=>{" * depth + "1" + "})" * depth + ";\n")
    return d


def gen_t7():
    """T7: Rust use-tree nested 100,000 deep (~510 KB)."""
    d = f"{POC}/t7"
    os.makedirs(d, exist_ok=True)
    p = f"{d}/deep_use.rs"
    if not os.path.exists(p):
        with open(p, "w") as f:
            f.write("use crate::" + "a::{" * 100000 + "b" + "}" * 100000 + ";\n")
    return d


def gen_t8():
    """T8: mutate kill-during-run — tiny JS project with a rel-op mutant.
    calc.js is made executable: mode preservation across the atomic
    splice/restore is part of the H-4 contract (rename() must not drop
    the exec bit the old truncate-write kept)."""
    d = f"{POC}/t8"
    os.makedirs(d, exist_ok=True)
    src = f"{d}/calc.js"
    with open(src, "w") as f:
        f.write("function eq(a, b) {\n  return a == b;\n}\n\nmodule.exports = { eq };\n")
    os.chmod(src, 0o755)  # H-4: exec bit must survive every write path
    with open(f"{d}/test_calc.js", "w") as f:
        f.write("const { eq } = require('./calc');\nconsole.log(eq(1, 1) === true);\n")
    return d, src


def run(argv, timeout=30.0, mem_mb=AS_LIMIT_MB, cwd=None):
    """Fork+exec+wait4 runner. Returns dict with wall, rss_mib, exit info."""
    # ASan/UBSan binaries reserve terabytes of virtual address space for
    # shadow memory: RLIMIT_AS would kill them at startup. No cap there.
    if "sanitize" in argv[0]:
        mem_mb = None
    pid = os.fork()
    if pid == 0:
        try:
            if mem_mb:
                lim = mem_mb * 1024 * 1024
                resource.setrlimit(resource.RLIMIT_AS, (lim, lim))
            if cwd:
                os.chdir(cwd)
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, 1)
            os.dup2(devnull, 2)
            os.execv(argv[0], argv)
        except Exception:
            os._exit(127)
    t0 = time.time()
    killed = False
    while True:
        wpid, status, ru = os.wait4(pid, os.WNOHANG)
        if wpid == pid:
            break
        if time.time() - t0 > timeout:
            os.kill(pid, 9)
            wpid, status, ru = os.wait4(pid, 0)
            killed = True
            break
        time.sleep(0.01)
    wall = time.time() - t0
    res = {
        "wall": round(wall, 2),
        "rss_mib": round(ru.ru_maxrss / 1024.0, 1),
        "killed": killed,
    }
    if os.WIFSIGNALED(status):
        res["signal"] = os.WTERMSIG(status)
    else:
        res["exit"] = os.WEXITSTATUS(status)
    return res


def sh(argv, timeout=60.0, cwd=None, env=None):
    """Plain subprocess.run for control-flow tests (mutate, etc.)."""
    e = dict(os.environ)
    if env:
        e.update(env)
    return subprocess.run(argv, cwd=cwd, env=e, timeout=timeout,
                          capture_output=True, text=True)


def oc_base(oc):
    return [oc]  # --packs-dir must follow the subcommand; appended per-test


def is_san(oc):
    return "sanitize" in oc


def rss_lim(oc):
    return 1500 if is_san(oc) else 300  # ASan overhead ~10x


def wall_lim(oc):
    return 30 if is_san(oc) else 5  # ASan overhead ~5-10x


def test_t1(oc):
    d = gen_t1()
    r = run(oc_base(oc) + ["check", ".", "--packs-dir", PACKS], timeout=60, cwd=d)
    # fixed behavior: scan completes normally (exit 0/1 = findings exit),
    # bounded memory. Baseline under the same cap: 1,955 MiB / 1.5 s.
    ok = (not r.get("killed")) and r.get("exit") in (0, 1) and r["rss_mib"] < rss_lim(oc)
    return {"id": "T1 (H-1)", "r": r, "ok": ok}


def test_t5(oc):
    d = gen_t5()
    r = run(oc_base(oc) + ["check", ".", "--coverage", "cov.out",
            "--packs-dir", PACKS], timeout=60, cwd=d)
    ok = (not r.get("killed")) and r.get("exit") in (0, 2) and r["rss_mib"] < rss_lim(oc)
    return {"id": "T5 (H-3)", "r": r, "ok": ok}


def test_t6b(oc, depth=200000, timeout=25.0):
    d = gen_t6b(depth)
    r = run(oc_base(oc) + ["check", ".", "--packs-dir", PACKS], timeout=timeout * 2 if is_san(oc) else timeout, cwd=d)
    if depth > 8192:  # fixed behavior: loud, fast refusal, bounded RSS
        ok = (not r.get("killed")) and r.get("exit") == 2 and r["wall"] < wall_lim(oc) and r["rss_mib"] < rss_lim(oc)
    else:             # legitimate depth: completes normally
        ok = (not r.get("killed")) and r.get("exit") in (0, 1) and r["wall"] < timeout * 0.8
    return {"id": f"T6b d={depth} (H-5)", "r": r, "ok": ok}


def test_t6c(oc, depth=20000, timeout=25.0):
    d = gen_t6c(depth)
    r = run(oc_base(oc) + ["check", ".", "--packs-dir", PACKS], timeout=timeout * 2 if is_san(oc) else timeout, cwd=d)
    if depth > 8192:  # fixed behavior: loud, fast refusal, bounded RSS
        ok = (not r.get("killed")) and r.get("exit") == 2 and r["wall"] < wall_lim(oc) and r["rss_mib"] < rss_lim(oc)
    else:             # legitimate depth: completes normally
        ok = (not r.get("killed")) and r.get("exit") in (0, 1) and r["wall"] < timeout * 0.8
    return {"id": f"T6c arrows d={depth} (H-5)", "r": r, "ok": ok}


def test_t7(oc):
    d = gen_t7()
    r = run(oc_base(oc) + ["check", ".", "--packs-dir", PACKS], timeout=40, cwd=d,
            mem_mb=None)  # no RLIMIT: the baseline SEGV raced an arena blowup
    # fixed behavior: loud fast refusal, no signal, no blowup.
    # Baseline (unlimited): SIGSEGV exit -11 in 1.9 s at ~1.6 GB RSS.
    ok = (not r.get("killed")) and r.get("exit") == 2 and r["wall"] < wall_lim(oc) and r["rss_mib"] < rss_lim(oc)
    return {"id": "T7 (H-2)", "r": r, "ok": ok}


def test_t8_verify(oc):
    """T8 full crash-safety verification:
    (a) SIGKILL while the mutant is applied -> file mutated on disk AND
        journal present (unavoidable window; SIGKILL cannot be caught);
    (b) the NEXT oc mutate run restores the pristine bytes at startup
        (journal recovery) and leaves the file identical at exit;
    (c) SIGINT while the mutant is applied -> the handler restores the
        file IMMEDIATELY and the process exits 128+2."""
    d, src = gen_t8()
    orig = open(src).read()
    cmd = [oc, "mutate", ".", "--packs-dir", PACKS,
           "--test-cmd", "sleep 0.3 && true", "--timeout", "10"]

    def wait_mutated(deadline_s=15):
        ddl = time.time() + deadline_s
        while time.time() < ddl:
            try:
                if open(src).read() != orig:
                    return True
            except FileNotFoundError:
                pass
            time.sleep(0.002)
        return False

    # (a)+(b) SIGKILL path
    p = subprocess.Popen(cmd, cwd=d, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    mutated_seen = wait_mutated()
    os.kill(p.pid, 9)
    p.wait(timeout=10)
    corrupted = open(src).read() != orig
    journal_left = os.path.exists(src + ".oc-mutate-journal")
    rec = sh([oc, "mutate", ".", "--packs-dir", PACKS, "--test-cmd", "true",
              "--timeout", "10"], timeout=90, cwd=d)
    recovered = open(src).read() == orig
    journal_gone = not os.path.exists(src + ".oc-mutate-journal")

    # (c) SIGINT path: handler must restore instantly
    p = subprocess.Popen(cmd, cwd=d, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    mutated_seen2 = wait_mutated()
    os.kill(p.pid, 2)  # SIGINT -> mutate_fatal_signal
    p.wait(timeout=10)
    sigint_restored = open(src).read() == orig
    rc = p.returncode
    sigint_exit128 = rc in (130, 2)  # 128+SIGINT or shell-mapped
    # (d) mode preservation: the exec bit set in gen_t8 must survive the
    # SIGKILL+journal-recovery cycle AND the SIGINT-handler restore AND
    # the normal happy-path splice/restore of the recovery run.
    mode_ok = (os.stat(src).st_mode & 0o777) == 0o755
    ok = (mutated_seen and corrupted and journal_left and recovered and journal_gone
          and mutated_seen2 and sigint_restored and rc == 130 and mode_ok)
    return {"id": "T8 (H-4)", "a_sigkill_mutated": mutated_seen,
            "a_sigkill_corrupted": corrupted, "a_journal_left": journal_left,
            "b_recovered_next_run": recovered, "b_journal_cleaned": journal_gone,
            "c_sigint_restored_instant": sigint_restored, "c_sigint_exit": rc,
            "d_exec_bit_preserved": mode_ok,
            "ok": ok}


def main():
    oc = os.path.abspath(sys.argv[1])  # cwd changes inside the runners break relative paths
    mode = "verify"
    quick = "--quick" in sys.argv
    deep = not quick
    for a in sys.argv[2:]:
        if a in ("--baseline", "--verify", "--all", "--fast"):
            mode = a[2:]
    print(f"== oc_kill_highs_poc mode={mode} oc={oc} "
          f"(RLIMIT_AS={AS_LIMIT_MB}MiB) ==")
    results = []
    if mode in ("verify", "all", "fast"):
        results.append(test_t1(oc))
        results.append(test_t5(oc))
        results.append(test_t6b(oc, depth=2000 if quick else 200000))
        results.append(test_t6c(oc, depth=1000 if quick else 20000))
        results.append(test_t7(oc))
    if mode in ("verify", "all"):
        results.append(test_t8_verify(oc))
    if mode == "baseline":
        results.append(test_t6b(oc, depth=2000 if quick else 200000))
        results.append(test_t6c(oc, depth=1000 if quick else 20000))
        results.append(test_t7(oc))
    n_ok = sum(1 for r in results if r.get("ok"))
    for r in results:
        print(json.dumps(r, ensure_ascii=False))
    print(f"SUMMARY: {n_ok}/{len(results)} passed")
    print("done")
    sys.exit(0 if n_ok == len(results) else 1)


if __name__ == "__main__":
    main()
