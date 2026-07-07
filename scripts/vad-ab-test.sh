#!/usr/bin/env bash
set -euo pipefail

API_BASE="${API_BASE:-http://127.0.0.1:9080}"
WAV="${1:-}"
TOKEN="${VOICEQAS_OPS_WRITE_TOKEN:-dev-write}"

if [[ -z "${WAV}" || ! -f "${WAV}" ]]; then
  echo "usage: VOICEQAS_OPS_WRITE_TOKEN=... $0 /path/to/sample.wav"
  exit 1
fi

models=(auto k2fsa k2fsa-int8 v5 v4)
printf "%-12s %-8s %-10s %s\n" "model" "vad_ok" "ms" "text_preview"

for model in "${models[@]}"; do
  curl -fsS -X POST "${API_BASE}/v1/stt/vad/model" \
    -H "Content-Type: application/json" \
    -H "X-Ops-Token: ${TOKEN}" \
    -d "{\"model\":\"${model}\"}" >/dev/null

  start_ms=$(date +%s%3N)
  json=$(curl -fsS -X POST "${API_BASE}/v1/stt/transcribe" \
    -H "Content-Type: audio/wav" \
    -H "X-STT-Model: auto" \
    --data-binary @"${WAV}")
  end_ms=$(date +%s%3N)
  elapsed=$((end_ms - start_ms))

  text=$(echo "${json}" | python3 -c "import json,sys; d=json.load(sys.stdin); print((d.get('text') or d.get('error') or '')[:60])" 2>/dev/null || echo "parse_error")
  vad_ok=$(curl -fsS "${API_BASE}/v1/stt/ready" | python3 -c "import json,sys; d=json.load(sys.stdin); print(next((m for m in d.get('models',[]) if m.get('id')=='vad'),{}).get('ready',False))")

  printf "%-12s %-8s %-10s %s\n" "${model}" "${vad_ok}" "${elapsed}" "${text}"
done
