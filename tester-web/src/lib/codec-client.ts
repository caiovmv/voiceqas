import type { AudioFormat } from './types';

const API_BASE = import.meta.env.VITE_API_BASE ?? '/api';

function pcmToBytes(pcm: Int16Array): number[] {
  const out: number[] = [];
  for (let i = 0; i < pcm.length; i++) {
    out.push(pcm[i] & 0xff);
    out.push((pcm[i] >> 8) & 0xff);
  }
  return out;
}

function bytesToInt16(bytes: number[]): Int16Array {
  const out = new Int16Array(bytes.length / 2);
  for (let i = 0; i < out.length; i++) {
    out[i] = (bytes[i * 2 + 1] << 8) | (bytes[i * 2] & 0xff);
  }
  return out;
}

export async function packRtpViaBackend(
  format: AudioFormat,
  pcm: Int16Array,
  frameMs: number,
): Promise<Uint8Array[]> {
  const res = await fetch(`${API_BASE}/v1/tools/pack-rtp`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      format,
      frame_ms: frameMs,
      pcm_bytes: pcmToBytes(pcm),
    }),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error((err as { error?: string }).error ?? `pack-rtp ${res.status}`);
  }
  const data = (await res.json()) as { frames: number[][] };
  return data.frames.map((f) => Uint8Array.from(f));
}

export async function decodeRtpViaBackend(
  format: AudioFormat,
  frames: Uint8Array[],
): Promise<Int16Array> {
  const res = await fetch(`${API_BASE}/v1/tools/decode-rtp`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      format,
      frames: frames.map((f) => Array.from(f)),
    }),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error((err as { error?: string }).error ?? `decode-rtp ${res.status}`);
  }
  const data = (await res.json()) as { pcm_bytes: number[] };
  return bytesToInt16(data.pcm_bytes);
}

export function needsBackendCodec(format: AudioFormat): boolean {
  return format === 'rtp_g722' || format === 'rtp_g729';
}
