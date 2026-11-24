# SPRINT-18 -- CI portability: the test suite runs on ANY checkout

Status: hardening sprint, 2026-11-22. Driver: the user direction
"complete all the target languages first, in order to test the project
locally and via GitHub Actions." Language completion state: all NINE
registered languages are complete and gated (javascript, typescript,
tsx, python predate the program; go, java, csharp, rust, ruby landed in
Tracks C/D through SPRINT-16) -- this sprint changes NO language code
and NO verdict logic; it removes the last machine-bound bytes from the
tree and repairs the two CI jobs the tree could never have passed, so
the repo is testable from a user clone and from a fresh GitHub Actions
run.

## 1. What was wrong (found by fresh-sandbox verification, not assumed)

The sandbox recycle wiped `work/` again; the tree was restored from the
sha256-verified SPRINT-17 archive (`c4511ec8...`) with a history
restart (`daa0efe`, the documented convention). Re-running the suite
from the restored tree surfaced three portability defects that a
single-machine workflow never notices:

1. **Two committed fixtures carried absolute machine paths.**
   `tests/fixtures/coverage/v8.json` (the V8 url
   `file:///home/z/.../examples/js/demo.js`) and
   `tests/fixtures/coverage/istanbul.json` (the top-level key) baked in
   the generating machine's repo path. The V8 adapter reads the url
   path off disk for offset-to-line mapping (an unreadable url is a
   hard error by contract, `src/coverage.c`), so `test_sprint5` and
   `test_sprint16` could only pass behind the session-local
   `omnicheck -> work/` compatibility symlink -- i.e. they FAILED on
   GitHub runners and on any user clone. This symlink is now retired;
   nothing in the committed bytes needs it.
2. **The format job would have failed.** 49 first-party files violated
   clang-format-19 (`include/oc/buildtag.h` first). The `format` job in
   `.github/workflows/ci.yml` had never actually been exercised; on the
   first real push it would have gone red.
3. **Docs LOC drift.** The README derived-LOC row lagged the tree after
   the sprint deltas (caught by `audit_docs.py` match=False; refreshed,
   match=True).

## 2. The fix

- **Fixture templates.** Both fixtures now carry the
  `@@OC_REPO_ROOT@@` placeholder where the machine path used to be. A
  `fixture_subst()` helper (static, duplicated in the two suites per
  the standalone-suite convention) substitutes the placeholder with
  THIS machine's `OC_REPO_ROOT` compile definition into a `/tmp` copy
  before loading. All three consumer sites use the substituted copy:
  `test_v8_adapter` (both fixtures), the `test_e2e_leaderboard` CLI
  command (the v8 template through the real binary), and the
  `test_sprint16` dispatch probe (istanbul). The adapter contract is
  exercised unchanged -- absolute `file://` url, file must exist -- and
  the istanbul path rule (exact-then-unique-suffix, pure string) is
  untouched. Signed arithmetic guards short repo roots.
- **clang-format-19 hygiene.** All first-party `.c`/`.h` under
  `include/ src tests` formatted with clang-format 19.1.7
  (51 files, +759/-868 lines, whitespace-only); `check-format.sh` now
  reports OK. `ci.yml` pins `clang-format-19` and puts
  `/usr/lib/llvm-19/bin` on PATH for the script (the unversioned
  `clang-format` name would soft-skip the job).
- **README** derived-LOC row refreshed to the post-format tree
  (142 files / 39,688 lines); `audit_docs.py` 33/33, match=True
  (`docs/evidence/docs_completeness.json`).

## 3. Verification (all at HEAD `1b4d6cf`)

- **Matrix, 4 build cells from scratch per cell** (the standing
  SPRINT-9 mechanism, `CTEST_PARALLEL_LEVEL=2`, 32 suites per cell),
  bundle `docs/evidence/gate_day/M5_2026-11-22/bundle.json`:
  default 66.8s, clang-release 58.7s, sanitize 377.0s,
  clang-sanitize 527.5s -- all green. This bundle REPLACES the one
  recorded mid-sprint at `42ec297` (pre-format-hygiene), so the
  evidence HEAD is the delivered HEAD.
- **Determinism: untouched by construction** -- no analyzer, resolver,
  report, or compat bytes changed; the fixtures feed test-only
  consumers and the 10k corpus digest applies to check reports.
- **Language gates: stand at their recorded verdicts** (the ten
  `lang_*`/`*_corpus` verdicts in `docs/evidence/`); this sprint
  touches nothing they measure. The corpus artifacts re-provision per
  the standing recipe when the next gate-day re-record is due.
- **vendor.sh discipline re-proven on the restore path:** configuring
  WITHOUT vendoring fails `-Werror` by design (the no-backend
  `#else` branch leaves `oc_parser_parse_file`'s `p` unused) -- exactly
  the m1-replay contract; CI orders vendor before configure, and the
  fresh-clone leg remains the standing proof.

## 4. What a fresh clone (user machine or GitHub runner) now does

`./scripts/vendor.sh && cmake --preset default -DOC_WERROR=ON &&
cmake --build --preset default -j && ctest --preset default` -- no
symlink, no path assumptions beyond the checkout itself; identical to
the `ci.yml` steps (build-test matrix ubuntu gcc/clang + macos clang,
sanitizers ubuntu ASan+UBSan, format pinned to clang-format-19). The
macos cell is the one surface this sandbox cannot pre-verify; it runs
the same offline default preset.

## 5. Non-goals

- No verdict logic, pack, grammar-pin, or compat change (B3 schema
  untouched); no corpus re-record; Track B items B1/B2 remain the
  user's own work at the program's end; the M-gate formal dates and
  the v1.0.0 tag remain calendar/human steps.
