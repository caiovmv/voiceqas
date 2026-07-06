#!/usr/bin/env bash
set -euo pipefail

MODELS_DIR="${1:-/models/stt}"
BASE_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models"

mkdir -p "${MODELS_DIR}"

download_and_extract() {
  local archive="$1"
  local url="${BASE_URL}/${archive}"
  if [[ -d "${MODELS_DIR}/${archive%.tar.bz2}" ]]; then
    echo "already present: ${archive}"
    return
  fi
  echo "downloading ${archive}..."
  curl -fsSL "${url}" -o "/tmp/${archive}"
  tar -xjf "/tmp/${archive}" -C "${MODELS_DIR}"
  rm -f "/tmp/${archive}"
}

download_and_extract "sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2"
download_and_extract "sherpa-onnx-whisper-turbo.tar.bz2"

echo "STT models ready under ${MODELS_DIR}"
ls -la "${MODELS_DIR}"
