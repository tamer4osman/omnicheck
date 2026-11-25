# Sprint 4 (M3 Beta scope) - LSP server, MCP server, CI templates, resolver v3

Committed as one sprint on 2026-11-18. Everything below was verified by
direct execution. The gate criteria of M3 Beta (plan Table 9, formal
date 2027-05-31) are all PASS with committed, replayable evidence
(`scripts/acceptance/m3_gate.py`, `docs/evidence/m3_gate_verdict.json`).

## Session context (honesty first)

- NO workspace wipe this session either; the sprint started from the
  live tree at `b4ce01a`. One sandbox-recycle artifact: all 130 tracked
  files flipped to mode 100755 (zero content changes); handled by
  `git config core.fileMode false` instead of a checkout, since the
  bits are a property of the recycled workspace, not the repo.
- cmake reinstalled via pip (4.4.5); gcc 14.2 present; **clang still
  absent (no sudo, FOURTH session)** -- the gcc-vs-clang byte-stability
  re-verification stays CARRIED on the same terms as the M1 gate review
  (Sprint 0 cross-compiler evidence stands).
- The previous sprint record corrected the "M2 Alpha = WBS 4.x"
  shorthand; this sprint's scope follows the corrected plan reading:
  M3 Beta scope = WBS 3.3 / 3.4 / 3.6 + the two resolver descopes that
  SPRINT-3 recorded against M3 (resolver v3 landed; package.json
  exports / node_modules resolution STAYS DESCOPED -- see below).
- The M2 acceptance harness was NOT re-run this session (the eslint
  reference checkout was not re-fetched after the /tmp recycle); the
  M2 evidence of SPRINT-3 stands, and the scan-pipeline refactor is
  separately proven byte-stable on this repo (examples + fixtures +
  10k corpus byte-identical, all finding counts matching Sprint 3).

## Delivered

1. **Shared scan pipeline (`src/scan.c` + `include/oc/scan.h`).**
   The full `cmd_check` pipeline moved verbatim into `oc_scan_run`;
   `oc check` now renders from the shared result. Proof of no drift:
   `oc check examples --packs-dir packs` (JSON + human) and the
   fixtures run are byte-identical before/after (cmp), and the 15
   pre-existing test binaries stayed green untouched. The graph stays
   reachable from the result (document symbols), parse errors remain
   named records, and the result is destroy-safe on failure paths.
2. **Shared report contract (`src/report.c` + `include/oc/report.h`).**
   The JSON and human renderers moved out of the CLI so `oc check`,
   `oc mcp omnicheck_scan` (and any future surface) emit the same
   `omnicheck.report/v0` bytes for the same inputs.
3. **Resolver v3 (`imports.c`, Python absolute intra-repo imports).**
   Package-root probing: the importing file's nearest ancestor without
   an `__init__.py` is the root (`<spec>.py`, then `<spec>/__init__.py`);
   fallback to each analysis root in command-line order (src-layout).
   Stdlib / site-packages names match no intra-repo file and stay
   unresolved -- never guessed, so an edge still always means "target
   is part of this analysis". Effect measured on the fixtures: modules
   referenced ONLY through absolute imports stop being OC006
   false-positives; `import mypkg.cycle_b` absolute cycles now reach
   OC003. `from pkg import name` honestly resolves the specifier `pkg`
   to the package `__init__.py` (the record carries specifiers only).
4. **LSP server (`src/lsp.c`, `oc lsp`, WBS 3.3).** LSP 3.17 over
   stdio: initialize (capabilities + rootUri scan root), didOpen /
   didChange / didSave -> fresh scan -> `textDocument/publishDiagnostics`
   (findings in canonical order; parse errors as severity-1
   `parse-error` diagnostics), `textDocument/documentSymbol` (flat
   DocumentSymbol[] from the graph), shutdown/exit per spec. Fail-open
   session (a failed scan is logged, never fatal); unknown requests get
   -32601; two real bugs caught before landing: an uninitialized-writer
   brace bug (initialize response missing its two closing braces) and a
   dangling workspace-root pointer into the per-message arena (root is
   now owned by session state). Both are exactly the class of bug the
   scripted-client smoke caught before the C test was written.
5. **MCP server (`src/mcp.c`, `oc mcp`, WBS 3.4).** MCP stdio transport
   (newline-delimited JSON-RPC): initialize (echoes the client's
   protocolVersion), `tools/list` = three tools with JSON schemas,
   `tools/call`: `omnicheck_scan` (full deterministic report),
   `omnicheck_findings` (rule / file_contains / severity / limit
   filters over canonically sorted findings), `omnicheck_rules` (the
   8-rule registry). Unknown tool = -32602; scan execution failure =
   `isError:true` result (fail-open); session ends at stdin EOF.
6. **CI gate templates (`templates/`, WBS 3.6).** Composite GitHub
   Action (vendor-if-missing -> cmake Release -> `oc check --fail-on`),
   workflow template, README. Both YAML files parse (PyYAML-validated
   during development); structural checks are committed in
   `tests/test_sprint4.c` (inputs, steps, gate command, vendor seam).
7. **M3 gate harness (`scripts/acceptance/m3_gate.py`).** All five
   Table-9 criteria executable and replayable; reference repository
   PINNED below; verdict committed with measured evidence.

## Reference repository (pinned) and gate verdict

**psf/black @ `c0e143e75443afd3be50c3b63ed2ee2fd108f509`, scope
`src/black` -- 25 files / 16,731 LOC Python, absolute intra-repo
imports throughout** (`from black.cache import ...`), fetched as a
codeload tarball at the full SHA (short-SHA fetches are rejected by
the server; same replay procedure as the M2 chalk/eslint pins).
Candidates rich/requests were considered; black has the highest
density of absolute intra-repo imports, which is exactly what resolver
v3 needs a real repository for.

M3 gate criteria (gcc 14.2 Release):

| Criterion (plan Table 9) | Measured |
| --- | --- |
| Two packs installed through the manifest format | 4 packs (ts-js, python, tsx, typescript); loader fields complete; every pack.json carries `omnicheck.pack/v0` |
| Python repository analysed end-to-end | 25/25 analyzed, 0 parse errors, 257 findings across 7 rules, 70 imports edges (100% py-target -- resolver v3 live on a real repo), byte-identical rerun, 0.181 s wall |
| LSP diagnostics visible in an editor session | Scripted LSP 3.17 session over the real binary: initialize OK, publishDiagnostics (OC002/OC006 + parse-error classes), documentSymbols (Function 12), byte-identical session rerun |
| MCP tools listable and callable by a coding agent | Scripted MCP client: 3 tools listed in fixed order with schemas; scan / findings / rules calls all succeed; byte-identical session rerun |
| OmniDecide ranking active in CLI output | `oc check --triage` with the mock provider: `triage.ranked=true`, every finding carries `triage_rank`, and the rank-only guardrail verified (findings identical to the untriaged run apart from triage fields) |

**Verdict: PASS on all five criteria** (formal gate date 2027-05-31 is
unchanged; the harness re-runs near the gate date like M1/M2).

## Test suite: 16 -> 18 test binaries, all green in gcc Release

- `tests/test_sprint4.c` (58 checks): resolver v3 unit checks on a
  synthetic graph (package-root probing, src-layout shadowing the
  analysis root, root fallback, script-dir sibling, stdlib names NULL,
  relative imports unchanged), fixture checks through the real binary
  (exact edges for the five absolute-import shapes, NO edge for the
  stdlib `json` import, OC003 fires on the absolute-import cycle, the
  four absolutely-referenced modules are NOT OC006-flagged,
  byte-identical rerun), template structural checks.
- `tests/test_lsp.c` (35 checks): framing units (Content-Length CRLF +
  bare-LF tolerance, EOF, write/read roundtrip via tmpfile), URI
  roundtrip (`%20` encode/decode; non-file URIs rejected), and the
  full scripted session against the real `oc lsp` (capabilities,
  diagnostics incl. the severity-1 parse-error class, symbols, -32601
  for unknown requests, clean exit 0 after shutdown, exactly 6 frames).
- `tests/test_mcp.c`: two full sessions against the real `oc mcp`
  (initialize echo, 3 tools in order with object schemas, rules count
  8 with OC001 first, scan = report contract with parse_errors/triage
  blocks, findings filter rule=OC002 limit=2, -32602 unknown tool,
  -32601 unknown method, exit 0 at EOF, byte-identical transcripts).
- 16/16 pre-existing binaries green untouched (scan refactor proved
  byte-stable before any new surface was built on it).

## Performance budget (sprint gate 90 s single-core)

10k-file corpus (same generator as Sprints 2-3): **8.87 s / 8.80 s**
two consecutive runs, byte-identical, exactly 10,000 OC002 + 200 OC003
+ 20,000 OC008 findings, 0 parse errors. Sprint 3 measured 9.76 s with
the same corpus -- resolver v3 added no measurable regression (the
probe cost is a binary search per specifier).

## Explicit descopes (descope-not-slip, recorded before they hurt)

- **package.json exports / node_modules resolution**: unchanged from
  the Sprint 2 descope, still recorded against M4 scope (WBS 4.x per
  the corrected plan reading).
- **LSP buffer-state analysis**: didOpen/didChange/didSave trigger
  disk-state scans; unsaved buffer text is not analyzed. Recorded in
  oc/lsp.h and ARCHITECTURE.md; incremental sync + buffer analysis is
  editor-grade work deferred until a real editor integration exists.
- **LSP UTF-16 positions**: columns are byte offsets (tree-sitter);
  UTF-16 mapping deferred (affects only non-ASCII line columns).
- **LSP incremental document cache**: fresh scan per event. Editors
  on the 10k-corpus scale would feel this; correctness first, cache
  behind a future descope note.
- **MCP resources/prompts**: tools only (the WBS item says "findings
  query tools"); resources/prompts are not in the WBS line item.
- **gcc-vs-clang byte-stability re-verification**: carried a FOURTH
  session (no clang, no sudo). Sprint 0 cross-compiler evidence stands;
  M1 gate review records it as the one carried evidence item.

## Documentation

- docs/ARCHITECTURE.md: resolver v3 contract (item 3 rewritten), new
  sections: shared scan pipeline + report contract, LSP server
  contract, MCP server contract, CI gate templates.
- README.md: `oc lsp` / `oc mcp` quickstart, resolver v3 note, M3
  acceptance section with the black pin.
- docs/evidence/m3_gate_verdict.json: committed verdict with measured
  evidence and the reference pin.

## M3 gate status (2027-05-31 formal date)

All five acceptance criteria PASS with committed, replayable evidence.
Remaining before the formal gate date: the carried gcc-vs-clang
re-verification (fourth session and counting), and a harness re-run
near the gate date to confirm no drift. Next sprint (M4 RC scope per
plan Table 9): coverage adapters for the CRAP leaderboard (WBS 4.3),
mutation engine (4.5), breaking-change gate (4.6), supply-chain
health (4.2) -- the CRAP-ms v0 composition from Sprint 2 already
covers the 4.3 substance the verdict report weakened to "composition
increment"; the mutation-validated form stays behind 4.5 by design.
