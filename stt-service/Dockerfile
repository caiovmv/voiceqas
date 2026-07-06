# syntax=docker/dockerfile:1
FROM python:3.12-slim-bookworm AS runtime

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    STT_MODEL_REPO=alefiury/parakeet-tdt-0.6b-v3-ptBR-TAGARELA-onnx \
    STT_MODEL_ARCH=nemo-conformer-tdt \
    STT_LANGUAGE=pt \
    HF_HOME=/models/hf

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app/ app/

# Pré-download do modelo ONNX (evita cold start longo no primeiro request)
RUN python - <<'PY'
import os
os.environ.setdefault("HF_HOME", "/models/hf")
import onnx_asr
onnx_asr.load_model(
    os.environ["STT_MODEL_ARCH"],
    os.environ["STT_MODEL_REPO"],
)
print("model cached")
PY

EXPOSE 8090

HEALTHCHECK --interval=30s --timeout=10s --start-period=120s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8090/health || exit 1

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8090"]
