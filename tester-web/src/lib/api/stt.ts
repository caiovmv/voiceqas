import { opsAuthHeaders } from '../auth';
import type { CodecConfig, SttModel, SttResult } from '../types';
import type { EncodedPayload } from '../audio';
import { shouldDropFrame } from '../audio';
import { apiBase, fetchJson, resolveWsBase } from './client';
import { concatFrames, parseWsEventData, parseWsJson, sleep } from './util';

const STT_FLUSH_TIMEOUT_MS = 180_000;

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
  extraHeaders: Record<string, string> = {},
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
      ...extraHeaders,
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

function parseSttWsMessage(raw: unknown): Record<string, unknown> {
  if (typeof raw === 'string') {
    return parseWsJson(raw) as Record<string, unknown>;
  }
  throw new Error('STT WebSocket: mensagem inesperada (binária)');
}

export function transcribeWebSocket(
  encoded: EncodedPayload,
  config: CodecConfig,
  onStatus: (msg: string) => void,
): { close: () => void; send: () => Promise<SttResult> } {
  const wsBase = resolveWsBase();
  const ws = new WebSocket(`${wsBase}/v1/stt/stream`);
  ws.binaryType = 'arraybuffer';

  let resolveFlush: ((value: SttResult) => void) | null = null;
  let rejectFlush: ((err: Error) => void) | null = null;
  let flushTimer: ReturnType<typeof setTimeout> | null = null;
  let handshakeOk = false;
  let flushDone = false;
  let resolveHandshake: (() => void) | null = null;
  let rejectHandshake: ((err: Error) => void) | null = null;
  let handshakeTimer: ReturnType<typeof setTimeout> | null = null;

  const clearFlushWait = () => {
    if (flushTimer) {
      clearTimeout(flushTimer);
      flushTimer = null;
    }
    resolveFlush = null;
    rejectFlush = null;
  };

  const clearHandshakeWait = () => {
    if (handshakeTimer) {
      clearTimeout(handshakeTimer);
      handshakeTimer = null;
    }
    resolveHandshake = null;
    rejectHandshake = null;
  };

  const handshakePromise = new Promise<void>((resolve, reject) => {
    resolveHandshake = resolve;
    rejectHandshake = reject;
    handshakeTimer = setTimeout(() => reject(new Error('STT handshake timeout')), 15_000);
  });

  const handleJson = (data: Record<string, unknown>) => {
    if (data.status === 'ok' && !handshakeOk) {
      handshakeOk = true;
      clearHandshakeWait();
      resolveHandshake?.();
      onStatus('STT handshake OK');
      return;
    }
    if (data.error) {
      const err = new Error(String(data.error));
      if (!handshakeOk) {
        clearHandshakeWait();
        rejectHandshake?.(err);
      } else if (rejectFlush) {
        const reject = rejectFlush;
        clearFlushWait();
        reject(err);
      }
      return;
    }
    if (!resolveFlush) return;
    if (data.type === 'final') {
      const result = data as unknown as SttResult;
      if (result.ok === false) {
        const reject = rejectFlush;
        clearFlushWait();
        reject?.(new Error(result.error ?? 'STT falhou'));
        return;
      }
      const resolve = resolveFlush;
      clearFlushWait();
      flushDone = true;
      resolve(result);
      return;
    }
    if (data.type === 'error') {
      const reject = rejectFlush;
      clearFlushWait();
      reject?.(new Error(String(data.error ?? 'STT falhou')));
    }
  };

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
    onStatus(`STT WebSocket conectado (${wsBase})`);
  };

  ws.onmessage = (ev) => {
    void (async () => {
      try {
        let data: Record<string, unknown>;
        if (typeof ev.data === 'string') {
          data = parseSttWsMessage(ev.data);
        } else {
          data = (await parseWsEventData(ev.data)) as Record<string, unknown>;
        }
        handleJson(data);
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        if (!handshakeOk && rejectHandshake) {
          clearHandshakeWait();
          rejectHandshake(new Error(msg));
        } else if (rejectFlush) {
          const reject = rejectFlush;
          clearFlushWait();
          reject(new Error(msg));
        } else {
          onStatus(msg);
        }
      }
    })();
  };

  ws.onerror = () => {
    const err = new Error('Erro STT WebSocket');
    if (!handshakeOk) {
      clearHandshakeWait();
      rejectHandshake?.(err);
    } else if (rejectFlush) {
      const reject = rejectFlush;
      clearFlushWait();
      reject(err);
    }
    onStatus(err.message);
  };

  ws.onclose = () => {
    if (flushDone) return;
    if (!handshakeOk) {
      clearHandshakeWait();
      rejectHandshake?.(new Error('STT WebSocket fechado antes do handshake'));
      return;
    }
    if (rejectFlush) {
      const reject = rejectFlush;
      clearFlushWait();
      reject(new Error('STT WebSocket fechado antes da resposta do flush'));
    }
  };

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
      if (config.frameMs > 0) {
        await sleep(config.frameMs);
      }
    }
    onStatus('Áudio enviado, aguardando flush STT…');
    const result = await new Promise<SttResult>((resolve, reject) => {
      resolveFlush = resolve;
      rejectFlush = reject;
      flushTimer = setTimeout(() => {
        clearFlushWait();
        reject(new Error('STT flush timeout'));
      }, STT_FLUSH_TIMEOUT_MS);
      ws.send(
        JSON.stringify({
          type: 'flush',
          model: config.sttModel,
          provider: config.sttProvider,
          language: 'pt',
        }),
      );
    });
    flushDone = true;
    clearFlushWait();
    ws.close();
    return result;
  };

  return {
    close: () => {
      flushDone = true;
      clearHandshakeWait();
      clearFlushWait();
      ws.close();
    },
    send,
  };
}
