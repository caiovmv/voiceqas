# Command Center UI — spec e implementação

## Modos do tester-web

| Modo | URL | Público |
|------|-----|---------|
| **Tester** | `#tester` (default) | Engenharia / QA |
| **Command Center** | `#command-center` | Operações / NOC |

---

## WebSocket ops (`/v1/ops/stream`)

Handshake:

```json
{
  "filter_session_id": "call-uuid",
  "subscribe_vqa": true,
  "subscribe_stt": true,
  "subscribe_alerts": true,
  "subscribe_pipeline": true,
  "token": "opcional"
}
```

Eventos: `vqa_window`, `stt_partial`, `stt_final`, `alert`, `pipeline_snapshot`

---

## REST ops

| Método | Path | Auth | Descrição |
|--------|------|------|-----------|
| GET | `/v1/ops/metrics/history` | read | Eventos persistidos (JSONL) |
| GET | `/v1/ops/alerts` | read | Alertas históricos |
| GET | `/v1/ops/pipeline/snapshot` | read | Sankey JSON (fleet ou `?session_id=`) |
| GET | `/v1/ops/pipeline/sessions` | read | Sessões ativas (`active_sessions`) e finalizadas (`finished_sessions`) |
| GET/POST/DELETE | `/v1/media/sessions` | read/write | Media relay |

Tokens (`ops:` no YAML ou env):

- `admin_token` / `VOICEQAS_OPS_TOKEN` — acesso total
- `read_token` / `VOICEQAS_OPS_READ_TOKEN` — GET + ops WS
- `write_token` / `VOICEQAS_OPS_WRITE_TOKEN` — mutações media

---

## Pipeline Sankey (Grafana)

Dashboard **VoiceQAS Pipeline Sankey** em Grafana (`http://localhost:3001`):

- **Fleet (default):** métricas agregadas via Prometheus `voiceqas_pipeline_*`
- **Debug por sessão:** variável `session_id` → `GET /v1/ops/pipeline/snapshot?session_id=...`
- Painel ECharts com inbound (SIP → VQA → STT → AI Agent) e outbound (AI Agent → SIP)
- **11 painéis por estágio** (escala independente): `sip_in`, `rtp_ingress`, `decode_vqa`, `agc_vqa`, `enhancement`, `vqa`, `stt_gate`, `decode_stt`, `agc_stt`, `resample_16k`, `stt_buffer`

---

## Persistência e webhooks

- Eventos em JSONL: `ops.storage_path` (default `/tmp/voiceqas-ops.jsonl`)
- Webhook HTTP POST em `ops.webhook_url` para alertas (`low_score`, `stt_not_ready`)
- Thresholds: `alert_score_threshold`, `stt_not_ready_alert_ms`

---

## STT parcial incremental

Sliding window no `SttSessionManager` (`partial_stt_*` em `ops:`):

- A cada ~3s com buffer ≥3s, transcreve últimos 5s
- Publica `stt_partial` no ops WS
- gRPC `TranscribeStream` emite `STT_PARTIAL`
- Silero VAD filtra silêncio antes do ASR

---

## gRPC MediaRelayService (`proto/media.proto`)

- `OpenSession`, `CloseSession`, `ListSessions`, `SendAgentAudio`

---

## Command Center UI

Health, auth, alertas, media CRUD, agent-audio, session grid, CSV export, NOC fullscreen, histórico persistido, auto-reconnect ops WS, **seletor Silero VAD** (`POST /v1/stt/vad/model`).

### Estrutura `tester-web/src/lib/`

| Módulo | Responsabilidade |
|--------|------------------|
| `api/client.ts` | `fetchJson`, `apiBase`, `resolveWsBase` |
| `api/ops.ts` | histórico, alertas, ops WebSocket |
| `api/media.ts` | sessões media relay, agent-audio |
| `api/stt.ts` | ready, VAD model, transcribe REST/WS |
| `api/vqa.ts` | analyze batch REST/WS |
| `api/grpc.ts` | playground gRPC batch/stream |
| `api.ts` | barrel re-export (imports legados) |
| `domain/` | merge de sessões, regras de alerta (testável) |
| `ops.ts`, `auth.ts`, `types.ts` | helpers ops e tipos compartilhados |

Vitest cobre `domain/*`, codecs (`g711`, `g722`) e utilitários de áudio.

## Métricas Prometheus (`GET /metrics`)

- `voiceqas_up`, `voiceqas_stt_ready`, `voiceqas_ops_ws_listeners`
- `voiceqas_vqa_composite_score`, `voiceqas_vqa_snr_db`, `voiceqas_vqa_silence_ratio`
- `voiceqas_vqa_active_sessions`, `voiceqas_vqa_stt_ready_sessions`
- `voiceqas_pipeline_active_sessions`
- `voiceqas_pipeline_finished_sessions` — recentes em retenção (até 200)
- `voiceqas_pipeline_finished_sessions_total` — total desde o boot
- `voiceqas_pipeline_bytes_total{direction,stage}`, `voiceqas_pipeline_latency_ms`, `voiceqas_pipeline_jitter_ms`, `voiceqas_pipeline_composite_score`, `voiceqas_pipeline_dropped_bytes_total`

---

## Pendências futuras

- Sankey interativo no Command Center React (reutiliza JSON do snapshot)
- TSDB dedicado (Prometheus/ClickHouse) em vez de JSONL
- OAuth / SSO enterprise
- Online recognizer sherpa (modelo streaming dedicado)
