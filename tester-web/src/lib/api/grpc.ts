import type { BatchResult, CodecConfig, SttResult } from '../types';
import { FORMAT_ENUM } from '../types';
import type { EncodedPayload } from '../audio';
import { apiBase, fetchJson } from './client';
import { concatFrames } from './util';

export async function grpcReady() {
  return fetchJson<Record<string, unknown>>('/v1/playground/grpc/ready', { method: 'POST' });
}

export async function transcribeGrpcBatch(
  encoded: EncodedPayload,
  config: CodecConfig,
): Promise<SttResult> {
  const pcmBytes = Array.from(concatFrames(encoded.frames));
  return fetchJson<SttResult>('/v1/playground/grpc/stt-transcribe', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      format: FORMAT_ENUM[config.format],
      sample_rate: config.sampleRate,
      pcm_bytes: pcmBytes,
      model: config.sttModel,
      provider: config.sttProvider,
      language: 'pt',
      session_id: `${config.sessionId}-stt`,
    }),
  });
}

export async function analyzeGrpcBatch(
  encoded: EncodedPayload,
  config: CodecConfig,
): Promise<BatchResult> {
  const pcmBytes = Array.from(concatFrames(encoded.frames));
  return fetchJson<BatchResult>('/v1/playground/grpc/analyze-batch', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      format: FORMAT_ENUM[config.format],
      sample_rate: config.sampleRate,
      pcm_bytes: pcmBytes,
      session_id: config.sessionId,
    }),
  });
}

export async function analyzeGrpcStream(
  encoded: EncodedPayload,
  config: CodecConfig,
  onReport: (r: import('../types').WindowMetrics) => void,
): Promise<void> {
  const frames = encoded.frames.map((frame, i) => ({
    timestamp_ms: i * config.frameMs,
    pcm_bytes: Array.from(frame),
  }));

  const res = await fetch(`${apiBase}/v1/playground/grpc/analyze-stream`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      session_id: config.sessionId,
      format: FORMAT_ENUM[config.format],
      frames,
    }),
  });
  if (!res.ok) throw new Error(await res.text());

  const text = await res.text();
  for (const line of text.split('\n')) {
    if (!line.startsWith('data: ')) continue;
    const json = JSON.parse(line.slice(6)) as import('../types').WindowMetrics;
    onReport(json);
  }
}
