#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo ">> Docker builder with unit tests (VOICEQAS_BUILD_TESTS=ON)..."
docker build \
  --target builder \
  --build-arg VOICEQAS_BUILD_TESTS=ON \
  --build-arg VOICEQAS_STT_CUDA=0 \
  -f "$ROOT/Dockerfile" \
  "$ROOT"
