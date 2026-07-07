import type { OpsAlertEvent, OpsEvent } from '../types';
import { fetchJson, resolveWsBase } from './client';
import { opsWsHandshake, parseWsJson } from './util';

const wsBase = resolveWsBase();

export async function fetchOpsHistory(params?: {
  sessionId?: string;
  type?: string;
  limit?: number;
}) {
  const q = new URLSearchParams();
  if (params?.sessionId) q.set('session_id', params.sessionId);
  if (params?.type) q.set('type', params.type);
  if (params?.limit) q.set('limit', String(params.limit));
  const qs = q.toString();
  return fetchJson<{ status: string; events: OpsEvent[] }>(
    `/v1/ops/metrics/history${qs ? `?${qs}` : ''}`,
  );
}

export async function fetchOpsAlerts(sessionId?: string, limit = 50) {
  const q = new URLSearchParams();
  if (sessionId) q.set('session_id', sessionId);
  q.set('limit', String(limit));
  return fetchJson<{ status: string; alerts: OpsAlertEvent[] }>(
    `/v1/ops/alerts?${q.toString()}`,
  );
}

export function connectOpsStream(options: {
  filterSessionId?: string;
  subscribeVqa?: boolean;
  subscribeStt?: boolean;
  onEvent: (ev: OpsEvent) => void;
  onStatus: (msg: string) => void;
}): { close: () => void } {
  const ws = new WebSocket(`${wsBase}/v1/ops/stream`);

  ws.onopen = () => {
    ws.send(JSON.stringify(opsWsHandshake(options)));
    options.onStatus('Ops WebSocket conectado, handshake enviado');
  };

  ws.onmessage = (ev) => {
    try {
      const data = parseWsJson(ev.data as string) as Record<string, unknown>;
      if (data.status === 'ok' && data.mode === 'ops') {
        options.onStatus('Ops handshake OK');
        return;
      }
      if (data.type === 'vqa_window' || data.type === 'stt_final' || data.type === 'stt_partial' || data.type === 'alert') {
        options.onEvent(data as unknown as OpsEvent);
        return;
      }
      if (data.error) {
        options.onStatus(`Erro: ${data.error}`);
      }
    } catch {
      options.onStatus(String(ev.data));
    }
  };

  ws.onerror = () => options.onStatus('Erro ops WebSocket');
  ws.onclose = () => options.onStatus('Ops WebSocket fechado');

  return { close: () => ws.close() };
}
