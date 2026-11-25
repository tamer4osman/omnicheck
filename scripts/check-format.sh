#!/bin/sh
# Fails if any first-party source is not clang-format clean.
# Fixture corpora are DATA, not first-party code: since SPRINT-20 the C
# fixtures are themselves .c files whose line numbers are hand-counted
# pins (the OC001 63-line fixture, the comp-35 decision inventory) --
# tests/fixtures* is excluded and must never be reformatted.
set -eu
cd "$(dirname "$0")/.."

if ! command -v clang-format > /dev/null 2>&1; then
    echo "check-format: clang-format not found, skipping"
    exit 0
fi

files=$(find include src tests -name '*.c' -o -name '*.h' | grep -v '^tests/fixtures' | sort)
for f in $files; do
    clang-format --dry-run --Werror "$f"
done
echo "check-format: OK"
