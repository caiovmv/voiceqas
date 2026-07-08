import { getOpsToken } from '../auth';

export function parseWsJson(raw: string): unknown {
  return JSON.parse(raw.replace(/\0+$/g, ''));
}

/** Normaliza payload WS (string, Blob ou ArrayBuffer) antes do JSON.parse. */
export async function parseWsEventData(data: unknown): Promise<unknown> {
  if (typeof data === 'string') {
    return parseWsJson(data);
  }
  if (
    data &&
    typeof data === 'object' &&
    'text' in data &&
    typeof (data as { text: () => Promise<string> }).text === 'function'
  ) {
    return parseWsJson(await (data as { text: () => Promise<string> }).text());
  }
  if (data instanceof ArrayBuffer) {
    return parseWsJson(new TextDecoder().decode(data));
  }
  if (ArrayBuffer.isView(data)) {
    const view = data as ArrayBufferView;
    const bytes = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
    return parseWsJson(new TextDecoder().decode(bytes));
  }
  return parseWsJson(String(data));
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
  subscribePipeline?: boolean;
}): Record<string, unknown> {
  const handshake: Record<string, unknown> = {
    subscribe_vqa: options.subscribeVqa ?? true,
    subscribe_stt: options.subscribeStt ?? true,
    subscribe_pipeline: options.subscribePipeline ?? true,
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
