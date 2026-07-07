#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
MIN_LINE="${VOICEQAS_COVERAGE_MIN_LINE:-40}"

export VOICEQAS_COVERAGE=1
export VOICEQAS_BUILD_TESTS=1
export CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}"

if ! command -v gcovr >/dev/null 2>&1; then
  echo "gcovr is required (apt install gcovr)" >&2
  exit 1
fi

"$ROOT/scripts/build.sh"

cd "$BUILD_DIR"
gcovr -r "$ROOT" \
  --filter "$ROOT/src/" \
  --filter "$ROOT/include/" \
  --exclude "$ROOT/build/" \
  --exclude '.*/_deps/.*' \
  --exclude '.*/generated/.*' \
  --exclude '.*/third_party/.*' \
  --exclude '.*/tests/.*' \
  --print-summary \
  --xml "$BUILD_DIR/coverage.xml" \
  --html "$BUILD_DIR/coverage.html" \
  --html-details \
  --fail-under-line "$MIN_LINE"

echo "Line coverage >= ${MIN_LINE}%"
