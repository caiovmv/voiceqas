import type { BatchResult, CodecConfig, WindowMetrics } from '../types';
import type { EncodedPayload } from '../audio';
import { apiBase, resolveWsBase } from './client';
import { concatFrames, parseWsJson, sleep } from './util';

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
      'X-Session-Id': config.sessionId,
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

export function analyzeWebSocket(
  encoded: EncodedPayload,
  config: CodecConfig,
  onReport: (r: WindowMetrics) => void,
  onStatus: (msg: string) => void,
  dropPct: number,
): { close: () => void; send: () => Promise<void> } {
  const ws = new WebSocket(`${resolveWsBase()}/v1/stream`);
  let handshakeOk = false;
  let resolveHandshake: (() => void) | null = null;
  let rejectHandshake: ((err: Error) => void) | null = null;
  const handshakePromise = new Promise<void>((resolve, reject) => {
    resolveHandshake = resolve;
    rejectHandshake = reject;
    setTimeout(() => reject(new Error('VQA handshake timeout')), 10_000);
  });

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
      const data = parseWsJson(ev.data as string) as Record<string, unknown>;
      if (data.status === 'ok') {
        handshakeOk = true;
        resolveHandshake?.();
        onStatus('Handshake OK');
        return;
      }
      if (data.error) {
        rejectHandshake?.(new Error(String(data.error)));
        return;
      }
      onReport(data as unknown as WindowMetrics);
    } catch {
      onStatus(String(ev.data));
    }
  };

  ws.onerror = () => {
    onStatus('Erro WebSocket');
    rejectHandshake?.(new Error('WebSocket connection failed'));
  };
  ws.onclose = () => onStatus('WebSocket fechado');

  const send = async () => {
    await handshakePromise;
    if (ws.readyState !== WebSocket.OPEN || !handshakeOk) {
      throw new Error('WebSocket não está pronto');
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
