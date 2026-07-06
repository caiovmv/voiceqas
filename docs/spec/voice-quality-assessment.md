# voiceqas — Especificação de avaliação de qualidade de voz

## Objetivo

Avaliar rapidamente a qualidade de áudio recebido de tronco SIP para decidir se um trecho é **STT-ready** (apto para speech-to-text).

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

## Regra STT-ready (defaults)

- `composite_score >= 65`
- `silence_ratio <= 0.60`
- `clipping_ratio <= 0.02`
- `snr_estimate_db >= 12`
- Histerese: 2 janelas OK para ligar, 1 janela ruim para desligar

## Formatos suportados

- `pcm_s16le_8k`, `pcm_s16le_16k`
- `rtp_pcmu` (PT 0), `rtp_pcma` (PT 8) — G.711
- `rtp_g722` (PT 9, clock 8 kHz, áudio 16 kHz wideband)
- `rtp_g729` (PT 18, 8 kHz, frames 10/20 ms)

### Ferramentas (tester-web)

- `POST /v1/tools/pack-rtp` — empacota PCM em pacotes RTP (G.722, G.729)
- `POST /v1/tools/decode-rtp` — decodifica pacotes RTP para PCM

## APIs

### REST (porta 8080)

- `GET /health`, `GET /ready`
- `POST /v1/analyze/batch` — WAV ou PCM raw
- `POST /v1/analyze/segment` — JSON com `pcm_bytes`
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
- `SpeechToTextService` — STT Parakeet TAGARELA (`Ready`, `Transcribe`, `TranscribeStream`)

### STT — sherpa-onnx embutido (monolito C++)

Motor in-process via **sherpa-onnx** (sem sidecar Python):

- **Primário:** `sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8`
- **Fallback / alternativo:** `sherpa-onnx-whisper-turbo` com `language=pt`
- **Call center pt-BR:** diretório opcional `whisper_callcenter_dir` (ONNX exportado de LoRA merged)

Parâmetro `model` em todos os endpoints STT: `parakeet` | `whisper` | `auto` (parakeet → whisper se vazio/falha).

Modelos baixados com `scripts/download-stt-models.sh` para volume `/models/stt`.

#### REST

- `GET /v1/stt/ready` — readiness + lista de modelos carregados
- `POST /v1/stt/transcribe` — batch (WAV / PCM / RTP); header `X-STT-Model` ou `?model=`
- `POST /v1/stt/transcribe/segment` — JSON (`pcm_bytes`, `format`, `model`)
- `POST /v1/playground/grpc/stt-transcribe` — bridge HTTP para gRPC STT

#### WebSocket STT (porta 8081)

1. Conectar em `ws://host:8081/v1/stt/stream`
2. Handshake JSON: `{session_id, format, sample_rate, model?, language?}`
3. Enviar frames binários PCM/RTP
4. Enviar `{"type":"flush","model?":"auto"}` para transcrição final
5. Receber JSON com `text`, `segments`, `processing_ms`, `model`

#### gRPC STT

- `SpeechToTextService.Ready`
- `SpeechToTextService.Transcribe` (campo `model`)
- `SpeechToTextService.TranscribeStream` (chunks + `flush=true`, campo `model`)

#### Swagger

- `/docs/swagger` — OpenAPI interativo (`openapi/voiceqas.yaml`)

### Playground HTTP (browser)

- `POST /v1/playground/grpc/ready`
- `POST /v1/playground/grpc/analyze-batch`
- `POST /v1/playground/grpc/analyze-stream` (SSE)

## Critérios de aceitação observáveis

1. Silêncio → `stt_ready=false`, score baixo
2. Tom/fala simulada limpa → score alto, possível `stt_ready=true` após histerese
3. Clipping > 5% → `stt_ready=false`
4. Mesmo PCM produz mesmo report em REST, WS e playground gRPC

## Docker

- **voiceqas** — REST + gRPC + WebSocket + STT embutido + Swagger
- **tester-web** — React 19 + Vite, gravação browser, teste STT com seletor de modelo

Ver `README.md` e `tester-web/`.

- C++26 (`-std=c++26`)
- Dependências via vcpkg manifest (`vcpkg.json`) com overrides para releases estáveis mais recentes
