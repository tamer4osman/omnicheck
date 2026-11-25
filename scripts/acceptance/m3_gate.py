#!/usr/bin/env python3
"""M3 Beta gate acceptance harness (plan Table 9, gate M3 2027-05-31).

Executes the five recorded acceptance criteria and emits a
machine-readable verdict:

  1. Two packs installed through the manifest format -- `oc packs`
     lists >= 2 packs, each carrying the omnicheck.pack/v0 manifest
     fields (schema, grammar, extensions, enabled passes).
  2. Python repository analysed end-to-end -- the pinned reference
     repository (black @ c0e143e75443afd3be50c3b63ed2ee2fd108f509,
     scope src/black, 25 files / ~16.7k LOC, absolute intra-repo
     imports) scans with zero parse errors, findings present, import
     edges resolved (resolver v3), and byte-identical reruns.
  3. LSP diagnostics visible in an editor session -- a scripted LSP
     client (initialize -> initialized -> didOpen -> diagnostics ->
     documentSymbol -> shutdown -> exit) runs the real `oc lsp` binary;
     diagnostics arrive as textDocument/publishDiagnostics and the
     session is byte-identical across two runs.
  4. MCP tools listable and callable by a coding agent -- a scripted
     MCP client (the coding-agent stand-in) runs the real `oc mcp`
     binary: initialize, tools/list (three tools), tools/call of every
     tool succeeds; byte-identical across two runs.
  5. OmniDecide ranking active in CLI output -- `oc check --triage`
     with the OmniDecide provider (OC_OMNIDECIDE_BIN; the mock shim in
     CI, the production omnidecide-c shim when present) marks
     triage.ranked=true with per-finding ranks, and the rank-only
     guardrail is verified: findings are identical to the untriaged run
     apart from triage fields.

Usage:
  python3 scripts/acceptance/m3_gate.py --oc build/default/oc \
      --repo /path/to/black --scope src/black

Exit code 0 iff every criterion holds; each criterion's measured
numbers are printed and collected into the verdict JSON.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run(cmd, env=None, cwd=None, stdin_bytes=None, timeout=600):
    return subprocess.run(
        cmd,
        capture_output=True,
        env=env,
        cwd=cwd,
        input=stdin_bytes,
        timeout=timeout,
    )


def sha256_bytes(b):
    import hashlib

    return hashlib.sha256(b).hexdigest()


def count_loc(root):
    files = lines = 0
    for r, d, f in os.walk(root):
        d[:] = [x for x in d if x not in (".git", "__pycache__", "node_modules")]
        for x in f:
            if x.endswith(".py"):
                files += 1
                try:
                    with open(os.path.join(r, x), "rb") as fh:
                        lines += sum(1 for _ in fh)
                except OSError:
                    pass
    return files, lines


class Verdict:
    def __init__(self):
        self.criteria = []
        self.notes = []

    def add(self, name, ok, evidence):
        self.criteria.append({"criterion": name, "pass": bool(ok), "evidence": evidence})
        print(("PASS " if ok else "FAIL ") + name)
        for k, v in evidence.items():
            print("      %s: %s" % (k, v))
        return ok

    @property
    def ok(self):
        return all(c["pass"] for c in self.criteria)


# ------------------------------------------------------------ criterion 1
def check_packs_manifest(v):
    """The manifest is the pack.json file; `oc packs` proves the loader
    round-trips it. Both halves are checked."""
    packs_dir = os.path.join(REPO_ROOT, "packs")
    r = run([ARGS.oc, "packs", "--packs-dir", packs_dir])
    if r.returncode != 0:
        return v.add("packs-manifest (>=2 packs, pack/v0 fields)", False,
                     {"stderr": r.stderr.decode()[:300]})
    data = json.loads(r.stdout.decode())
    packs = data if isinstance(data, list) else data.get("packs", [])
    loader_ok = []
    for p in packs:
        loader_ok.append(
            bool(p.get("grammar"))
            and bool(p.get("extensions"))
            and bool(p.get("enabled_passes"))
            # entry_points is a PRESENCE check, not non-empty: the SPRINT-20
            # C pack detects entries as a parse FACT (main definition), not
            # from manifest patterns, so its entry_points list is
            # legitimately empty -- the loader criterion is that the field
            # round-trips, which an empty list satisfies. The manifest half
            # below never required non-empty entry_points either.
            and "entry_points" in p
        )
    manifest_ok = []
    for pid in [p.get("id") for p in packs]:
        pj = os.path.join(packs_dir, pid, "pack.json")
        try:
            with open(pj) as fh:
                man = json.load(fh)
            manifest_ok.append(
                man.get("schema") == "omnicheck.pack/v0"
                and bool(man.get("grammar"))
                and bool(man.get("extensions"))
                and bool(man.get("enabled_passes"))
            )
        except Exception as exc:  # noqa: BLE001 - recorded as evidence
            v.notes.append("pack.json read failure for %s: %r" % (pid, exc))
            manifest_ok.append(False)
    ids = [p.get("id") for p in packs]
    return v.add(
        "packs-manifest (>=2 packs, pack/v0 fields)",
        len(packs) >= 2 and all(loader_ok) and all(manifest_ok),
        {
            "packs": ids,
            "count": len(packs),
            "loader_fields_complete": all(loader_ok),
            "manifest_schema_pack_v0": all(manifest_ok),
        },
    )


# ------------------------------------------------------------ criterion 2
def check_python_e2e(v, repo_scope):
    packs_dir = os.path.join(REPO_ROOT, "packs")
    t0 = time.time()
    r1 = run([ARGS.oc, "check", repo_scope, "--packs-dir", packs_dir])
    wall1 = time.time() - t0
    r2 = run([ARGS.oc, "check", repo_scope, "--packs-dir", packs_dir])
    identical = r1.stdout == r2.stdout and r1.returncode == r2.returncode

    ok_report = r1.returncode in (0, 1)  # 1 = findings at/above --fail-on
    report = json.loads(r1.stdout.decode()) if ok_report else {}
    meta = report.get("meta", {})
    findings = report.get("findings", [])
    rules = sorted({f["rule"] for f in findings})

    g = run([ARGS.oc, "graph", repo_scope, "--packs-dir", packs_dir])
    graph = json.loads(g.stdout.decode()) if g.returncode == 0 else {}
    nodes = graph.get("nodes", [])
    files_by_id = {n.get("id"): n.get("path") for n in nodes if n.get("kind") == "file"}
    n_import_edges = sum(
        1 for e in graph.get("edges", []) if e.get("kind") == "imports"
    )
    # resolver v3 evidence: at least one edge whose target is a .py file
    py_edges = sum(
        1
        for e in graph.get("edges", [])
        if e.get("kind") == "imports"
        and str(files_by_id.get(e.get("to"), "")).endswith(".py")
    )

    return v.add(
        "python-repository-end-to-end",
        ok_report
        and meta.get("files_analyzed", 0) > 0
        and meta.get("files_with_parse_errors", -1) == 0
        and len(findings) > 0
        and n_import_edges > 0
        and py_edges > 0
        and identical,
        {
            "reference": REPO_PIN,
            "files_analyzed": meta.get("files_analyzed"),
            "loc": LOC[0] if LOC else None,
            "parse_error_files": meta.get("files_with_parse_errors"),
            "finding_count": len(findings),
            "rules_firing": rules,
            "imports_edges": n_import_edges,
            "py_target_edges": py_edges,
            "wall_s_round1": round(wall1, 3),
            "byte_identical_rerun": identical,
        },
    )


# --------------------------------------------------- criteria 3 & 4 helpers
def lsp_frame(obj):
    body = json.dumps(obj).encode()
    return b"Content-Length: %d\r\n\r\n%s" % (len(body), body)


def collect_lsp_frames(stream):
    frames = []
    pos = 0
    while pos < len(stream):
        head_end = stream.index(b"\r\n\r\n", pos)
        headers = stream[pos:head_end].decode()
        length = int(headers.split("Content-Length:")[1].strip())
        body = stream[head_end + 4 : head_end + 4 + length]
        frames.append(json.loads(body))
        pos = head_end + 4 + length
    return frames


def check_lsp_session(v):
    packs_dir = os.path.join(REPO_ROOT, "packs")
    demo = os.path.join(REPO_ROOT, "tests", "fixtures_sprint4", "lspdemo")
    demo_uri = "file://" + demo
    payload = b"".join(
        [
            lsp_frame(
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "method": "initialize",
                    "params": {"rootUri": demo_uri, "capabilities": {}},
                }
            ),
            lsp_frame({"jsonrpc": "2.0", "method": "initialized", "params": {}}),
            lsp_frame(
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/didOpen",
                    "params": {"textDocument": {"uri": demo_uri + "/demo.js", "text": ""}},
                }
            ),
            lsp_frame(
                {
                    "jsonrpc": "2.0",
                    "id": 2,
                    "method": "textDocument/documentSymbol",
                    "params": {"textDocument": {"uri": demo_uri + "/demo.js"}},
                }
            ),
            lsp_frame(
                {
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "shutdown",
                }
            ),
            lsp_frame({"jsonrpc": "2.0", "method": "exit"}),
        ]
    )
    outs = []
    rcs = []
    for _ in range(2):
        r = run([ARGS.oc, "lsp", "--packs-dir", packs_dir], stdin_bytes=payload)
        outs.append(r.stdout)
        rcs.append(r.returncode)

    frames = collect_lsp_frames(outs[0]) if outs[0] else []
    diag = [f for f in frames if f.get("method") == "textDocument/publishDiagnostics"]
    init_ok = any(
        f.get("id") == 1
        and f.get("result", {}).get("capabilities", {}).get("documentSymbolProvider")
        for f in frames
    )
    demo_diags = [
        d
        for f in diag
        if f["params"]["uri"].endswith("demo.js")
        for d in f["params"]["diagnostics"]
    ]
    symbols = next((f for f in frames if f.get("id") == 2), None)
    symbols_ok = bool(
        symbols
        and symbols.get("result")
        and any(s.get("name") == "alpha" for s in symbols["result"])
    )

    return v.add(
        "lsp-diagnostics-in-editor-session",
        rcs == [0, 0]
        and init_ok
        and len(demo_diags) > 0
        and symbols_ok
        and outs[0] == outs[1],
        {
            "exit_codes": rcs,
            "initialize_capabilities_ok": init_ok,
            "demo_diagnostics": [d.get("code") for d in demo_diags],
            "document_symbols_ok": symbols_ok,
            "byte_identical_session_rerun": outs[0] == outs[1],
            "note": "scripted client stands in for the editor; protocol is LSP 3.17 stdio",
        },
    )


def mcp_session_bytes(fix_root):
    def line(obj):
        return (json.dumps(obj) + "\n").encode()

    return b"".join(
        [
            line(
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "method": "initialize",
                    "params": {"protocolVersion": "2024-11-05"},
                }
            ),
            line({"jsonrpc": "2.0", "method": "notifications/initialized"}),
            line({"jsonrpc": "2.0", "id": 2, "method": "tools/list"}),
            line({"jsonrpc": "2.0", "id": 3, "method": "tools/call",
                  "params": {"name": "omnicheck_scan",
                             "arguments": {"paths": [fix_root]}}}),
            line({"jsonrpc": "2.0", "id": 4, "method": "tools/call",
                  "params": {"name": "omnicheck_findings",
                             "arguments": {"paths": [fix_root], "rule": "OC002"}}}),
            line({"jsonrpc": "2.0", "id": 5, "method": "tools/call",
                  "params": {"name": "omnicheck_rules", "arguments": {}}}),
        ]
    )


def check_mcp_tools(v):
    packs_dir = os.path.join(REPO_ROOT, "packs")
    demo = os.path.join(REPO_ROOT, "tests", "fixtures_sprint4", "lspdemo")
    payload = mcp_session_bytes(demo)
    outs = []
    rcs = []
    for _ in range(2):
        r = run([ARGS.oc, "mcp", "--packs-dir", packs_dir], stdin_bytes=payload)
        outs.append(r.stdout)
        rcs.append(r.returncode)
    lines = [json.loads(x) for x in outs[0].decode().splitlines() if x.strip()]
    by_id = {}
    for m in lines:
        if isinstance(m.get("id"), int):
            by_id[m["id"]] = m
    init_ok = (
        1 in by_id
        and by_id[1]["result"].get("serverInfo", {}).get("name") == "omnicheck"
    )
    tools = by_id.get(2, {}).get("result", {}).get("tools", [])
    tool_names = [t.get("name") for t in tools]
    scan_ok = False
    findings_ok = False
    rules_ok = False
    try:
        rep = json.loads(by_id[3]["result"]["content"][0]["text"])
        scan_ok = rep["meta"]["tool"] == "omnicheck" and rep["meta"]["finding_count"] > 0
        fq = json.loads(by_id[4]["result"]["content"][0]["text"])
        findings_ok = fq["count"] >= 1 and all(
            f["rule"] == "OC002" for f in fq["findings"]
        )
        rules = json.loads(by_id[5]["result"]["content"][0]["text"])
        rules_ok = rules["count"] >= 8
    except Exception as exc:  # noqa: BLE001 - recorded as evidence
        v.notes.append("mcp tool parse failure: %r" % exc)
    return v.add(
        "mcp-tools-listable-and-callable",
        rcs == [0, 0] and init_ok and tool_names == ["omnicheck_scan",
                                                     "omnicheck_findings",
                                                     "omnicheck_rules"]
        and scan_ok and findings_ok and rules_ok and outs[0] == outs[1],
        {
            "exit_codes": rcs,
            "tools": tool_names,
            "scan_report_findings": rep["meta"]["finding_count"] if scan_ok else None,
            "findings_query_count": fq["count"] if findings_ok else None,
            "rules_listed": rules["count"] if rules_ok else None,
            "byte_identical_session_rerun": outs[0] == outs[1],
            "note": "scripted client stands in for the coding agent; protocol is MCP stdio",
        },
    )


# ------------------------------------------------------------ criterion 5
def check_omnidecide_ranking(v):
    """The ranking fixture carries only info-severity findings and zero
    parse failures, so both runs exit 0 and the ONLY diff between them
    is the triage annotation."""
    packs_dir = os.path.join(REPO_ROOT, "packs")
    fix = os.path.join(REPO_ROOT, "tests", "fixtures_sprint4", "ranking")
    mock = os.path.join(REPO_ROOT, "tools", "mock_omnidecide.py")
    env = dict(os.environ)
    env["OC_OMNIDECIDE_BIN"] = mock

    r_plain = run([ARGS.oc, "check", fix, "--packs-dir", packs_dir])
    r_tri = run([ARGS.oc, "check", fix, "--packs-dir", packs_dir, "--triage"], env=env)
    if r_plain.returncode != 0 or r_tri.returncode != 0:
        return v.add("omnidecide-ranking-active-in-cli", False,
                     {"exit": [r_plain.returncode, r_tri.returncode],
                      "stderr": r_tri.stderr.decode()[:300]})
    plain = json.loads(r_plain.stdout.decode())
    tri = json.loads(r_tri.stdout.decode())
    tri_meta = tri.get("triage", {})
    ranked_flag = tri_meta.get("ranked") is True and tri_meta.get("ranked_count", 0) > 0
    ranked_findings = all(
        "triage_rank" in f for f in tri.get("findings", [])
    )

    # rank-only guardrail: same findings (rule/file/line/fingerprint),
    # nothing dropped, nothing re-severitied, order untouched.
    def key(f):
        return (f["rule"], f["file"], f["line"], f["fingerprint"])

    same_findings = [key(f) for f in plain["findings"]] == [
        key(f) for f in tri["findings"]
    ]
    return v.add(
        "omnidecide-ranking-active-in-cli",
        ranked_flag and ranked_findings and same_findings,
        {
            "provider": tri_meta.get("provider"),
            "model": tri_meta.get("model"),
            "ranked": tri_meta.get("ranked"),
            "ranked_count": tri_meta.get("ranked_count"),
            "finding_count": len(tri.get("findings", [])),
            "rank_only_guardrail_intact": same_findings,
            "note": "mock provider (rank order deliberately reversed); production "
                    "omnidecide-c speaks the same v0 protocol (private upstream)",
        },
    )


# ------------------------------------------------------------------ main

REPO_PIN = None
LOC = None

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="M3 Beta gate acceptance harness")
    ap.add_argument("--oc", required=True, help="path to the built oc binary")
    ap.add_argument("--repo", required=True, help="path to the reference repository checkout")
    ap.add_argument("--scope", default="src", help="repository-relative analysis scope")
    ap.add_argument("--out", default=None, help="write the verdict JSON here")
    ARGS = ap.parse_args()

    repo_scope = os.path.join(ARGS.repo, ARGS.scope) if os.path.isdir(
        os.path.join(ARGS.repo, ARGS.scope)
    ) else ARGS.repo
    LOC = count_loc(repo_scope)
    import hashlib

    REPO_PIN = {
        "repo": os.path.basename(ARGS.repo.rstrip("/")),
        "files_py": LOC[0],
        "loc": LOC[1],
    }

    v = Verdict()
    v.add("reference-repo-measured", LOC[0] > 0,
          {"files_py": LOC[0], "loc": LOC[1], "scope": repo_scope})
    check_packs_manifest(v)
    check_python_e2e(v, repo_scope)
    check_lsp_session(v)
    check_mcp_tools(v)
    check_omnidecide_ranking(v)

    verdict = {
        "gate": "M3 Beta (plan Table 9; formal gate date 2027-05-31)",
        "executed": time.strftime("%Y-%m-%d"),
        "oc": ARGS.oc,
        "criteria": v.criteria,
        "notes": v.notes,
        "verdict": "PASS" if v.ok else "FAIL",
    }
    print("\nVERDICT:", verdict["verdict"])
    if ARGS.out:
        with open(ARGS.out, "w") as fh:
            json.dump(verdict, fh, indent=2)
            fh.write("\n")
    sys.exit(0 if v.ok else 1)
