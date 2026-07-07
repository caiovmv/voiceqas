import { describe, expect, it } from 'vitest';
import { int16ToFloat, resampleInt16 } from './audio';

describe('audio', () => {
  it('converts int16 to normalized float', () => {
    const f = int16ToFloat(new Int16Array([0, 32767, -32768]));
    expect(f[0]).toBe(0);
    expect(f[1]).toBeCloseTo(1, 3);
    expect(f[2]).toBeCloseTo(-1, 3);
  });

  it('resamples identity when rates match', () => {
    const input = new Int16Array([1, 2, 3, 4]);
    const out = resampleInt16(input, 8000, 8000);
    expect(Array.from(out)).toEqual([1, 2, 3, 4]);
  });

  it('downsamples to fewer samples', () => {
    const input = new Int16Array([0, 100, 200, 300]);
    const out = resampleInt16(input, 8000, 4000);
    expect(out.length).toBeLessThan(input.length);
  });
});
