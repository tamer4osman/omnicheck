# OmniCheck CI gate templates (WBS 3.6, Sprint 4)

Two artifacts wire the deterministic OmniCheck gate into GitHub Actions:

| File | What it is |
| --- | --- |
| `github-action/action.yml` | Composite action: builds `oc` (Release), runs `oc check` with the fail-on policy. |
| `ci/omnicheck-gate.yml` | Workflow template to copy into the analyzed repository's `.github/workflows/`. |

## What the gate enforces

- `oc check` exit code drives the job: **0** clean, **1** findings at or
  above `--fail-on` (default `warn`), **2** tool error or any analyzed
  file that did not parse cleanly. A broken file can never produce a
  silent green run (OmniCheck honesty contract, M1 kill-switch criterion
  #2).
- Findings and fingerprints are byte-stable across runs and machines:
  re-runs are diff-safe, and the report is small enough to attach to the
  job log (`--format human` renders the grouped text report).

## Wiring options

1. **Engine checkout (shown in the workflow template):** check out the
   OmniCheck engine beside the caller's sources and reference the local
   composite action (`uses: ./omnicheck-engine/templates/github-action`).
2. **Prebuilt binary:** set the `oc-bin` input; both build steps are
   skipped and the gate step runs the given binary directly.

## Inputs

| Input | Default | Meaning |
| --- | --- | --- |
| `paths` | `.` | Analysis roots (space separated). |
| `packs-dir` | `packs` | Declarative language pack directory. |
| `fail-on` | `warn` | Exit-1 threshold: `info` \| `warn` \| `error`. |
| `config` | *(empty)* | One `KEY=VALUE` per line, forwarded as `--config` (e.g. `OC001.max_lines=40`). |
| `coverage` | *(empty)* | One coverage JSON per line (Istanbul / coverage.py), enables OC007 CRAP-ms. |
| `triage` | `false` | `true` enables the OmniDecide rank-only sidecar (fail-open; never decides alone). |
| `oc-bin` | *(empty)* | Prebuilt `oc` binary; skips the vendored-grammar fetch and build. |

## Multi-language analysis

The packs are declarative (`packs/*/pack.json`): JavaScript, TypeScript,
TSX and Python are registered, not trained. A repository containing any
of those languages is analyzed end-to-end with no per-language setup;
parse failures are named per file and fail the job with exit 2.
