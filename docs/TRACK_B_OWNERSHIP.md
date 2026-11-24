# Track B ownership decision (B1-B8 deferred upgrades)

Status: decision record, 2026-11-22. Supersedes the "awaiting explicit
user decision" note in the handoff addenda (v6 line 85, v8 line 178)
and closes item 4 of `OmniCheck_Project_Plan.md` section 9.3 for B1/B2.

## Definitions recovered (2026-11-22)

The B1/B2 definitions were recovered from
`upload/OmniCheck_Project_Plan.md` section 9.3 item 4 (line 836).
They were never in-repo; the earlier search covered the repo, git
history and handoff addenda but missed the original upload. Full
Track B list (protocol: present options + evidence, get a decision,
then build):

- B1 -- TS config parsing scope: tsconfig `extends`/`references`.
- B2 -- unused-exports scope: wildcard/require/dynamic forms.
- B3 -- compat signature diffs.
- B4 -- LSP descope items.
- B5 -- node_modules resolution.
- B6 -- Istanbul per-function counters.
- B7 -- mutation on huge repos.
- B8 -- runner per-leg completion.

## The decision (user, 2026-11-22)

"I will handle the B1/B2 code testing on my local machine, as well as
the GitHub Actions development environment; I will take care of this
at the end."

Scope of the decision:

- B1/B2 implementation AND testing move to the user, on the user's
  local machine, deferred to the end of the program. No agent-side
  build of B1/B2 is authorized; do not pre-build.
- The GitHub Actions development environment also moves to the user.
  The repo already ships `.github/workflows/ci.yml` with three jobs
  (build-test matrix: ubuntu-latest x {gcc, clang} + macos-latest x
  clang, warnings-as-errors; sanitizers: ASan+UBSan, leak detection
  on; format: clang-format via `scripts/check-format.sh`). The user's
  work extends this existing workflow; it does not start from zero.

## Implications

- B4-B8 remain decision-pending under the same protocol; do not
  pre-build (plan section 9.3 item 4 still governs them). B3 is
  DECIDED (2026-11-22, Option B typed-where-declared, build as the
  next session's sprint unit -- see `docs/B3_COMPAT_SIGNATURE_DIFFS.md`
  section 7); its build session is authorized, nothing else is.
- With B1/B2 owned, every item of plan section 9.3 now has an owner:
  items 1-2 are calendar/human rituals (M1 drift re-check 2026-12-31,
  formal gates M2-M5 across 2027, v1.0.0 tag 2027-12-31, no AI, no
  early tagging); item 3 is data-gated (L3 calibration waits for real
  outcome data; L4/L7 unlock only behind it); item 4 is B1/B2 (user)
  plus B3-B8 (user decisions pending); item 5, Track D language
  expansion, is complete through Ruby (Go, Java, C#, Rust, Ruby all
  delivered and gated).

## Provenance note (2026-11-22 sandbox incident)

The session sandbox recycle wiped the working tree, including `.git`.
The tree was restored from `omnicheck-sprint16-ruby.tar.gz` (git
archive at 26de777, sha256 sidecar verified OK, 581/581 files). The
reconstructed git repository has no history continuity before this
commit; the archives in `download/` remain the provenance-pinned
deliverables, and the handoff addenda (v7, v8) remain the authoritative
sprint records.
