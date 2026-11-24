#!/usr/bin/env bash
# M5 GA packaging (plan P4): a release tarball with the layout users
# actually install -- bin/oc, packs/, LICENSE, THIRD_PARTY_NOTICES.md,
# README.md -- plus a sha256 manifest. The tarball is smoke-checked by
# extracting to a temp dir and running the packaged binary end-to-end;
# its example-report bytes must equal the repo build's output (the
# determinism contract applies to the artifact, not just the tree).
#
# Usage: scripts/package.sh [output-dir]   (default: dist/)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-$REPO_ROOT/dist}"
BUILD_DIR="$REPO_ROOT/build/default"

VERSION="$(sed -n 's/#define OC_VERSION_STRING "\(.*\)"/\1/p' "$REPO_ROOT/include/oc/common.h")"
ARCH="$(uname -m)"
PKG="omnicheck-${VERSION}-linux-${ARCH}"

if [ ! -x "$BUILD_DIR/oc" ]; then
    echo "error: $BUILD_DIR/oc not built (cmake --preset default && cmake --build --preset default)" >&2
    exit 1
fi

STAGE="$(mktemp -d)/$PKG"
mkdir -p "$STAGE/bin" "$STAGE/packs"
cp "$BUILD_DIR/oc" "$STAGE/bin/oc"
cp -r "$REPO_ROOT/packs/." "$STAGE/packs/"
cp "$REPO_ROOT/LICENSE" "$REPO_ROOT/THIRD_PARTY_NOTICES.md" "$REPO_ROOT/README.md" "$STAGE/"

mkdir -p "$OUT_DIR"
TARBALL="$OUT_DIR/$PKG.tar.gz"
tar -C "$(dirname "$STAGE")" -czf "$TARBALL" "$PKG"

# sha256 manifest (the tarball plus the binary inside it, for pinned installs)
SHAFILE="$OUT_DIR/$PKG.tar.gz.sha256"
( cd "$OUT_DIR" && sha256sum "$(basename "$TARBALL")" > "$(basename "$SHAFILE")" )
sha256sum "$STAGE/bin/oc" >> "$SHAFILE"

# ---------------------------------------------------------- smoke test -
# `oc check` exits 1 on this corpus by design (findings >= threshold);
# the smoke asserts the REPORT BYTES, not the exit code.
SMOKE_DIR="$(mktemp -d)"
tar -C "$SMOKE_DIR" -xzf "$TARBALL"
"$SMOKE_DIR/$PKG/bin/oc" --version
"$SMOKE_DIR/$PKG/bin/oc" check "$REPO_ROOT/examples" --packs-dir "$SMOKE_DIR/$PKG/packs" \
    > "$SMOKE_DIR/packaged_report.json" || true
"$BUILD_DIR/oc" check "$REPO_ROOT/examples" --packs-dir "$REPO_ROOT/packs" \
    > "$SMOKE_DIR/repo_report.json" || true
cmp "$SMOKE_DIR/packaged_report.json" "$SMOKE_DIR/repo_report.json" \
    || { echo "error: packaged report differs from repo build report" >&2; exit 1; }
rm -rf "$SMOKE_DIR"

echo "packaged: $TARBALL"
echo "checksum: $SHAFILE"
cat "$SHAFILE"
