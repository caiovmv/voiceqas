import { opsAuthHeaders } from '../auth';
import type { CodecConfig, SttModel, SttResult } from '../types';
import type { EncodedPayload } from '../audio';
import { shouldDropFrame } from '../audio';
import { apiBase, fetchJson, resolveWsBase } from './client';
import { concatFrames, parseWsJson, sleep } from './util';

const wsBase = resolveWsBase();

export async function setVadModel(model: string) {
  return fetchJson<{
    status: string;
    model: string;
    active_id: string;
    active_name: string;
    vad_ready: boolean;
  }>('/v1/stt/vad/model', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...opsAuthHeaders() },
    body: JSON.stringify({ model }),
  });
}

export async function checkSttReady() {
  return fetchJson<{
    status: string;
    provider?: string;
    providers_available?: string[];
    loaded_providers?: string[];
    cuda_compiled?: boolean;
    models?: Array<{
      id: string;
      name: string;
      ready: boolean;
      active?: boolean;
      active_id?: string;
      path?: string;
    }>;
  }>('/v1/stt/ready');
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
      'X-STT-Provider': config.sttProvider,
      'X-Session-Id': `${config.sessionId}-stt`,
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

export function transcribeWebSocket(
  encoded: EncodedPayload,
  config: CodecConfig,
  onStatus: (msg: string) => void,
): { close: () => void; send: () => Promise<SttResult> } {
  const ws = new WebSocket(`${wsBase}/v1/stt/stream`);

  let resolveFlush: ((value: SttResult) => void) | null = null;
  let handshakeOk = false;
  let resolveHandshake: (() => void) | null = null;
  let rejectHandshake: ((err: Error) => void) | null = null;
  const handshakePromise = new Promise<void>((resolve, reject) => {
    resolveHandshake = resolve;
    rejectHandshake = reject;
    setTimeout(() => reject(new Error('STT handshake timeout')), 10_000);
  });

  ws.onopen = () => {
    ws.send(
      JSON.stringify({
        session_id: `${config.sessionId}-stt`,
        format: config.format,
        sample_rate: config.sampleRate,
        model: config.sttModel,
        provider: config.sttProvider,
        language: 'pt',
      }),
    );
    onStatus('STT WebSocket conectado, handshake enviado');
  };

  ws.onmessage = (ev) => {
    try {
      const data = parseWsJson(ev.data as string) as Record<string, unknown>;
      if (data.status === 'ok') {
        handshakeOk = true;
        resolveHandshake?.();
        onStatus('STT handshake OK');
        return;
      }
      if (data.error) {
        rejectHandshake?.(new Error(String(data.error)));
        return;
      }
      if (data.type === 'final' || data.type === 'error' || data.text !== undefined) {
        if (resolveFlush) {
          resolveFlush(data as unknown as SttResult);
          resolveFlush = null;
        }
      }
    } catch {
      onStatus(String(ev.data));
    }
  };

  ws.onerror = () => onStatus('Erro STT WebSocket');

  const send = async () => {
    await handshakePromise;
    if (ws.readyState !== WebSocket.OPEN || !handshakeOk) {
      throw new Error('STT WebSocket não está pronto');
    }
    for (let i = 0; i < encoded.frames.length; i++) {
      if (config.simulatePacketLossPct > 0 && shouldDropFrame(i, config.simulatePacketLossPct)) {
        continue;
      }
      ws.send(encoded.frames[i]);
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
