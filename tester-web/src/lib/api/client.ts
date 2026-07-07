import { opsAuthHeaders, formatOpsAuthError } from '../auth';

export const apiBase = import.meta.env.VITE_API_BASE ?? '/api';

export async function fetchJson<T>(path: string, init?: RequestInit): Promise<T> {
  const headers = { ...opsAuthHeaders(), ...(init?.headers as Record<string, string> | undefined) };
  const res = await fetch(`${apiBase}${path}`, { ...init, headers });
  const text = await res.text();
  let data: unknown;
  try {
    data = JSON.parse(text);
  } catch {
    data = { raw: text };
  }
  if (!res.ok) {
    const err = data as { error?: string };
    throw new Error(formatOpsAuthError(err.error ?? res.statusText ?? text));
  }
  return data as T;
}

export function resolveWsBase(): string {
  const configured = import.meta.env.VITE_WS_BASE as string | undefined;
  if (configured) {
    return configured.replace(/\/$/, '');
  }
  const { protocol, hostname, port, host } = window.location;
  const wsProto = protocol === 'https:' ? 'wss' : 'ws';
  if (port === '3000' || port === '') {
    return `${wsProto}://${host}/ws`;
  }
  if (port === '9080') {
    return `${wsProto}://${hostname}:9081`;
  }
  return `${wsProto}://${host}/ws`;
}
