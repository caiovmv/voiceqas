# voiceqas — Especificação de avaliação de qualidade de voz

## Objetivo

Avaliar rapidamente a qualidade de áudio recebido de tronco SIP para decidir se um trecho é **STT-ready** (apto para speech-to-text).

Desde o pipeline de media relay, todo áudio inbound passa por **decode → PCM16 → AGC opcional** antes das métricas VQA e do STT. Ver [`audio-pipeline.md`](audio-pipeline.md).

## Métricas por janela (500 ms)

| Métrica | Descrição |
|---------|-----------|
| `rms_dbfs` / `peak_dbfs` | Nível de volume |
| `clipping_ratio` | Fração de amostras saturadas |
| `snr_estimate_db` | Relação energia fala/ruído (VAD energético) |
| `silence_ratio` | Fração de frames sem fala |
| `spectral_flatness` | Ruído vs fala tonal |
| `packet_loss_pct` / `jitter_ms` | Qualidade RTP |
| `composite_score` | Score 0–100 |
| `stt_ready` | Gate binário com histerese |

### VQA vs segmentação STT

| Camada | Tecnologia | Uso |
|--------|------------|-----|
| VQA streaming | VAD **energético** (`rms_dbfs >= -40`) | `stt_ready`, métricas em tempo real |
| STT | Silero VAD (planejado, C++) | Cortar silêncio antes do ASR |

O AGC inbound melhora `rms_dbfs` e `snr_estimate_db` quando o interlocutor fala baixo, sem alterar o codec RTP.

## Regra STT-ready (defaults)

- `composite_score >= 65`
- `silence_ratio <= 0.60`
- `clipping_ratio <= 0.02`
- `snr_estimate_db >= 12`
- Histerese: 2 janelas OK para ligar, 1 janela ruim para desligar

Penalidade explícita: `rms_dbfs < -50` reduz o score (ver `stt_gate.cpp`).

## Formatos suportados

- `pcm_s16le_8k`, `pcm_s16le_16k`
- `rtp_pcmu` (PT 0), `rtp_pcma` (PT 8) — G.711
- `rtp_g722` (PT 9, clock 8 kHz, áudio 16 kHz wideband)
- `rtp_g729` (PT 18, 8 kHz, frames 10/20 ms)

Decode unificado: `audio::decode_to_pcm`. G.722/G.729 mantêm estado de codec por sessão RTP.

## Processamento de áudio inbound

```
payload (RTP ou PCM) → decode_to_pcm → AgcState::process_inplace → VoiceAnalyzer / STT
```

Config (`config/voiceqas.example.yaml`):

```yaml
audio:
  normalize_enabled: true
  agc_target_rms_dbfs: -20
  agc_max_gain_db: 24
  limiter_ceiling_dbfs: -3
```

## Media relay (UDP)

| Porta | Uso |
|-------|-----|
| `10000/udp` | RTP inbound interlocutor → VQA + STT |

APIs: `POST /v1/media/sessions`, `POST /v1/media/sessions/{id}/agent-audio`, `DELETE /v1/media/sessions/{id}`.

O áudio do **agente** é codificado de volta ao formato da sessão e enviado via UDP. O interlocutor não é re-encodado.

## Ferramentas (tester-web / REST)

- `POST /v1/tools/pack-rtp` — empacota PCM em pacotes RTP (G.711, G.722, G.729)
- `POST /v1/tools/decode-rtp` — decodifica pacotes RTP para PCM

## APIs

### REST (porta 8080)

- `GET /health`, `GET /ready`
- `POST /v1/analyze/batch` — WAV ou PCM raw
- `POST /v1/analyze/segment` — JSON com `pcm_bytes`
- `POST /v1/media/sessions` — registrar sessão media relay
- `POST /v1/media/sessions/{id}/agent-audio` — egress agente
- `DELETE /v1/media/sessions/{id}`
- `GET /docs/swagger`, `GET /docs/redoc`
- `GET /ui/` — console web

### WebSocket (porta 8081)

1. Conectar em `ws://host:8081/v1/stream`
2. Enviar handshake JSON: `{session_id, format, sample_rate}`
3. Enviar frames binários PCM/RTP
4. Receber `QualityReport` JSON por janela

### gRPC (porta 50051)

Serviços:

- `VoiceQualityService` — análise de qualidade (`Ready`, `AnalyzeStream`, `AnalyzeBatch`)
- `SpeechToTextService` — STT (`Ready`, `Transcribe`, `TranscribeStream`)

### STT — sherpa-onnx embutido (monolito C++)

Motor in-process via **sherpa-onnx** (sem sidecar Python em produção):

- **Primário:** `sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8`
- **Fallback:** `sherpa-onnx-whisper-turbo` com `language=pt`
- **Call center pt-BR:** `whisper_callcenter_dir` (opcional)

Parâmetro `model`: `parakeet` | `whisper` | `auto`.

Áudio STT: decode → channel strip (NR→HPF→EQ→DeEss→Comp→Lim→AGC) → resample 16 kHz → recognizer.

#### REST STT

- `GET /v1/stt/ready`
- `POST /v1/stt/transcribe`
- `POST /v1/stt/transcribe/segment`
- `POST /v1/playground/grpc/stt-transcribe`

#### WebSocket STT (porta 8081)

1. `ws://host:8081/v1/stt/stream`
2. Handshake: `{session_id, format, sample_rate, model?, language?}`
3. Frames binários + `{"type":"flush"}`

### Playground HTTP

- `POST /v1/playground/grpc/ready`
- `POST /v1/playground/grpc/analyze-batch`
- `POST /v1/playground/grpc/analyze-stream` (SSE)

## Critérios de aceitação observáveis

1. Silêncio → `stt_ready=false`, score baixo
2. Fala limpa (ou com AGC em voz baixa) → score alto, possível `stt_ready=true`
3. Clipping > 5% → `stt_ready=false`
4. Mesmo PCM produz mesmo report em REST, WS e playground gRPC
5. Round-trip G.711 encode/decode com degradação controlada (testes unitários)
6. Agente PCM → sessão PCMU → pacotes RTP PT=0 com timestamp monotônico

## Docker

- **voiceqas** — REST + gRPC + WebSocket + STT + **UDP 10000**
- **tester-web** — React 19 + Vite

Stack: C++26, vcpkg, GCC 15+. Ver `README.md`.
