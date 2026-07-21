/** Lightweight biquad / dynamics preview mirroring server channel strip (no RNNoise). */

export interface BiquadCoeffs {
  b0: number;
  b1: number;
  b2: number;
  a1: number;
  a2: number;
}

export class BiquadFilter {
  private z1 = 0;
  private z2 = 0;
  constructor(private c: BiquadCoeffs) {}

  reset() {
    this.z1 = 0;
    this.z2 = 0;
  }

  process(x: number): number {
    const y = this.c.b0 * x + this.z1;
    this.z1 = this.c.b1 * x - this.c.a1 * y + this.z2;
    this.z2 = this.c.b2 * x - this.c.a2 * y;
    return y;
  }
}

function clampFreq(sampleRate: number, freq: number) {
  return Math.min(Math.max(freq, 1), sampleRate * 0.5 - 1);
}

export function highpassCoeffs(sampleRate: number, cutoffHz: number, q = 0.7071): BiquadCoeffs {
  const f0 = clampFreq(sampleRate, cutoffHz);
  const w0 = (2 * Math.PI * f0) / sampleRate;
  const cos = Math.cos(w0);
  const sin = Math.sin(w0);
  const alpha = sin / (2 * q);
  const b0 = (1 + cos) / 2;
  const b1 = -(1 + cos);
  const b2 = (1 + cos) / 2;
  const a0 = 1 + alpha;
  const a1 = -2 * cos;
  const a2 = 1 - alpha;
  return { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: a1 / a0, a2: a2 / a0 };
}

export function peakingCoeffs(
  sampleRate: number,
  freqHz: number,
  gainDb: number,
  q: number,
): BiquadCoeffs {
  const f0 = clampFreq(sampleRate, freqHz);
  const A = 10 ** (gainDb / 40);
  const w0 = (2 * Math.PI * f0) / sampleRate;
  const cos = Math.cos(w0);
  const sin = Math.sin(w0);
  const alpha = sin / (2 * Math.max(0.05, q));
  const b0 = 1 + alpha * A;
  const b1 = -2 * cos;
  const b2 = 1 - alpha * A;
  const a0 = 1 + alpha / A;
  const a1 = -2 * cos;
  const a2 = 1 - alpha / A;
  return { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: a1 / a0, a2: a2 / a0 };
}

/** Magnitude response (dB) for cascaded peaking + optional HPF at `freqHz`. */
export function eqResponseDb(
  sampleRate: number,
  freqHz: number,
  hpfCutoff: number | null,
  bands: { freq_hz: number; gain_db: number; q: number }[],
): number {
  let mag = 1;
  const stages: BiquadCoeffs[] = [];
  if (hpfCutoff != null) stages.push(highpassCoeffs(sampleRate, hpfCutoff));
  for (const b of bands) stages.push(peakingCoeffs(sampleRate, b.freq_hz, b.gain_db, b.q));
  const w = (2 * Math.PI * freqHz) / sampleRate;
  const cos = Math.cos(w);
  const sin = Math.sin(w);
  for (const c of stages) {
    const numRe = c.b0 + c.b1 * cos + c.b2 * Math.cos(2 * w);
    const numIm = -(c.b1 * sin + c.b2 * Math.sin(2 * w));
    const denRe = 1 + c.a1 * cos + c.a2 * Math.cos(2 * w);
    const denIm = -(c.a1 * sin + c.a2 * Math.sin(2 * w));
    const num = Math.hypot(numRe, numIm);
    const den = Math.hypot(denRe, denIm) || 1e-12;
    mag *= num / den;
  }
  return 20 * Math.log10(Math.max(mag, 1e-12));
}

export function applyStripPreview(
  pcm: Int16Array,
  sampleRate: number,
  strip: import('./pipeline-mix').DspStripState,
): Int16Array {
  if (pcm.length === 0) return new Int16Array(0);
  let f = new Float32Array(pcm.length);
  for (let i = 0; i < pcm.length; i++) f[i] = pcm[i] / 32768;

  if (strip.hpf.enabled) {
    const hpf = new BiquadFilter(highpassCoeffs(sampleRate, strip.hpf.cutoff_hz));
    for (let i = 0; i < f.length; i++) f[i] = hpf.process(f[i]);
  }
  if (strip.eq.enabled) {
    for (const band of strip.eq.bands) {
      const bq = new BiquadFilter(peakingCoeffs(sampleRate, band.freq_hz, band.gain_db, band.q));
      for (let i = 0; i < f.length; i++) f[i] = bq.process(f[i]);
    }
  }

  if (strip.compressor.enabled) {
    const thr = 10 ** (strip.compressor.threshold_db / 20);
    const ratio = Math.max(1, strip.compressor.ratio);
    const makeup = 10 ** (strip.compressor.makeup_db / 20);
    let env = 0;
    const atk = 1 - Math.exp(-1 / ((strip.compressor.attack_ms / 1000) * sampleRate));
    const rel = 1 - Math.exp(-1 / ((strip.compressor.release_ms / 1000) * sampleRate));
    for (let i = 0; i < f.length; i++) {
      const level = Math.abs(f[i]);
      env += (level - env) * (level > env ? atk : rel);
      let gr = 1;
      if (env > thr) {
        const envDb = 20 * Math.log10(env);
        const grDb = (envDb - strip.compressor.threshold_db) * (1 - 1 / ratio);
        gr = 10 ** (-grDb / 20);
      }
      f[i] *= gr * makeup;
    }
  }

  if (strip.limiter.enabled) {
    const ceil = 10 ** (strip.limiter.ceiling_dbfs / 20);
    for (let i = 0; i < f.length; i++) {
      f[i] = Math.max(-ceil, Math.min(ceil, f[i]));
    }
  }

  if (strip.agc.enabled) {
    let sum = 0;
    for (let i = 0; i < f.length; i++) sum += f[i] * f[i];
    const rms = Math.sqrt(sum / f.length);
    const target = 10 ** (strip.agc.target_rms_dbfs / 20);
    if (rms > 1e-8) {
      const maxG = 10 ** (strip.agc.max_gain_db / 20);
      const g = Math.min(maxG, Math.max(1 / maxG, target / rms));
      for (let i = 0; i < f.length; i++) f[i] *= g;
    }
  }

  const out = new Int16Array(f.length);
  for (let i = 0; i < f.length; i++) {
    const v = Math.round(f[i] * 32768);
    out[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
  }
  return out;
}
