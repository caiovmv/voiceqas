#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Build directory not found: $BUILD_DIR" >&2
  exit 1
fi

cd "$BUILD_DIR"
echo ">> Running voiceqas_tests (core)..."
ctest --output-on-failure -L "unit;core" "$@"
echo ">> Running voiceqas_server_tests (server)..."
ctest --output-on-failure -L "unit;server" "$@"
