#!/usr/bin/env bash
set -euo pipefail

MODELS_DIR="${1:-/models/stt}"
K2_BASE="https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models"
SN4_V4_URL="https://github.com/snakers4/silero-vad/raw/v4.0/files/silero_vad.onnx"
SN4_V5_URL="https://github.com/snakers4/silero-vad/raw/v5.0/files/silero_vad.onnx"

mkdir -p "${MODELS_DIR}"

download_and_extract() {
  local archive="$1"
  local url="${K2_BASE}/${archive}"
  if [[ -d "${MODELS_DIR}/${archive%.tar.bz2}" ]]; then
    echo "already present: ${archive}"
    return
  fi
  echo "downloading ${archive}..."
  curl -fsSL "${url}" -o "/tmp/${archive}"
  tar -xjf "/tmp/${archive}" -C "${MODELS_DIR}"
  rm -f "/tmp/${archive}"
}

download_file() {
  local name="$1"
  local url="$2"
  local dest="${MODELS_DIR}/${name}"
  if [[ -f "${dest}" ]]; then
    echo "already present: ${name}"
    return
  fi
  echo "downloading ${name}..."
  curl -fsSL "${url}" -o "${dest}"
}

download_and_extract "sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2"
download_and_extract "sherpa-onnx-whisper-turbo.tar.bz2"

# Silero VAD variants (default auto picks k2-fsa silero_vad.onnx first).
download_file "silero_vad.onnx" "${K2_BASE}/silero_vad.onnx"
download_file "silero_vad.int8.onnx" "${K2_BASE}/silero_vad.int8.onnx"
download_file "silero_vad_v4.onnx" "${SN4_V4_URL}"
download_file "silero_vad_v5.onnx" "${SN4_V5_URL}"

echo "STT models ready under ${MODELS_DIR}"
ls -la "${MODELS_DIR}"
