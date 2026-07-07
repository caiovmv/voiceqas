import type { AudioFormat, CodecConfig } from './types';
import { COMPARE_PRESETS } from './types';
import { decodeRtpViaBackend, needsBackendCodec, packRtpViaBackend } from './codec-client';
import { decodeG722, encodeG722 } from './g722';
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

export function prepareAudioForStt(
  pcm: Int16Array,
  pcmRate: number,
  config: CodecConfig,
): Int16Array {
  let samples = preparePcmForConfig(pcm, pcmRate, config);
  if (config.simulateClipping) {
    samples = applyClipping(samples, config.clippingGain);
  }
  return samples;
}

export function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

async function blobToBytes(blob: Blob): Promise<Uint8Array> {
  return new Uint8Array(await blob.arrayBuffer());
}

/** PCM após encode/decode do codec escolhido (round-trip SIP). */
export async function buildCodecRoundTripPcm(
  pcm: Int16Array,
  pcmRate: number,
  config: CodecConfig,
): Promise<{ pcm: Int16Array; sampleRate: number }> {
  const encoded = await encodeForTransport(pcm, pcmRate, config);
  const decoded = await decodeFromEncoded(encoded);
  return { pcm: decoded, sampleRate: encoded.sampleRate };
}

/** PCM 16 kHz enviado ao pipeline STT (decode + resample). */
export async function buildSttEnhancedPcm(
  pcm: Int16Array,
  pcmRate: number,
  config: CodecConfig,
  sttTargetRate = 16000,
): Promise<Int16Array> {
  const { pcm: roundTrip, sampleRate } = await buildCodecRoundTripPcm(pcm, pcmRate, config);
  if (sampleRate === sttTargetRate) {
    return roundTrip;
  }
  return resampleInt16(roundTrip, sampleRate, sttTargetRate);
}

export interface ComparisonPackFile {
  name: string;
  blob: Blob;
}

export async function buildComparisonPack(
  pcm: Int16Array,
  pcmRate: number,
  config: CodecConfig,
): Promise<ComparisonPackFile[]> {
  const qualityId =
    config.format === 'rtp_g722'
      ? 'g722'
      : config.format === 'rtp_pcmu'
        ? 'g711'
        : 'pcm16';
  const [roundTrip, sttPcm] = await Promise.all([
    buildCodecRoundTripPcm(pcm, pcmRate, config),
    buildSttEnhancedPcm(pcm, pcmRate, config),
  ]);
  return [
    { name: '01-original.wav', blob: createWavBlob(pcm, pcmRate) },
    {
      name: `02-codec-${qualityId}-roundtrip.wav`,
      blob: createWavBlob(roundTrip.pcm, roundTrip.sampleRate),
    },
    { name: '03-stt-enhanced-16khz.wav', blob: createWavBlob(sttPcm, 16000) },
  ];
}

export async function downloadComparisonPackZip(
  pcm: Int16Array,
  pcmRate: number,
  config: CodecConfig,
) {
  const { zipSync } = await import('fflate');
  const files = await buildComparisonPack(pcm, pcmRate, config);
  const zipEntries: Record<string, Uint8Array> = {};
  for (const file of files) {
    zipEntries[file.name] = await blobToBytes(file.blob);
  }
  const zipped = zipSync(zipEntries);
  const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
  downloadBlob(new Blob([zipped], { type: 'application/zip' }), `voiceqas-audio-pack-${stamp}.zip`);
}

export async function encodeForTransport(
  pcm: Int16Array,
  pcmRate: number,
  config: CodecConfig,
): Promise<EncodedPayload> {
  const samples = prepareAudioForStt(pcm, pcmRate, config);

  const frameSamples = Math.floor(config.sampleRate * (config.frameMs / 1000));
  const frames: Uint8Array[] = [];

  if (config.format === 'pcm_s16le_8k' || config.format === 'pcm_s16le_16k') {
    for (let i = 0; i < samples.length; i += frameSamples) {
      const chunk = samples.subarray(i, i + frameSamples);
      frames.push(int16ToBytes(chunk));
    }
    return { frames, format: config.format, sampleRate: config.sampleRate, frameMs: config.frameMs };
  }

  if (config.format === 'rtp_g722') {
    let g722Input = samples;
    if (g722Input.length % 2 !== 0) {
      const padded = new Int16Array(g722Input.length + 1);
      padded.set(g722Input);
      g722Input = padded;
    }
    const g722 = encodeG722(g722Input);
    const frameSamples = Math.floor(config.sampleRate * (config.frameMs / 1000));
    const bytesPerFrame = frameSamples / 2;
    let seq = 1;
    let ts = 0;
    for (let i = 0; i < g722.length; i += bytesPerFrame) {
      const payload = g722.subarray(i, i + bytesPerFrame);
      frames.push(buildRtpPacket(seq++, ts, payload, 9));
      ts += payload.length;
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

  if (encoded.format === 'rtp_g722') {
    const total = encoded.frames.reduce((n, f) => n + extractRtpPayload(f).length, 0);
    const g722 = new Uint8Array(total);
    let off = 0;
    for (const frame of encoded.frames) {
      const payload = extractRtpPayload(frame);
      g722.set(payload, off);
      off += payload.length;
    }
    return decodeG722(g722);
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
      sttProvider: 'cpu',
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
