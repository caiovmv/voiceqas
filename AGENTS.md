# voiceqas — contexto para agentes



## O que é



Serviço C++ de **VQA** e **STT** para tronco SIP. Branch media pipeline: `feature/audio-pipeline-media-relay`.



## Portas (docker-compose local)



| Porta | Uso |

|-------|-----|

| 9080 | REST, Swagger, STT, media, ops (`voiceqas` mapeado; interno 8080) |

| 9081 | WS VQA, STT, **ops stream** |

| 9051 | gRPC VoiceQuality + STT + MediaRelay |

| 10000/UDP | Media relay RTP |

| 3000 | Command Center + Tester (`command-center`) |

| 3001 | Grafana |

| 9090 | Prometheus |

| 3100 | Loki |

| 3200 | Tempo |

| 12345 | Grafana Alloy UI |
| — | Beyla eBPF → Alloy OTLP (sidecar `voiceqas-beyla`) |



## APIs principais



- Media REST: `GET/POST/DELETE /v1/media/sessions`, `agent-audio`

- Ops REST: `GET /v1/ops/metrics/history`, `GET /v1/ops/alerts`, `GET /v1/ops/pipeline/snapshot`, `GET /v1/ops/pipeline/sessions`

- Ops WS: `/v1/ops/stream` — VQA, STT, alertas, `pipeline_snapshot`

- Metrics: `GET /metrics` (Prometheus)

- STT VAD: `POST /v1/stt/vad/model` (write token)

- gRPC: `MediaRelayService` (`proto/media.proto`)



## Ops



- Persistência JSONL (`ops.storage_path`)

- Webhooks (`ops.webhook_url`)

- Auth RBAC leve: admin / read / write tokens (`X-Ops-Token`)

- STT parcial incremental + Silero VAD (k2fsa default; selector `auto|k2fsa|v4|v5|k2fsa-int8`)

- Gate opcional `stt.require_stt_ready` — bloqueia STT streaming quando VQA diz não-ready



## Observabilidade



Stack em `deploy/observability/`: Prometheus, Grafana, Loki, Tempo, Alloy.

Dados persistentes no host em `data/observability/` (métricas, logs, traces, Grafana DB, ops JSONL). Sobrevive a `docker compose down`; use `docker compose down -v` apenas para o volume `stt-models`.

- **Audio Pipeline** no Command Center (`:3000/#command-center`) — estágios ao vivo via `/v1/ops/pipeline/snapshot`
- **APM / Traces** — Grafana (`:3001/d/voiceqas-apm`) ou Command Center (`:3000/#command-center`); ambos usam Tempo
- **RED / Latência** no Command Center — P50/P95/P98, RPS, 4xx/5xx, RPS por endpoint via Prometheus/Beyla (`/prometheus` proxy)
- Stack observabilidade (Jul/2026): Prometheus **v3.13.0**, Loki **3.7.3**, Tempo **3.0.2**, Alloy **v1.17.1**, Grafana **12.4.5** (12.x por compat. Business Charts 7.x)
- Grafana VoiceQAS API datasource: **Infinity** (`yesoreyeram-infinity-datasource`); JSON API é frontend-only e falha no Grafana 12
- Grafana: entrypoint copia plugins para `/var/lib/grafana/plugins` e habilita Infinity + ECharts no boot



## Arquitetura (refactor 90%)

- **Ports/adapters:** `include/voiceqas/ports/` — `IMetricsPublisher`, `IPipelineTelemetry`; adapters em `src/ops/adapters/`
- **VQA sessions:** `VqaSessionManager` (antes `SessionManager`) em `analyzer.hpp`
- **RTP ingress unificado:** `media::RtpIngressProcessor` no media relay (decode único → fan-out PCM)
- **Codec registry:** `audio::CodecRegistry` / `default_codec_registry()`
- **REST modular:** `src/server/routes/` — `static_routes`, `vqa_routes`, `media_routes`, `ops_routes`, `stt_routes` + `RouteContext`
- **gRPC mappers:** `include/voiceqas/server/grpc_mappers.hpp` — proto ↔ `AudioFormat`, quality/STT payloads
- **Config:** `load_app_config()` — injetar structs; `global_*_config()` deprecated
- **Clean Arch:** `voiceqas_core` (domínio) + `voiceqas_ops` (infra observability) — core usa apenas `ports/`; telemetry via `IPipelineTelemetry`
- **Testes:** `voiceqas_tests` (core) + `voiceqas_server_tests` (auth, event store, grpc mappers); labels GTest `unit;core` / `unit;server`
- **Coverage C++:** `VOICEQAS_COVERAGE=1 ./scripts/coverage.sh` (gcovr, floor configurável)
- **Coverage frontend:** `cd tester-web && npm run test:coverage` (domain + auth + codecs testáveis)
- **Docker tests:** `VOICEQAS_BUILD_TESTS=ON ./scripts/docker-test.sh` (builder stage + ctest)
- **Frontend:** `tester-web/src/lib/api/` (ops, media, stt, vqa, grpc), `lib/domain/`, Vitest em `npm test`


## Pendente

- Enhancement neural (RNNoise)

- Stack SIP signaling

- TSDB enterprise / OAuth SSO

- Recognizer sherpa streaming dedicado (online)



## Build



```bash

./scripts/download-stt-models.sh   # volume Docker ou path local

docker compose up --build

```



Docs: `docs/spec/command-center-ui.md`, `docs/spec/audio-pipeline.md`

