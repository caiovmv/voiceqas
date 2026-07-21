#!/usr/bin/env bash
# Build voiceqas-build-base (toolchain + vcpkg deps). Run rarely.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-voiceqas-build-base:local}"
BUILD_TESTS="${VOICEQAS_BUILD_TESTS:-OFF}"
echo "Building $TAG (VOICEQAS_BUILD_TESTS=$BUILD_TESTS) ..."
docker build -f "$ROOT/Dockerfile.base" -t "$TAG" \
  --build-arg "VOICEQAS_BUILD_TESTS=$BUILD_TESTS" \
  "$ROOT"
echo "OK — base ready. App rebuilds: docker compose build voiceqas"