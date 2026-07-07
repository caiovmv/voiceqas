import { describe, expect, it } from 'vitest';
import { decodeG722, encodeG722 } from './g722';

describe('g722', () => {
  it('encodes and decodes without throwing', () => {
    const samples = new Int16Array(160);
    for (let i = 0; i < samples.length; i++) {
      samples[i] = Math.round(2000 * Math.sin((2 * Math.PI * 440 * i) / 16000));
    }
    const encoded = encodeG722(samples);
    expect(encoded.length).toBeGreaterThan(0);
    const decoded = decodeG722(encoded);
    expect(decoded.length).toBeGreaterThan(0);
  });

  it('returns empty output for empty input', () => {
    expect(encodeG722(new Int16Array(0)).length).toBe(0);
    expect(decodeG722(new Uint8Array(0)).length).toBe(0);
  });
});
