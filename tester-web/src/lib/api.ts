import type { BatchResult, CodecConfig, SttModel, SttResult, WindowMetrics } from './types';
import { FORMAT_ENUM } from './types';
import type { EncodedPayload } from './audio';

const apiBase = import.meta.env.VITE_API_BASE ?? '/api';
const wsBase =
  import.meta.env.VITE_WS_BASE ||
  `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`;

async function fetchJson<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${apiBase}${path}`, init);
  const text = await res.text();
  let data: unknown;
  try {
    data = JSON.parse(text);
  } catch {
    data = { raw: text };
  }
  if (!res.ok) {
    const err = data as { error?: string };
    throw new Error(err.error ?? res.statusText ?? text);
  }
  return data as T;
}

export async function checkReady() {
  return fetchJson<Record<string, unknown>>('/ready');
}

export async function checkSttReady() {
  return fetchJson<{ status: string; models?: Array<{ id: string; name: string; ready: boolean }> }>(
    '/v1/stt/ready',
  );
}

export async function transcribeRestBatch(
  encoded: EncodedPayload,
  config: CodecConfig,
): Promise<SttResult> {
  const allBytes = concatFrames(encoded.frames);
  const res = await fetch(`${apiBase}/v1/stt/transcribe`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/octet-stream',
      'X-Audio-Format': config.format,
      'X-Sample-Rate': String(config.sampleRate),
      'X-Language': 'pt',
      'X-STT-Model': config.sttModel,
    },
    body: allBytes,
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(text);
  }
  return res.json();
}

export async function transcribeRestWav(wav: Blob, model: SttModel = 'auto'): Promise<SttResult> {
  const res = await fetch(`${apiBase}/v1/stt/transcribe?model=${encodeURIComponent(model)}`, {
    method: 'POST',
    headers: {
      'Content-Type': 'audio/wav',
      'X-STT-Model': model,
      'X-Language': 'pt',
    },
    body: wav,
  });
  if (!res.ok) {
    throw new Error(await res.text());
  }
  return res.json();
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
      language: 'pt',
    }),
  });
}

export function transcribeWebSocket(
  encoded: EncodedPayload,
  config: CodecConfig,
  onStatus: (msg: string) => void,
): { close: () => void; send: () => Promise<SttResult> } {
  const ws = new WebSocket(`${wsBase}/v1/stt/stream`);

  let resolveFlush: ((value: SttResult) => void) | null = null;

  ws.onopen = () => {
    ws.send(
      JSON.stringify({
        session_id: `${config.sessionId}-stt`,
        format: config.format,
        sample_rate: config.sampleRate,
        model: config.sttModel,
        language: 'pt',
      }),
    );
    onStatus('STT WebSocket conectado');
  };

  ws.onmessage = (ev) => {
    try {
      const data = JSON.parse(ev.data as string);
      if (data.status === 'ok') {
        onStatus('STT handshake OK');
        return;
      }
      if (data.type === 'final' || data.type === 'error' || data.text !== undefined) {
        if (resolveFlush) {
          resolveFlush(data as SttResult);
          resolveFlush = null;
        }
      }
    } catch {
      onStatus(String(ev.data));
    }
  };

  ws.onerror = () => onStatus('Erro STT WebSocket');

  const send = async () => {
    if (ws.readyState !== WebSocket.OPEN) {
      throw new Error('STT WebSocket não está aberto');
    }
    for (const frame of encoded.frames) {
      ws.send(frame);
      await sleep(config.frameMs);
    }
    const result = await new Promise<SttResult>((resolve, reject) => {
      resolveFlush = resolve;
      ws.send(JSON.stringify({ type: 'flush', model: config.sttModel }));
      setTimeout(() => reject(new Error('STT flush timeout')), 120_000);
    });
    ws.close();
    return result;
  };

  return {
    close: () => ws.close(),
    send,
  };
}

export async function analyzeRestBatch(
  encoded: EncodedPayload,
  config: CodecConfig,
): Promise<BatchResult> {
  const allBytes = concatFrames(encoded.frames);
  const res = await fetch(`${apiBase}/v1/analyze/batch`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/octet-stream',
      'X-Audio-Format': config.format,
      'X-Sample-Rate': String(config.sampleRate),
    },
    body: allBytes,
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(text);
  }
  return res.json();
}

export async function analyzeRestWav(wav: Blob): Promise<BatchResult> {
  const res = await fetch(`${apiBase}/v1/analyze/batch`, {
    method: 'POST',
    headers: { 'Content-Type': 'audio/wav' },
    body: wav,
  });
  if (!res.ok) throw new Error(await res.text());
  return res.json();
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
    }),
  });
}

export async function grpcReady() {
  return fetchJson<Record<string, unknown>>('/v1/playground/grpc/ready', { method: 'POST' });
}

export async function analyzeGrpcStream(
  encoded: EncodedPayload,
  config: CodecConfig,
  onReport: (r: WindowMetrics) => void,
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
    const json = JSON.parse(line.slice(6)) as WindowMetrics;
    onReport(json);
  }
}

export function analyzeWebSocket(
  encoded: EncodedPayload,
  config: CodecConfig,
  onReport: (r: WindowMetrics) => void,
  onStatus: (msg: string) => void,
  dropPct: number,
): { close: () => void; send: () => Promise<void> } {
  const ws = new WebSocket(`${wsBase}/v1/stream`);

  ws.onopen = () => {
    ws.send(
      JSON.stringify({
        session_id: config.sessionId,
        format: config.format,
        sample_rate: config.sampleRate,
        timestamp_ms: 0,
      }),
    );
    onStatus('WebSocket conectado, handshake enviado');
  };

  ws.onmessage = (ev) => {
    try {
      const data = JSON.parse(ev.data as string);
      if (data.status === 'ok') {
        onStatus('Handshake OK');
        return;
      }
      onReport(data as WindowMetrics);
    } catch {
      onStatus(String(ev.data));
    }
  };

  ws.onerror = () => onStatus('Erro WebSocket');
  ws.onclose = () => onStatus('WebSocket fechado');

  const send = async () => {
    if (ws.readyState !== WebSocket.OPEN) {
      throw new Error('WebSocket não está aberto');
    }
    for (let i = 0; i < encoded.frames.length; i++) {
      if (dropPct > 0 && Math.random() * 100 < dropPct) continue;
      ws.send(encoded.frames[i]);
      await sleep(config.frameMs);
    }
  };

  return {
    close: () => ws.close(),
    send,
  };
}

function concatFrames(frames: Uint8Array[]): Uint8Array {
  const total = frames.reduce((n, f) => n + f.length, 0);
  const out = new Uint8Array(total);
  let off = 0;
  for (const f of frames) {
    out.set(f, off);
    off += f.length;
  }
  return out;
}

function sleep(ms: number) {
  return new Promise((r) => setTimeout(r, ms));
}
