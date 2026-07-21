#!/usr/bin/env bash
# Build Release "badass": LTO + -march=x86-64-v3 nos targets voiceqas (nao sherpa-onnx).
# Rodar manualmente antes de testes finais — link LTO e mais lento que build normal.
#
# Uso:
#   ./scripts/build-badass.sh              # Docker (padrao)
#   ./scripts/build-badass.sh local        # build nativo via scripts/build.sh
#   ./scripts/build-badass.sh --redeploy   # build + recreate voiceqas/beyla/cc
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

MODE="docker"
REDEPLOY=0
for arg in "$@"; do
  case "$arg" in
    local) MODE="local" ;;
    docker) MODE="docker" ;;
    --redeploy) REDEPLOY=1 ;;
  esac
done

if [[ "$MODE" == "local" ]]; then
  echo ">> build-badass (local): LTO + x86-64-v3"
  export VOICEQAS_PERF_BADASS=ON
  exec "$ROOT/scripts/build.sh" -DVOICEQAS_PERF_BADASS=ON
fi

echo ">> build-badass (docker): LTO + x86-64-v3 — link pode levar varios minutos"
docker compose build voiceqas --build-arg VOICEQAS_PERF_BADASS=ON
docker tag voiceqas:local voiceqas:badass-local 2>/dev/null || true
echo ">> image: voiceqas:local (tag extra: voiceqas:badass-local)"

if [[ "$REDEPLOY" == "1" ]]; then
  echo ">> redeploy voiceqas + beyla + command-center"
  docker compose up -d --force-recreate voiceqas command-center beyla
  sleep 5
  echo "voiceqas health=$(docker inspect voiceqas --format '{{.State.Health.Status}}')"
fi

echo "OK — flags: Release -O3, LTO (IPO), -march=x86-64-v3 (sem -ffast-math)"