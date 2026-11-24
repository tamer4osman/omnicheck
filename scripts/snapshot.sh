#!/usr/bin/env bash
# Cut a sprint snapshot with git-archive semantics: TRACKED tree content
# only, straight from the committed HEAD (provenance: the artifact is the
# commit).
#
# Why this exists (the sprint22/23 size regression): the sprint20/21
# snapshots were effectively tracked-content-only (~3.1 MB), but the
# sprint22/23 cuts swept the working directory with plain `tar .` and
# dragged in the UNTRACKED payload -- the vendored tree-sitter grammar
# trees (~275 MB: generated parser tables, the c-sharp npm package's six
# prebuilt .node binaries and .wasm bundle, the swift grammar's three
# 18 MB parser variants), the build/ tree, and the untracked corpus
# extractions. The artifact jumped 3.1 MB -> 24.8 MB with ZERO project
# source growth (the tracked tree is ~9 MB).
#
# The untracked payload is untracked BY DESIGN (.gitignore):
#   - third_party/* (except CMakeLists.txt and omnidecide/) comes back
#     via `sh scripts/vendor.sh` -- pinned refs + sha256-verified npm
#     fallbacks recorded in the script, byte-identity proof per grammar
#     in THIRD_PARTY_NOTICES.md / docs/evidence;
#   - corpus archives (curl 7.88.1, abseil 20240722.0, ktor 3.0.1,
#     swift-argument-parser 1.5.0) re-provision per the gate hints;
#   - build/ and dist/ regenerate.
# The restore checklist (handoff addenda) is: untar, vendor.sh,
# provision corpora if corpus gates are to run, cmake, ctest, gates.
#
# Usage: scripts/snapshot.sh <name> [out-dir]
#   e.g. scripts/snapshot.sh sprint23-swift
set -euo pipefail

NAME="${1:?usage: snapshot.sh <name> [out-dir]}"
OUT_DIR="${2:-/home/z/my-project/download}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

cd "$REPO_ROOT"
if [ -n "$(git status --porcelain)" ]; then
    echo "error: working tree is dirty -- commit first (the artifact is HEAD)" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/omnicheck-$NAME.tar.gz"
git archive --format=tar.gz -o "$OUT" HEAD

SHA="$OUT.sha256"
( cd "$OUT_DIR" && sha256sum "$(basename "$OUT")" > "$(basename "$SHA")" )

echo "snapshot: $OUT"
du -h "$OUT" | cut -f1 | sed 's/^/size:     /'
echo "sha256:   $(cut -d' ' -f1 "$SHA")"

# Self-check: the artifact must contain the tracked tree and NONE of the
# untracked payload classes.
if tar tzf "$OUT" | grep -Eq '(^|/)(third_party/tree-sitter-[^/]+|build|curl-7\.88\.1|abseil-20240722\.0|ktor-3\.0\.1|swift-argument-parser-1\.5\.0)/'; then
    echo "error: untracked payload leaked into the snapshot" >&2
    exit 1
fi
for f in scripts/vendor.sh THIRD_PARTY_NOTICES.md third_party/CMakeLists.txt third_party/omnidecide/PROVENANCE.md; do
    tar tzf "$OUT" "$f" > /dev/null || { echo "error: missing $f in snapshot" >&2; exit 1; }
done
echo "self-check: tracked-only, vendor pins + restore path included"
