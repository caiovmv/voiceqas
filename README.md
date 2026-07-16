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

| Interface | Porta | Uso |
|-----------|-------|-----|
| REST + Docs + UI | 8080 | API batch, **STT**, media sessions, Swagger |
| WebSocket | 8081 | VQA `/v1/stream` · STT `/v1/stt/stream` |
| gRPC | 50051 | `VoiceQualityService` + `SpeechToTextService` |
| **Media RTP (UDP)** | **10000** | Relay inbound interlocutor + outbound agente G.711/G.722/G.729 |
| **Tester Web (React 19)** | **3000** | Gravação, codecs, VQA e STT |

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

Acesse:

- **Tester React:** http://localhost:3000
- **Console legado:** http://localhost:8080/ui/
- **Swagger UI:** http://localhost:8080/docs/swagger
- **ReDoc:** http://localhost:8080/docs/redoc
- **WebSocket tester:** http://localhost:8080/ui/ws.html
- **gRPC playground:** http://localhost:8080/ui/grpc.html

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

## gRPC nativo (grpcurl)

```bash
grpcurl -plaintext localhost:50051 voiceqas.v1.VoiceQualityService/Ready
grpcurl -plaintext localhost:50051 voiceqas.v1.SpeechToTextService/Ready
```

## STT (transcrição)

```bash
# Readiness do motor STT
curl -s http://localhost:8080/v1/stt/ready | jq

# Transcrever WAV
curl -s -X POST http://localhost:8080/v1/stt/transcribe \
  -H "Content-Type: audio/wav" \
  -H "X-STT-Model: auto" \
  --data-binary @gravacao.wav | jq
```

Endpoint dedicado: **`POST /v1/stt/transcribe`** (aceita os mesmos formatos de áudio da análise VQA).

## Media relay (pipeline bidirecional)

Documentação completa: [`docs/spec/audio-pipeline.md`](docs/spec/audio-pipeline.md)

### Registrar sessão

```bash
curl -s -X POST http://localhost:8080/v1/media/sessions \
  -H "Content-Type: application/json" \
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
curl -s -X POST http://localhost:8080/v1/media/sessions/call-1/agent-audio \
  -H "Content-Type: application/octet-stream" \
  -H "X-Sample-Rate: 16000" \
  --data-binary @agent.pcm | jq
```

### Encerrar sessão

```bash
curl -s -X DELETE http://localhost:8080/v1/media/sessions/call-1
```

RTP inbound do interlocutor: enviar pacotes UDP para `localhost:10000`. O pipeline aplica decode + AGC antes de VQA/STT.

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

Produção deve usar gRPC na porta 50051.

## Atualizar dependências

```bash
./.vcpkg/vcpkg x-update-baseline
# opcional: editar overrides em vcpkg.json
./.vcpkg/vcpkg upgrade --no-dry-run
```

## License

This project is licensed under the **Apache License, Version 2.0**. See [`LICENSE`](LICENSE).

Third-party attributions (SpanDSP LGPL-2.1, RNNoise, sherpa-onnx, codecs, UI deps, etc.) are listed in [`NOTICE`](NOTICE).
