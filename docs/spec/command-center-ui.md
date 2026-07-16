# Command Center UI — spec e implementação

## Modos do tester-web

| Modo | URL | Público |
|------|-----|---------|
| **Tester** | `#tester` (default) | Engenharia / QA |
| **Análise** | `#analysis` | Lab vertical: captura → codec → mix → gate → Processar → STT/LLM |
| **Command Center** | `#command-center` | Operações / NOC |

### Análise (lab de pipeline)

Fluxo em `#analysis` (`AnalysisLabApp`):

1. **Fonte** — gravar / carregar WAV (`RecorderPanel` + waveform)
2. **Codec (opcional)** — off = `pcm_s16le_16k`; on = tronco G.711 / G.722 / PCM via `CodecConfigPanel`
3. **Mesa de mix** — toggles AGC, RNNoise, Silero turns, focus primary
4. **Gate VQA** — limiares (score, SNR, silence-for-ready, histerese, split quality/presence)
5. **Processar** — duas passagens com o mesmo áudio/codec:
   - **Antes:** mix legado (`LEGACY_MIX`) + headers DSP off
   - **Depois:** mix configurado + headers `X-Audio-AGC`, `X-Audio-Enhancement`, `X-STT-Diarization`, `X-STT-Focus-Primary`
6. **Resultados** — métricas, timeline scores sobreposta, espectrograma (FFT local + turns), painel de interlocutores
7. **STT** — modelo (`auto|parakeet|whisper`) + provider (`cpu|cuda`); diff visual do texto; turns multi-ator quando diarization on e focus_primary off
8. **LLM** — POST /v1/analysis/llm (proxy Ollama). Payload: { mix, before, after, objective } onde objective (lib/domain/analysis-objective.ts) agrega SNR/RMS/peak/crest/clipping/ready e stt_token_f1_after_vs_before (proxy lexical, **não** WER). Resposta em 6 seções: Diagnóstico → Configuração do strip (sem juízo de eficácia) → **Validação objetiva do DSP** (tabela) → STT → Sugestões → Plano. PESQ/STOI/LUFS/WER indisponíveis até existirem no Lab — o agente não deve inventá-los.
   Server-side: valida titulos das 6 secoes + tabela objetiva; ate 3 tentativas; resposta inclui format_ok/attempts/format_errors (HTTP 422 se falhar).

Re-score local do gate: `lib/domain/vqa-rescore.ts`. Headers: `lib/api/process-headers.ts`.

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

## Audio Pipeline (Command Center)

Painel nativo em **Command Center** (`http://localhost:3000/#command-center`):

- **Fleet ou por sessão:** dropdown alimentado por `GET /v1/ops/pipeline/sessions`
- **Estágios inbound/outbound** com barras de bytes, VQA score, jitter, drops
- Atualização: poll 3s + eventos `pipeline_snapshot` no ops WS
- Sem dependência de Grafana/ECharts/Infinity

Grafana (`:3001`) mantém métricas Prometheus `voiceqas_pipeline_*` nos dashboards de ops; o Sankey ECharts foi descontinuado.

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


## Command Center ? rotas (rework produ??o)

| Hash | Se??o |
|------|--------|
| `#command-center` | Monitor (overview) |
| `#command-center/pipeline` | Inbound / Outbound / Transport / DSP |
| `#command-center/asr` | ASR / STT + VAD |
| `#command-center/sessions` | Media + sess?es + hist?rico |
| `#command-center/observability` | RED + APM |
| `#command-center/config` | Auth ops, pipeline default, canais |

- **Ops WS:** auto-connect ao abrir; reconex?o a cada **10s** (`OPS_WS_RECONNECT_MS`).
- **Sess?o ativa ?nica:** dropdown global alimenta pipeline, ASR e hist?rico.
- **NOC fullscreen:** slides (Overview ? Pipeline ? ASR ? Sess?es ? Alertas ? RED ? APM), autoplay 30s, **Fixar slide**, atalhos ?/? e P.
- **Auth ops:** apenas em Configura??o; presets dev s? em `import.meta.env.DEV`.

### Canais (`/v1/config/channels`)

Persist?ncia: `VOICEQAS_CHANNELS_CONFIG_PATH` (default `/data/config/channels.yaml`, volume Docker `./data/config`).

Tipos: `sip_trunk`, `websocket`, `webrtc`, `rest`, `grpc`. Pipeline default global; override por canal. Media sessions aceitam `channel_id`.
