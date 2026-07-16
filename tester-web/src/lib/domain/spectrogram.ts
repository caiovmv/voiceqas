/** Lightweight magnitude spectrogram from PCM16 (STFT). */

export function pcmToSpectrogram(
  pcm: Int16Array,
  sampleRate: number,
  fftSize = 512,
  hop = 256,
  /** Cap columns so long calls (e.g. 4+ min) stay interactive in the Lab. */
  maxCols = 640,
): { rows: number; cols: number; data: Float32Array; sampleRate: number } {
  const window = hann(fftSize);
  let step = hop;
  const rawCols = Math.max(1, Math.floor((pcm.length - fftSize) / step) + 1);
  if (rawCols > maxCols && maxCols > 1) {
    step = Math.max(hop, Math.ceil((pcm.length - fftSize) / (maxCols - 1)));
  }
  const cols = Math.max(1, Math.floor((pcm.length - fftSize) / step) + 1);
  const rows = fftSize / 2;
  const data = new Float32Array(rows * cols);
  const re = new Float32Array(fftSize);
  const im = new Float32Array(fftSize);

  for (let c = 0; c < cols; c++) {
    const offset = c * step;
    for (let i = 0; i < fftSize; i++) {
      const s = offset + i < pcm.length ? pcm[offset + i] / 32768 : 0;
      re[i] = s * window[i];
      im[i] = 0;
    }
    fftRadix2(re, im);
    for (let r = 0; r < rows; r++) {
      const mag = Math.sqrt(re[r] * re[r] + im[r] * im[r]);
      data[r * cols + c] = 20 * Math.log10(mag + 1e-8);
    }
  }
  return { rows, cols, data, sampleRate };
}

function hann(n: number): Float32Array {
  const w = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    w[i] = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (n - 1)));
  }
  return w;
}

function fftRadix2(re: Float32Array, im: Float32Array): void {
  const n = re.length;
  let j = 0;
  for (let i = 0; i < n; i++) {
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
    let m = n >> 1;
    while (m >= 1 && j >= m) {
      j -= m;
      m >>= 1;
    }
    j += m;
  }
  for (let size = 2; size <= n; size <<= 1) {
    const half = size >> 1;
    const tableStep = (2 * Math.PI) / size;
    for (let i = 0; i < n; i += size) {
      for (let k = 0; k < half; k++) {
        const angle = tableStep * k;
        const wr = Math.cos(angle);
        const wi = -Math.sin(angle);
        const tr = wr * re[i + k + half] - wi * im[i + k + half];
        const ti = wr * im[i + k + half] + wi * re[i + k + half];
        re[i + k + half] = re[i + k] - tr;
        im[i + k + half] = im[i + k] - ti;
        re[i + k] += tr;
        im[i + k] += ti;
      }
    }
  }
}
