import type { AudioFormat, CodecConfig } from './types';
import { COMPARE_PRESETS } from './types';
import { decodeRtpViaBackend, needsBackendCodec, packRtpViaBackend } from './codec-client';
import { decodePcma, decodePcmu, encodePcma, encodePcmu } from './g711';
import { buildRtpPacket, extractRtpPayload } from './rtp';

export interface EncodedPayload {
  frames: Uint8Array[];
  format: AudioFormat;
  sampleRate: number;
  frameMs: number;
}

export interface EncodedVariant {
  id: string;
  label: string;
  config: CodecConfig;
  encoded: EncodedPayload;
  previewUrl: string;
}

export function int16ToFloat(samples: Int16Array): Float32Array {
  const out = new Float32Array(samples.length);
  for (let i = 0; i < samples.length; i++) {
    out[i] = samples[i] / (samples[i] < 0 ? 0x8000 : 0x7fff);
  }
  return out;
}

export function resampleInt16(input: Int16Array, fromRate: number, toRate: number): Int16Array {
  if (fromRate === toRate) return input;
  const floats = int16ToFloat(input);
  const resampled = resampleLinear(floats, fromRate, toRate);
  return floatToInt16(resampled);
}

export function preparePcmForConfig(
  pcm: Int16Array,
  pcmRate: number,
  config: CodecConfig,
): Int16Array {
  return resampleInt16(pcm, pcmRate, config.sampleRate);
}

export function resampleLinear(input: Float32Array, fromRate: number, toRate: number): Float32Array {
  if (fromRate === toRate) return input;
  const ratio = fromRate / toRate;
  const outLen = Math.floor(input.length / ratio);
  const out = new Float32Array(outLen);
  for (let i = 0; i < outLen; i++) {
    const src = i * ratio;
    const idx = Math.floor(src);
    const frac = src - idx;
    const a = input[idx] ?? 0;
    const b = input[idx + 1] ?? a;
    out[i] = a + (b - a) * frac;
  }
  return out;
}

export function floatToInt16(samples: Float32Array): Int16Array {
  const out = new Int16Array(samples.length);
  for (let i = 0; i < samples.length; i++) {
    const s = Math.max(-1, Math.min(1, samples[i]));
    out[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
  }
  return out;
}

export function applyClipping(samples: Int16Array, gain: number): Int16Array {
  const out = new Int16Array(samples.length);
  for (let i = 0; i < samples.length; i++) {
    let v = Math.round(samples[i] * gain);
    if (v > 32000) v = 32000;
    if (v < -32000) v = -32000;
    out[i] = v;
  }
  return out;
}

export function int16ToBytes(samples: Int16Array): Uint8Array {
  const bytes = new Uint8Array(samples.length * 2);
  const view = new DataView(bytes.buffer);
  for (let i = 0; i < samples.length; i++) {
    view.setInt16(i * 2, samples[i], true);
  }
  return bytes;
}

export async function encodeForTransport(
  pcm: Int16Array,
  pcmRate: number,
  config: CodecConfig,
): Promise<EncodedPayload> {
  let samples = preparePcmForConfig(pcm, pcmRate, config);
  if (config.simulateClipping) {
    samples = applyClipping(samples, config.clippingGain);
  }

  const frameSamples = Math.floor(config.sampleRate * (config.frameMs / 1000));
  const frames: Uint8Array[] = [];

  if (config.format === 'pcm_s16le_8k' || config.format === 'pcm_s16le_16k') {
    for (let i = 0; i < samples.length; i += frameSamples) {
      const chunk = samples.subarray(i, i + frameSamples);
      frames.push(int16ToBytes(chunk));
    }
    return { frames, format: config.format, sampleRate: config.sampleRate, frameMs: config.frameMs };
  }

  if (needsBackendCodec(config.format)) {
    const rtpFrames = await packRtpViaBackend(config.format, samples, config.frameMs);
    return {
      frames: rtpFrames,
      format: config.format,
      sampleRate: config.sampleRate,
      frameMs: config.frameMs,
    };
  }

  const g711 = config.format === 'rtp_pcmu' ? encodePcmu(samples) : encodePcma(samples);
  const pt = config.format === 'rtp_pcmu' ? 0 : 8;
  let seq = 1;
  let ts = 0;
  for (let i = 0; i < g711.length; i += frameSamples) {
    const payload = g711.subarray(i, i + frameSamples);
    frames.push(buildRtpPacket(seq++, ts, payload, pt));
    ts += frameSamples;
  }

  return { frames, format: config.format, sampleRate: config.sampleRate, frameMs: config.frameMs };
}

export function shouldDropFrame(_index: number, lossPct: number): boolean {
  if (lossPct <= 0) return false;
  return Math.random() * 100 < lossPct;
}

export function createWavBlob(samples: Int16Array, sampleRate: number): Blob {
  const dataSize = samples.length * 2;
  const buffer = new ArrayBuffer(44 + dataSize);
  const view = new DataView(buffer);
  const writeStr = (off: number, s: string) => {
    for (let i = 0; i < s.length; i++) view.setUint8(off + i, s.charCodeAt(i));
  };
  writeStr(0, 'RIFF');
  view.setUint32(4, 36 + dataSize, true);
  writeStr(8, 'WAVE');
  writeStr(12, 'fmt ');
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, 1, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * 2, true);
  view.setUint16(32, 2, true);
  view.setUint16(34, 16, true);
  writeStr(36, 'data');
  view.setUint32(40, dataSize, true);
  for (let i = 0; i < samples.length; i++) {
    view.setInt16(44 + i * 2, samples[i], true);
  }
  return new Blob([buffer], { type: 'audio/wav' });
}

function bytesToInt16(bytes: Uint8Array): Int16Array {
  const out = new Int16Array(bytes.length / 2);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let i = 0; i < out.length; i++) {
    out[i] = view.getInt16(i * 2, true);
  }
  return out;
}

/** Decodifica o payload enviado de volta para PCM (round-trip para preview). */
export async function decodeFromEncoded(encoded: EncodedPayload): Promise<Int16Array> {
  if (encoded.format === 'pcm_s16le_8k' || encoded.format === 'pcm_s16le_16k') {
    const total = encoded.frames.reduce((n, f) => n + f.length, 0);
    const out = new Uint8Array(total);
    let off = 0;
    for (const f of encoded.frames) {
      out.set(f, off);
      off += f.length;
    }
    return bytesToInt16(out);
  }

  if (needsBackendCodec(encoded.format)) {
    return decodeRtpViaBackend(encoded.format, encoded.frames);
  }

  const chunks: Int16Array[] = [];
  for (const frame of encoded.frames) {
    const payload = extractRtpPayload(frame);
    const decoded =
      encoded.format === 'rtp_pcmu' ? decodePcmu(payload) : decodePcma(payload);
    chunks.push(decoded);
  }
  const total = chunks.reduce((n, c) => n + c.length, 0);
  const out = new Int16Array(total);
  let off = 0;
  for (const c of chunks) {
    out.set(c, off);
    off += c.length;
  }
  return out;
}

export async function createPreviewUrl(encoded: EncodedPayload): Promise<string> {
  const pcm = await decodeFromEncoded(encoded);
  return URL.createObjectURL(createWavBlob(pcm, encoded.sampleRate));
}

export async function buildVariants(
  pcm: Int16Array,
  pcmRate: number,
  presetIds: string[],
  sessionBase: string,
): Promise<EncodedVariant[]> {
  const presets = COMPARE_PRESETS.filter((p) => presetIds.includes(p.id));
  const variants: EncodedVariant[] = [];
  for (const preset of presets) {
    const config: CodecConfig = {
      ...preset.config,
      sessionId: `${sessionBase}-${preset.id}`,
      sttModel: 'auto',
    };
    const encoded = await encodeForTransport(pcm, pcmRate, config);
    variants.push({
      id: preset.id,
      label: preset.label,
      config,
      encoded,
      previewUrl: await createPreviewUrl(encoded),
    });
  }
  return variants;
}

export function revokeVariantUrls(variants: EncodedVariant[]) {
  for (const v of variants) URL.revokeObjectURL(v.previewUrl);
}
