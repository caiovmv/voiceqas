# Audio pipeline — voiceqas

## Objetivo

Processar áudio de tronco SIP com pipeline **assimétrico**:

- **Interlocutor:** decode → PCM16 normalizado → VQA/STT (permanece PCM internamente)
- **Agente de voz:** PCM TTS → encode no codec negociado → RTP/UDP ao trunk

O problema alvo é qualidade da **voz do interlocutor** (nível baixo, ruído), não defeitos de rede SIP.

## Diagrama

```mermaid
flowchart TB
    subgraph inbound [Entrada interlocutor]
        RTP_IN[RTP UDP ou WS/gRPC] --> DEC[audio::decode_to_pcm]
        DEC --> AGC[AudioNormalizer AGC]
        AGC --> VQA[VoiceAnalyzer]
        AGC --> STT[STT resample 16k]
    end

    subgraph egress [Saída agente]
        TTS[PCM16 agente] --> RS[resample_pcm16]
        RS --> LIM[peak limiter]
        LIM --> ENC[audio::encode_from_pcm]
        ENC --> UDP_OUT[RTP UDP]
    end

    subgraph session [MediaSession]
        LOCK[format lock + remote_addr]
    end

    RTP_IN --> LOCK
    ENC --> LOCK
```

## Formatos suportados

| Formato | PCM rate | RTP PT | Encode outbound |
|---------|----------|--------|-----------------|
| `rtp_pcmu` | 8 kHz | 0 | Sim |
| `rtp_pcma` | 8 kHz | 8 | Sim |
| `rtp_g722` | 16 kHz | 9 | Sim |
| `rtp_g729` | 8 kHz | 18 | Sim |
| `pcm_s16le_8k` | 8 kHz | — | Raw PCM |
| `pcm_s16le_16k` | 16 kHz | — | Raw PCM |

Identificação de entrada:

1. **Declarativa** — `POST /v1/media/sessions` (default `rtp_g722`), handshake WS/gRPC, header `X-Audio-Format`
2. **Autodetect RTP** — PT do header (`0→PCMU`, `8→PCMA`, `9→G722`, `18→G729`); preferido **G.722**
3. **UDP sem sessão prévia** — PT inferido do primeiro pacote; alerta ops se ≠ `rtp_g722`

**Format lock:** após a primeira frame válida, o codec da sessão não pode mudar. Nova negociação exige novo `session_id`.

## Processamento inbound

### Decode

Módulo unificado: `audio::decode_to_pcm()` em [`src/audio/decoder.cpp`](../../src/audio/decoder.cpp).

- RTP: `RtpDepacketizer` com estado (loss, jitter, G.722/G.729 stateful)
- PCM: cópia direta int16 LE

### Normalização (AGC)

Config em `audio:` no YAML:

| Parâmetro | Default | Efeito |
|-----------|---------|--------|
| `normalize_enabled` | `true` | Liga/desliga AGC |
| `agc_target_rms_dbfs` | `-20` | Nível alvo |
| `agc_max_gain_db` | `24` | Ganho máximo |
| `agc_attack_ms` | `5` | Subida de ganho |
| `agc_release_ms` | `100` | Descida de ganho |
| `limiter_ceiling_dbfs` | `-3` | Teto anti-clipping |

- Estado **por sessão** (`AgcState` em `SessionManager` e `MediaSessionManager`)
- Latência típica: **+15–30 ms**
- **Não** aplicado na saída do agente (apenas limitador de pico)

### Fork VQA / STT

- **VQA:** `VoiceAnalyzer` na taxa nativa do codec (8 ou 16 kHz); VAD energético para métricas
- **STT:** `prepare_audio_for_stt` → resample linear 16 kHz → sherpa-onnx

### Ingress unificado (media relay)

`RtpIngressProcessor` ([`include/voiceqas/media/rtp_ingress.hpp`](../../include/voiceqas/media/rtp_ingress.hpp)) mantém estado de `RtpDepacketizer` por sessão e chama `audio::decode_to_pcm` uma vez por pacote. O PCM normalizado é distribuído em paralelo:

| Destino | API | Uso |
|---------|-----|-----|
| VQA | `VqaSessionManager::push_pcm` | janelas de qualidade + telemetria pipeline |
| STT | `SttSessionManager::append_pcm` | buffer + parciais incrementais |

Timer periódico no `media_relay` chama `emit_partials_for_all_sessions()` (substitui decode duplicado no caminho STT).

### Targets CMake

| Target | Conteúdo |
|--------|----------|
| `voiceqas_core` | Domínio: audio, RTP, VQA, STT, media — depende de `ports/` (interfaces), não de `ops/` |
| `voiceqas_ops` | Observability: metrics hub, pipeline tracker, Prometheus, webhooks, adapters |
| `voiceqas_server` | REST, WS, gRPC, media relay — compõe core + ops |

## Processamento outbound (agente)

1. PCM16 do TTS (tipicamente 16 kHz)
2. `resample_pcm16` → taxa da sessão (8 kHz G.711/G.729, 16 kHz G.722)
3. `apply_peak_limiter_inplace` (~−3 dBFS)
4. `encode_from_pcm` → G.711 / G.722 / G.729
5. `RtpPacketizer` → pacotes RTP 20 ms
6. `UDP sendto` → `remote_host:remote_port` da sessão

O interlocutor **nunca** passa por re-encode G.711.

## Media relay UDP

| Item | Valor |
|------|-------|
| Porta default | `10000/udp` |
| Env | `VOICEQAS_MEDIA_RTP_ADDR` |
| Config | `media.enabled`, `media.rtp_addr` |

Fluxo:

- **Inbound:** pacote RTP → `RtpIngressProcessor` (decode único) → fan-out PCM para `VqaSessionManager::push_pcm` e `SttSessionManager::append_pcm`
- **Outbound:** `POST .../agent-audio` → encode → UDP

Integração SBC: voiceqas é **media plane only** (sem SIP INVITE/SDP). O SBC roteia RTP para `host:10000` e recebe RTP do agente no `remote_host:port` registrado.

## APIs REST

### Registrar sessão

`POST /v1/media/sessions`

```json
{
  "session_id": "call-uuid",
  "format": "rtp_pcmu",
  "remote_host": "10.0.0.5",
  "remote_port": 12000,
  "sample_rate": 8000
}
```

### Encerrar sessão

`DELETE /v1/media/sessions/{session_id}`

### Áudio do agente

`POST /v1/media/sessions/{session_id}/agent-audio`

- Body: `application/octet-stream` (PCM16) ou JSON `{ "pcm_bytes": [...] }`
- Header: `X-Sample-Rate` (default `16000`)

Resposta:

```json
{
  "status": "ok",
  "rtp_packets": 42,
  "bytes_sent": 5040
}
```

### Ferramentas codec

- `POST /v1/tools/pack-rtp` — PCM → RTP (PCMU, PCMA, G.722, G.729)
- `POST /v1/tools/decode-rtp` — RTP → PCM

## Variáveis de ambiente

| Variável | Default |
|----------|---------|
| `VOICEQAS_MEDIA_RTP_ADDR` | `0.0.0.0:10000` |
| `VOICEQAS_AUDIO_NORMALIZE` | `true` |

## Limitações

| Item | Status |
|------|--------|
| Stack SIP (INVITE/SDP) | Fora do escopo |
| AEC (echo cancellation) | SBC upstream |
| Silero VAD no STT C++ | **Ativo** (k2fsa default; v4/v5/int8 para teste) |
| Enhancement neural (RNNoise) | Fase futura |
| Re-encode interlocutor | Proibido por design |

## Tuning

| Sintoma | Ajuste |
|---------|--------|
| Voz baixa, `stt_ready` falso | `agc_target_rms_dbfs` mais alto (−18), `agc_max_gain_db` |
| Ruído amplificado com AGC | Desligar AGC ou adicionar NS (futuro) |
| Agente distorce no G.711 | Reduzir nível TTS; `limiter_ceiling_dbfs` |
| WER alto com muito silêncio | Silero VAD (quando disponível) |

## Referências

- VQA e métricas: [`voice-quality-assessment.md`](voice-quality-assessment.md)
- Plano arquitetural: [`plan.md`](plan.md)
- Dashboard Sankey (Grafana): `deploy/observability/grafana/dashboards/voiceqas-pipeline-sankey.json`

## Telemetria por estágio (Sankey)

Publicada a cada janela VQA (~500 ms) como evento `pipeline_snapshot` e exposta em `GET /v1/ops/pipeline/snapshot`.

| Estágio | Métricas |
|---------|----------|
| `rtp_ingress` | bytes, jitter_ms, packet_loss_pct |
| `decode_vqa` / `agc_vqa` | latency_ms_p50/p95, bytes |
| `enhancement` | stub pass-through (0 ms; RNNoise futuro) |
| `vqa` | composite_score, snr_db, rms_dbfs, stt_ready |
| `stt_gate` / `stt_dropped` | dropped_bytes quando `require_stt_ready` |
| `decode_stt` → `asr` | latência por sub-etapa, buffer_ms, processing_ms |
| `ai_agent` | nó virtual (STT in / PCM out) |
| `agent_pcm_in` → `sip_out` | latência resample/limit/encode, rtp_packets, bytes_sent |

```mermaid
flowchart LR
    SIP_IN[SIP_in] --> RTP[RTP_ingress]
    RTP --> DEC_VQA[Decode_VQA]
    RTP --> DEC_STT[Decode_STT]
    RTP --> DROP[STT_dropped]
    DEC_VQA --> AGC_VQA --> ENH[Enhancement_stub] --> VQA --> GATE[STT_gate]
    GATE --> DEC_STT
    DEC_STT --> AGC_STT --> RS16 --> BUF --> VAD --> ASR --> AI[AI_Agent]
    AI --> PCM[Agent_PCM] --> RS_OUT --> LIM --> ENC --> PKT --> EGR[RTP_egress] --> SIP_OUT[SIP_out]
```

