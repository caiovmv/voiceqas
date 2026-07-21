#!/usr/bin/env bash
# Abre PR feature/audio-pipeline-media-relay → main (requer: gh auth login)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v gh >/dev/null 2>&1; then
  echo "Instale GitHub CLI: https://cli.github.com/" >&2
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "Execute: gh auth login" >&2
  exit 1
fi

gh pr create --base main --head feature/audio-pipeline-media-relay \
  --title "Media pipeline, Command Center, and architecture refactor" \
  --body "$(cat <<'EOF'
## Summary

- **Audio/media pipeline:** unified RTP ingress (`RtpIngressProcessor`), media relay UDP, G.711/G.722/G.729 codecs, agent outbound encode, and `MediaRelayService` gRPC.
- **Architecture refactor:** ports/adapters DI, `voiceqas_core` + `voiceqas_ops` split, codec registry, modular REST routes, consolidated gRPC mappers, `VqaSessionManager` rename.
- **Ops & observability:** Command Center UI, ops WebSocket/REST, pipeline Sankey telemetry, Prometheus/Loki/Grafana/Tempo stack, JSONL persistence, RBAC tokens.
- **Quality:** 20+ C++ unit tests, Vitest (23 tests, ~95% coverage on domain layer), GitHub Actions CI + gcov coverage job.

## Test plan

- [ ] `VOICEQAS_BUILD_TESTS=1 ./scripts/build.sh`
- [ ] `cd tester-web && npm run test:coverage && npm run build`
- [ ] `docker compose up --build`
- [ ] Command Center (`:3000/#command-center`)
- [ ] Grafana Sankey (`:3001`)
EOF
)"
