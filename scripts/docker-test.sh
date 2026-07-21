#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if ! docker image inspect voiceqas-build-base:local >/dev/null 2>&1; then
  echo ">> base image missing — building with VOICEQAS_BUILD_TESTS=ON..."
  VOICEQAS_BUILD_TESTS=ON "$ROOT/scripts/build-base.sh"
fi

echo ">> Docker builder with unit tests (VOICEQAS_BUILD_TESTS=ON)..."
docker build \
  --target builder \
  --build-arg VOICEQAS_BUILD_TESTS=ON \
  --build-arg VOICEQAS_STT_CUDA=0 \
  -f "$ROOT/Dockerfile" \
  "$ROOT"