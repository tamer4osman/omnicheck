# Screenshots (the README capture pipeline)

The README's terminal captures are produced from the **real shipped binary**
by `scripts/gen_screenshots.py` — the report text rendered inside every PNG is
byte-exact tool output; only terminal presentation (window chrome, token
coloring) is added. No scenario is staged, mocked or edited after the fact.

## The byte-exact claim, precisely

For each capture the pipeline runs one `oc` command, captures stdout+stderr
verbatim, and renders those bytes into a dark terminal window. What is
ADDED vs what is KEPT:

| Kept (byte-exact) | Added (presentation only) |
| --- | --- |
| every report line, in order, including fingerprints and counters | window chrome (traffic lights, title with the real exit code) |
| the exact command line, echoed in the prompt row | token coloring (severities, paths, badges, JSON) |
| exit codes as the tool returned them | the prompt prefix (`❯ omnicheck git:(main)`) |

Captures exit `1` by design: findings sit at/above the `--fail-on warn`
threshold, and `oc compat` exits `1` on a BREAKING verdict — that IS the
contract working.

## The six captures

| File | Command (relative to the repo root) |
| --- | --- |
| `oc-check-human.png` | `oc check examples --packs-dir packs --format human` |
| `oc-check-triage.png` | same + `--triage`, with `OC_OMNIDECIDE_BIN=build/default/oc-omnidecide-provider` |
| `oc-crap-leaderboard.png` | `oc check examples … --coverage <v8> --coverage <pycov>` (fixtures re-rooted from `tests/fixtures/coverage/`) |
| `oc-supply-chain.png` | `oc check tests/fixtures/supply --osv-snapshot tests/fixtures/supply/osv_snapshot.json --format human` |
| `oc-compat-gate.png` | `oc compat --baseline tests/fixtures/compat/v1 --current tests/fixtures/compat/v2 --packs-dir packs` |
| `oc-language-matrix.png` | the 15-language capability matrix rendered as a FIGURE (clearly not a terminal capture; data mirrors the README matrix table) |

## Regenerate

```sh
cmake --build --preset default -j        # the captures drive the real binary
python3 scripts/gen_screenshots.py       # writes docs/screenshots/*.png
```

Requirements: Playwright (Chromium) and Pillow. The script substitutes the
`@@OC_REPO_ROOT@@` placeholder of the tracked coverage fixtures into temp
copies (the same mechanism `m4_gate.py` uses), runs the five commands, renders
each capture at 2x device scale, then palette-quantizes the PNG — the five
terminal captures + the matrix figure weigh ~0.8 MB combined (the pre-pipeline
SPRINT-8 captures weighed ~2.2 MB for four).

## Why the pipeline is committed

The SPRINT-8-era captures showed a `packs:` line with four languages and
survived nine adoptions as "dated evidence until the screenshot pipeline
regenerates them" — drift by design debt. The pipeline is now one command:
after every adoption sprint, re-run it so the captures track the live
registry (SPRINT-24 captures show all fifteen packs).
