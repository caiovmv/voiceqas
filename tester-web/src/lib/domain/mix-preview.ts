import type { DiarizationTurn } from '../types';
import { applyStripPreview } from './dsp-preview';
import type { PipelineMixState } from './pipeline-mix';

/** Concatenate PCM crops for each turn (16-bit mono). */
export function concatTurnsPcm(
  pcm: Int16Array,
  sampleRate: number,
  turns: DiarizationTurn[],
): Int16Array {
  if (!turns.length) return pcm;
  const chunks: Int16Array[] = [];
  let total = 0;
  for (const t of turns) {
    const start = Math.max(0, Math.floor((t.start_ms / 1000) * sampleRate));
    const end = Math.min(pcm.length, Math.ceil((t.end_ms / 1000) * sampleRate));
    if (end <= start) continue;
    const slice = pcm.subarray(start, end);
    chunks.push(slice);
    total += slice.length;
  }
  if (total === 0) return pcm;
  const out = new Int16Array(total);
  let off = 0;
  for (const c of chunks) {
    out.set(c, off);
    off += c.length;
  }
  return out;
}

/**
 * Keep full timeline length; zero samples outside speech turns so spectrogram
 * clearly differs from the original (black gaps = silence).
 */
export function maskNonSpeechPcm(
  pcm: Int16Array,
  sampleRate: number,
  turns: DiarizationTurn[],
): Int16Array {
  if (!turns.length) return new Int16Array(pcm);
  const out = new Int16Array(pcm.length);
  for (const t of turns) {
    const start = Math.max(0, Math.floor((t.start_ms / 1000) * sampleRate));
    const end = Math.min(pcm.length, Math.ceil((t.end_ms / 1000) * sampleRate));
    if (end <= start) continue;
    out.set(pcm.subarray(start, end), start);
  }
  return out;
}

/**
 * Client-side energy gate when Silero turns are not available yet.
 * Digs silence holes so mix spectrum is never a clone of the original.
 */
export function approxEnergyMaskPcm(
  pcm: Int16Array,
  sampleRate: number,
  frameMs = 20,
): Int16Array {
  if (pcm.length === 0) return new Int16Array(0);
  const frame = Math.max(1, Math.floor((sampleRate * frameMs) / 1000));
  const rmses: number[] = [];
  for (let i = 0; i + frame <= pcm.length; i += frame) {
    let s = 0;
    for (let j = 0; j < frame; j++) {
      const v = pcm[i + j];
      s += v * v;
    }
    rmses.push(Math.sqrt(s / frame));
  }
  if (rmses.length === 0) return new Int16Array(pcm);
  const sorted = [...rmses].sort((a, b) => a - b);
  const floor = sorted[Math.floor(sorted.length * 0.2)] || 1;
  const thr = Math.max(floor * 3.5, 120);
  const out = new Int16Array(pcm.length);
  let fi = 0;
  for (let i = 0; i + frame <= pcm.length; i += frame) {
    if (rmses[fi++] >= thr) {
      out.set(pcm.subarray(i, i + frame), i);
    }
  }
  const rem = pcm.length % frame;
  if (rem > 0) {
    const start = pcm.length - rem;
    let s = 0;
    for (let j = start; j < pcm.length; j++) s += pcm[j] * pcm[j];
    if (Math.sqrt(s / rem) >= thr) {
      out.set(pcm.subarray(start), start);
    }
  }
  return out;
}

/** Simple RMS AGC so 'depois' spectrum/listen reflects level normalization. */
export function applyRmsAgc(pcm: Int16Array, targetRms = 2500): Int16Array {
  if (pcm.length === 0) return new Int16Array(0);
  let sum = 0;
  for (let i = 0; i < pcm.length; i++) sum += pcm[i] * pcm[i];
  const rms = Math.sqrt(sum / pcm.length);
  if (rms < 1) return new Int16Array(pcm);
  const gain = Math.min(8, targetRms / rms);
  const out = new Int16Array(pcm.length);
  for (let i = 0; i < pcm.length; i++) {
    const v = Math.round(pcm[i] * gain);
    out[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
  }
  return out;
}

export interface MixPreviewResult {
  pcm: Int16Array;
  sampleRate: number;
  /** How non-speech was treated for this preview. */
  mode: 'turn-mask' | 'energy-mask' | 'agc-only' | 'passthrough';
}

/**
 * Browser preview of the Lab "depois" path.
 * Prefer turn/energy masking (keep timeline, zero gaps) so spectrograms diverge
 * from the original. Channel strip (HPF/EQ/comp/lim/AGC) is approximated here;
 * RNNoise remains server-only (Processar is source of truth).
 */
export function buildMixPreviewPcm(
  pcm: Int16Array,
  sampleRate: number,
  mix: PipelineMixState,
  turns: DiarizationTurn[],
): MixPreviewResult {
  let out: Int16Array;
  let mode: MixPreviewResult['mode'] = 'passthrough';

  if (mix.focus_primary && turns.length > 0) {
    const primary = turns.filter((t) => t.is_primary);
    out = maskNonSpeechPcm(pcm, sampleRate, primary.length > 0 ? primary : turns);
    mode = 'turn-mask';
  } else if (mix.silero_vad_enabled && turns.length > 0) {
    out = maskNonSpeechPcm(pcm, sampleRate, turns);
    mode = 'turn-mask';
  } else if (mix.silero_vad_enabled) {
    out = approxEnergyMaskPcm(pcm, sampleRate);
    mode = 'energy-mask';
  } else {
    out = new Int16Array(pcm);
    mode = 'passthrough';
  }

  out = applyStripPreview(out, sampleRate, mix.strip);
  if (mode === 'passthrough' && mix.strip.agc.enabled) mode = 'agc-only';

  return { pcm: out, sampleRate, mode };
}
