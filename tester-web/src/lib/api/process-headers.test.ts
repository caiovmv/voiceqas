import { describe, expect, it } from 'vitest';
import { headersFromMix } from './process-headers';
import { DEFAULT_MIX, LEGACY_MIX } from '../domain/pipeline-mix';

describe('headersFromMix', () => {
  it('turns NR/spectral strip off for legacy mix', () => {
    const h = headersFromMix(LEGACY_MIX);
    expect(h['X-Audio-Enhancement']).toBe('0');
    expect(h['X-STT-Diarization']).toBe('0');
    expect(h['X-STT-Focus-Primary']).toBe('0');
    expect(h['X-Audio-AGC']).toBe('1');
    const strip = JSON.parse(h['X-Audio-Strip']);
    expect(strip.hpf.enabled).toBe(false);
    expect(strip.eq.enabled).toBe(false);
    expect(strip.agc.target_rms_dbfs).toBe(-20);
  });

  it('maps default strip plan (NR + HPF + EQ on)', () => {
    const h = headersFromMix(DEFAULT_MIX);
    expect(h['X-Audio-Enhancement']).toBe('0');
    expect(h['X-STT-Diarization']).toBe('1');
    expect(h['X-STT-Focus-Primary']).toBe('0');
    expect(h['X-Audio-AGC']).toBe('1');
    const strip = JSON.parse(h['X-Audio-Strip']);
    expect(strip.hpf.cutoff_hz).toBe(80);
    expect(strip.eq.bands).toHaveLength(4);
    expect(strip.limiter.ceiling_dbfs).toBe(-1);
    expect(strip.agc.target_rms_dbfs).toBe(-18);
  });

  it('disables focus when silero off', () => {
    const h = headersFromMix({ ...DEFAULT_MIX, silero_vad_enabled: false, focus_primary: true });
    expect(h['X-STT-Diarization']).toBe('0');
    expect(h['X-STT-Focus-Primary']).toBe('0');
  });
});
