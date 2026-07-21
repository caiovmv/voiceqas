# voiceqas

Serviço C++ de **avaliação de qualidade de voz** para áudio de tronco SIP (PCM e RTP: G.711, G.722, G.729), com gate **STT-ready**, **transcrição STT embutida** (sherpa-onnx) e **media relay UDP** (AGC inbound + egress do agente no codec negociado).

Contexto para desenvolvimento com agentes: [`AGENTS.md`](AGENTS.md).

## Stack

| Componente | Versão |
|------------|--------|
| **C++** | **26** (`-std=c++26`, GCC 15+) |
| **CMake** | 3.31+ |
| **Gerenciador de deps** | [vcpkg](https://vcpkg.io) (master, baseline atualizado no build) |
| gRPC / protobuf | última via vcpkg |
| Boost.Beast | última via vcpkg |
| nlohmann-json | 3.12.0 |
| cpp-httplib | 0.48.0 |
| yaml-cpp | 0.9.0 |
| GoogleTest | 1.17.0 (feature `test`) |

## Interfaces

| Interface | Interno (container) | Host Docker Compose | Uso |
|-----------|---------------------|---------------------|-----|
| REST + Docs + UI | 8080 | **9080** | API batch, process-audio, **STT**, media, Swagger |
| WebSocket | 8081 | **9081** | VQA `/v1/stream` · STT `/v1/stt/stream` · ops |
| gRPC | 50051 | **9051** | `VoiceQualityService` + `SpeechToTextService` + `MediaRelayService` |
| **Media RTP (UDP)** | **10000** | **10000** | Relay inbound interlocutor + outbound agente |
| **Tester Web (React 19)** | 80 | **3000** | Gravação, codecs, VQA e STT |

Exemplos curl abaixo usam as **portas host** (`9080` / `9081` / `9051`). Build local sem compose: `8080` / `8081` / `50051`.

## Docker (recomendado)

```bash
cd voiceqas
docker run --rm -v voiceqas_stt-models:/models/stt -v "$PWD/scripts:/scripts" debian:trixie-slim \
  bash -c "apt-get update -qq && apt-get install -y -qq curl bzip2 && bash /scripts/download-stt-models.sh /models/stt"
docker compose up --build
```

### Serviços

| Serviço | URL | Notas |
|---------|-----|-------|
| **voiceqas** | REST `http://localhost:9080` | Mapeado 9080→8080 (evita conflito local) |
| **command-center** | `http://localhost:3000` | Tester `#tester` · NOC `#command-center` |
| **Grafana** | `http://localhost:3001` | admin/admin · dashboard VoiceQAS |
| **Prometheus** | `http://localhost:9090` | Scrape `/metrics` |
| **Aloy** | `http://localhost:12345` | Logs Docker → Loki |

Tokens dev no compose: `dev-read`, `dev-write`, `dev-admin` (`X-Ops-Token`).

Modelos STT (~1,6 GB) no volume `voiceqas_stt-models`: parakeet, whisper, 4 variantes Silero VAD.

Ver [`docs/spec/audio-pipeline.md`](docs/spec/audio-pipeline.md) e [`docs/spec/command-center-ui.md`](docs/spec/command-center-ui.md).

### Tester Web (React 19 + Vite)

http://localhost:3000

- Gravação de áudio via microfone (`getUserMedia`) ou upload WAV
- Simulação de codecs: PCM 8/16 kHz, RTP G.711 (PCMU/PCMA), G.722 wideband, G.729
- Config: frame size, clipping, perda de pacotes RTP
- Envio para avaliação via **REST**, **WebSocket** ou **gRPC playground**
- Nginx no container faz proxy `/api` → voiceqas:8080 e `/ws` → voiceqas:8081

Dev local (sem Docker):

```bash
cd tester-web
npm install
npm run dev   # http://localhost:5173 com proxy para voiceqas
```

Builder backend: `gcc:15-bookworm` + vcpkg master + CMake 3.31+. Runtime: `debian:trixie-slim`.

Versões fixadas no manifest (`vcpkg.json` overrides): nlohmann-json 3.12.0, cpp-httplib 0.48.0, yaml-cpp 0.9.0, gtest 1.17.0. gRPC/protobuf/boost resolvem na última versão compatível do vcpkg.

```bash
docker compose build --build-arg GCC_VERSION=15 --build-arg CPP_STD=c++26
docker compose up
```

O arquivo `/app/dependency-versions.txt` dentro do container lista as libs instaladas pelo vcpkg no build.

Acesse (host Docker):

- **Tester React:** http://localhost:3000
- **Swagger UI:** http://localhost:9080/docs/swagger
- **ReDoc:** http://localhost:9080/docs/redoc
- **Console legado:** http://localhost:9080/ui/
- **WebSocket tester:** http://localhost:9080/ui/ws.html
- **gRPC playground:** http://localhost:9080/ui/grpc.html

### Imagem leve

- Multi-stage build
- Runtime sem toolchain (só `.so` necessárias + `curl` para healthcheck)
- `read_only` + `tmpfs` + limite 256MB RAM no compose
- UI estática servida pelo próprio binário (sem nginx extra)

## Build local (vcpkg)

Requer **GCC 15+** (ou Clang 19+ com suporte a C++26).

```bash
cd voiceqas
./scripts/build.sh
./build/voiceqas-server --config config/voiceqas.example.yaml
```

O script clona vcpkg em `.vcpkg/`, atualiza o baseline e compila com Ninja.

Build manual:

```bash
git clone --depth 1 https://github.com/microsoft/vcpkg.git .vcpkg
./.vcpkg/bootstrap-vcpkg.sh -disableMetrics
./.vcpkg/vcpkg x-update-baseline --add-initial-baseline

cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=.vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Audio pipeline (sem STT)

O channel strip (`Decode → NR → HPF → EQ → DeEss → Comp → Lim → AGC`) corre no ingress independentemente do STT estar ligado. Com STT ativo no serviço, **estes endpoints não disparam transcrição**; para ASR use `/v1/stt/*` (seção abaixo).

| Transport | Endpoint | O que volta |
|-----------|----------|-------------|
| **REST** | `POST /v1/tools/process-audio` | **WAV mono PCM16 tratado** (único path que devolve áudio) |
| **WebSocket** | `ws://…/v1/stream` | JSON `QualityReport` por janela (~500 ms) |
| **gRPC** | `AnalyzeBatch` / `AnalyzeStream` | JSON scores / `QualityReport` (não PCM) |

Defaults S15: NR off, DeEss off, mild EQ, soft compressor, limiter −1 dBFS, AGC −18 dBFS. Knobs: `X-Audio-AGC`, `X-Audio-Enhancement`, `X-Audio-Strip` (JSON). Spec: [`docs/spec/audio-pipeline.md`](docs/spec/audio-pipeline.md).

### REST — obter áudio tratado

```bash
# WAV → WAV processado (format/rate inferidos do RIFF)
curl -sS -X POST "http://localhost:9080/v1/tools/process-audio" \
  -H "Content-Type: audio/wav" \
  -H "X-Session-Id: strip-lab-1" \
  -H "X-Audio-AGC: 1" \
  -H "X-Audio-Enhancement: 0" \
  --data-binary @input.wav \
  -o voiceqas-mix-strip.wav

# PCM16 LE 16 kHz cru → WAV processado (+ strip S15 via header)
curl -sS -X POST "http://localhost:9080/v1/tools/process-audio" \
  -H "Content-Type: application/octet-stream" \
  -H "X-Audio-Format: pcm_s16le_16k" \
  -H "X-Sample-Rate: 16000" \
  -H "X-Audio-AGC: 1" \
  -H "X-Audio-Enhancement: 0" \
  -H 'X-Audio-Strip: {"nr":{"enabled":false,"wet_dry":1.0},"hpf":{"enabled":true,"cutoff_hz":80},"eq":{"enabled":true,"bands":[{"freq_hz":250,"gain_db":-1.5,"q":1.0},{"freq_hz":450,"gain_db":-1.0,"q":1.2},{"freq_hz":2500,"gain_db":1.0,"q":0.8},{"freq_hz":3500,"gain_db":1.0,"q":0.8}]},"deesser":{"enabled":false},"compressor":{"enabled":true,"threshold_db":-20,"ratio":2,"attack_ms":5,"release_ms":80,"makeup_db":0},"limiter":{"enabled":true,"ceiling_dbfs":-1},"agc":{"enabled":true,"target_rms_dbfs":-18,"max_gain_db":24,"attack_ms":5,"release_ms":100}}' \
  --data-binary @input.pcm \
  -o voiceqas-mix-strip.wav
```

Resposta: `Content-Type: audio/wav`, header `X-Sample-Rate`. Timeline completa (sem máscara Silero).

Análise só métricas (strip aplicado, sem WAV de volta):

```bash
curl -sS -X POST "http://localhost:9080/v1/analyze/batch" \
  -H "Content-Type: application/octet-stream" \
  -H "X-Audio-Format: pcm_s16le_16k" \
  -H "X-Sample-Rate: 16000" \
  -H "X-Audio-AGC: 1" \
  -H "X-Audio-Enhancement: 0" \
  --data-binary @input.pcm | jq
```

### WebSocket — stream pelo pipeline

Handshake JSON, depois frames **binários** PCM/RTP. Resposta: JSON de qualidade (não áudio).

```bash
# Requer websocat: https://github.com/vi/websocat
# Terminal 1 — conexão + handshake
printf '%s\n' '{"session_id":"ws-strip-1","format":"pcm_s16le_16k"}' \
  | websocat -b ws://localhost:9081/v1/stream

# Terminal 2 — (mesmo socket) enviar PCM: use um cliente que mande binary frames
# Exemplo com Python (após handshake OK):
python - <<'PY'
import asyncio, json, pathlib, websockets
async def main():
    uri = "ws://localhost:9081/v1/stream"
    pcm = pathlib.Path("input.pcm").read_bytes()
    async with websockets.connect(uri) as ws:
        await ws.send(json.dumps({"session_id": "ws-strip-1", "format": "pcm_s16le_16k"}))
        print(await ws.recv())  # {"status":"ok","mode":"quality"}
        # chunks ~20 ms @ 16 kHz mono s16le = 640 bytes
        for i in range(0, len(pcm), 640):
            await ws.send(pcm[i:i+640])
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=0.05)
                print(msg)
            except asyncio.TimeoutError:
                pass
asyncio.run(main())
PY
```

### gRPC — AnalyzeBatch / AnalyzeStream

Host compose: `localhost:9051` (plaintext).

```bash
grpcurl -plaintext localhost:9051 voiceqas.v1.VoiceQualityService/Ready

# AnalyzeBatch: payload = bytes do PCM (grpcurl lê arquivo com @)
# Formato enum: PCM_S16LE_16K = 2
grpcurl -plaintext -d @ localhost:9051 voiceqas.v1.VoiceQualityService/AnalyzeBatch <<EOF
{
  "format": "PCM_S16LE_16K",
  "sample_rate": 16000,
  "payload": "$(base64 -w0 input.pcm 2>/dev/null || base64 -i input.pcm)"
}
EOF
```

Sem `grpcurl`, use o bridge HTTP (mesmo strip / métricas, sem STT):

```bash
# payload_bytes = array de bytes PCM, ou payload_base64
python - <<'PY' | curl -sS -X POST "http://localhost:9080/v1/playground/grpc/analyze-batch" \
  -H "Content-Type: application/json" -d @- | jq
import json, pathlib, base64
pcm = pathlib.Path("input.pcm").read_bytes()
print(json.dumps({
    "format": "pcm_s16le_16k",
    "sample_rate": 16000,
    "session_id": "grpc-strip-1",
    "payload_base64": base64.b64encode(pcm).decode(),
}))
PY
```

Playground no browser: http://localhost:9080/ui/grpc.html · SSE stream: `POST /v1/playground/grpc/analyze-stream`.

## STT (transcrição)

```bash
# Readiness do motor STT
curl -s http://localhost:9080/v1/stt/ready | jq

# Transcrever WAV
curl -s -X POST http://localhost:9080/v1/stt/transcribe \
  -H "Content-Type: audio/wav" \
  -H "X-STT-Model: auto" \
  --data-binary @gravacao.wav | jq
```

Endpoint dedicado: **`POST /v1/stt/transcribe`** (aceita os mesmos formatos de áudio da análise VQA).

## Media relay (pipeline bidirecional)

Documentação completa: [`docs/spec/audio-pipeline.md`](docs/spec/audio-pipeline.md)

### Registrar sessão

```bash
curl -s -X POST http://localhost:9080/v1/media/sessions \
  -H "Content-Type: application/json" \
  -H "X-Ops-Token: dev-write" \
  -d '{
    "session_id": "call-1",
    "format": "rtp_pcmu",
    "remote_host": "10.0.0.5",
    "remote_port": 12000,
    "sample_rate": 8000
  }' | jq
```

### Enviar áudio do agente (PCM16 → G.711 → RTP/UDP)

```bash
curl -s -X POST http://localhost:9080/v1/media/sessions/call-1/agent-audio \
  -H "X-Ops-Token: dev-write" \
  -H "Content-Type: application/octet-stream" \
  -H "X-Sample-Rate: 16000" \
  --data-binary @agent.pcm | jq
```

### Encerrar sessão

```bash
curl -s -X DELETE http://localhost:9080/v1/media/sessions/call-1 \
  -H "X-Ops-Token: dev-write"
```

RTP inbound do interlocutor: enviar pacotes UDP para `localhost:10000`. O pipeline aplica decode + channel strip antes de VQA/STT.

## Variáveis de ambiente

| Variável | Default |
|----------|---------|
| `VOICEQAS_REST_ADDR` | `0.0.0.0:8080` |
| `VOICEQAS_STT_LANGUAGE` | `pt` |
| `VOICEQAS_STT_DEFAULT_MODEL` | `auto` |
| `VOICEQAS_STT_MODELS_DIR` | `/models/stt` |
| `VOICEQAS_STT_THREADS` | `2` |
| `VOICEQAS_GRPC_ADDR` | `0.0.0.0:50051` |
| `VOICEQAS_WS_ADDR` | `0.0.0.0:8081` |
| `VOICEQAS_WEB_ROOT` | `web` |
| `VOICEQAS_OPENAPI_PATH` | `openapi/voiceqas.yaml` |
| `VOICEQAS_STT_THRESHOLD` | `65` |
| `VOICEQAS_MEDIA_RTP_ADDR` | `0.0.0.0:10000` |
| `VOICEQAS_AUDIO_NORMALIZE` | `true` |

## Playground gRPC no browser

O serviço expõe endpoints HTTP que espelham o contrato protobuf para testes sem grpc-web/Envoy:

- `POST /v1/playground/grpc/ready`
- `POST /v1/playground/grpc/analyze-batch`
- `POST /v1/playground/grpc/analyze-stream` (SSE)

Produção / Docker Compose: gRPC na porta host **9051** (interno 50051).

## Atualizar dependências

```bash
./.vcpkg/vcpkg x-update-baseline
# opcional: editar overrides em vcpkg.json
./.vcpkg/vcpkg upgrade --no-dry-run
```

## License

This project is licensed under the **Apache License, Version 2.0**. See [`LICENSE`](LICENSE).

Third-party attributions (SpanDSP LGPL-2.1, RNNoise, sherpa-onnx, codecs, UI deps, etc.) are listed in [`NOTICE`](NOTICE).
