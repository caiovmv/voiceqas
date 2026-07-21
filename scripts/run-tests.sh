#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Build directory not found: $BUILD_DIR" >&2
  exit 1
fi

cd "$BUILD_DIR"
echo ">> Running all unit tests..."
ctest --output-on-failure --tests-regex 'voiceqas_(server_)?tests' "$@"
