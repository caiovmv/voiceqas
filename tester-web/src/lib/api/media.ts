import { opsAuthHeaders } from '../auth';
import type { MediaSessionMeta, MediaSessionResponse } from '../types';
import { apiBase, fetchJson } from './client';

export async function listMediaSessions() {
  return fetchJson<{ status: string; sessions: MediaSessionMeta[] }>('/v1/media/sessions');
}

export async function createMediaSession(body: {
  session_id: string;
  format: string;
  sample_rate?: number;
  remote_host?: string;
  remote_port?: number;
  inbound_host?: string;
  inbound_port?: number;
}) {
  return fetchJson<MediaSessionResponse>('/v1/media/sessions', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...opsAuthHeaders() },
    body: JSON.stringify(body),
  });
}

export async function deleteMediaSession(sessionId: string) {
  return fetchJson<{ status: string }>(`/v1/media/sessions/${encodeURIComponent(sessionId)}`, {
    method: 'DELETE',
    headers: opsAuthHeaders(),
  });
}

export async function sendAgentPcm(
  sessionId: string,
  pcm: Int16Array,
  sampleRate = 16000,
) {
  const bytes = new Uint8Array(pcm.buffer, pcm.byteOffset, pcm.byteLength);
  const res = await fetch(`${apiBase}/v1/media/sessions/${encodeURIComponent(sessionId)}/agent-audio`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/octet-stream',
      'X-Sample-Rate': String(sampleRate),
      ...opsAuthHeaders(),
    },
    body: bytes,
  });
  if (!res.ok) {
    throw new Error(await res.text());
  }
  return res.json() as Promise<{ status: string; rtp_packets: number; bytes_sent: number }>;
}
