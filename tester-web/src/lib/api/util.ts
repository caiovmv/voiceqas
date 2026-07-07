import { getOpsToken } from '../auth';

export function parseWsJson(raw: string): unknown {
  return JSON.parse(raw.replace(/\0+$/g, ''));
}

export function concatFrames(frames: Uint8Array[]): Uint8Array {
  const total = frames.reduce((n, f) => n + f.length, 0);
  const out = new Uint8Array(total);
  let off = 0;
  for (const f of frames) {
    out.set(f, off);
    off += f.length;
  }
  return out;
}

export function sleep(ms: number) {
  return new Promise((r) => setTimeout(r, ms));
}

export function opsWsHandshake(options: {
  filterSessionId?: string;
  subscribeVqa?: boolean;
  subscribeStt?: boolean;
}): Record<string, unknown> {
  const handshake: Record<string, unknown> = {
    subscribe_vqa: options.subscribeVqa ?? true,
    subscribe_stt: options.subscribeStt ?? true,
  };
  if (options.filterSessionId) {
    handshake.filter_session_id = options.filterSessionId;
  }
  const token = getOpsToken();
  if (token) {
    handshake.token = token;
  }
  return handshake;
}
