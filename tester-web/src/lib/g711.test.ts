import { describe, expect, it } from 'vitest';
import { encodePcmu, decodePcmu, encodePcma, decodePcma } from './g711';

describe('g711', () => {
  it('round-trips mu-law samples', () => {
    const samples = new Int16Array([0, 1000, -1000, 32000, -32000]);
    const encoded = encodePcmu(samples);
    const decoded = decodePcmu(encoded);
    expect(decoded.length).toBe(samples.length);
    for (let i = 0; i < samples.length; i++) {
      expect(Math.abs(decoded[i] - samples[i])).toBeLessThan(800);
    }
  });

  it('round-trips a-law samples', () => {
    const samples = new Int16Array([0, 500, -500]);
    const encoded = encodePcma(samples);
    const decoded = decodePcma(encoded);
    expect(decoded.length).toBe(samples.length);
  });

  it('encodes silence to mu-law bytes', () => {
    const encoded = encodePcmu(new Int16Array(4));
    expect(encoded.every((b) => b === 0xFF || b === 0x7F || b === 0xFE)).toBe(true);
  });
});
