import { describe, expect, it } from 'vitest';
import {
  approxEnergyMaskPcm,
  applyRmsAgc,
  buildMixPreviewPcm,
  concatTurnsPcm,
  maskNonSpeechPcm,
} from './mix-preview';
import { DEFAULT_MIX } from './pipeline-mix';

describe('mix-preview', () => {
  it('concatenates turn crops', () => {
    const pcm = new Int16Array(16000);
    for (let i = 0; i < pcm.length; i++) pcm[i] = i;
    const out = concatTurnsPcm(pcm, 16000, [
      { start_ms: 0, end_ms: 100, speaker_id: 0, is_primary: true, text: 'a' },
      { start_ms: 500, end_ms: 600, speaker_id: 1, is_primary: false, text: 'b' },
    ]);
    expect(out.length).toBe(3200);
    expect(out[0]).toBe(0);
    expect(out[1600]).toBe(8000);
  });

  it('masks non-speech while keeping timeline length', () => {
    const pcm = new Int16Array(16000);
    pcm.fill(1000);
    const out = maskNonSpeechPcm(pcm, 16000, [
      { start_ms: 0, end_ms: 100, speaker_id: 0, is_primary: true, text: 'a' },
    ]);
    expect(out.length).toBe(16000);
    expect(out[0]).toBe(1000);
    expect(out[2000]).toBe(0);
  });

  it('energy mask zeros quiet frames', () => {
    const pcm = new Int16Array(16000);
    for (let i = 0; i < 4000; i++) pcm[i] = 4000;
    // mid silence already 0
    for (let i = 8000; i < 12000; i++) pcm[i] = 4000;
    const out = approxEnergyMaskPcm(pcm, 16000);
    expect(out.length).toBe(16000);
    expect(Math.abs(out[100])).toBeGreaterThan(0);
    expect(out[5000]).toBe(0);
  });

  it('applies AGC gain into a new buffer', () => {
    const pcm = new Int16Array(1000);
    pcm.fill(100);
    const out = applyRmsAgc(pcm, 2500);
    expect(out).not.toBe(pcm);
    expect(Math.abs(out[0])).toBeGreaterThan(100);
  });

  it('buildMixPreview with Silero turns uses turn-mask mode', () => {
    const pcm = new Int16Array(16000);
    pcm.fill(2000);
    const stripOff = {
      ...DEFAULT_MIX.strip,
      nr: { ...DEFAULT_MIX.strip.nr, enabled: false },
      hpf: { ...DEFAULT_MIX.strip.hpf, enabled: false },
      eq: { ...DEFAULT_MIX.strip.eq, enabled: false },
      deesser: { ...DEFAULT_MIX.strip.deesser, enabled: false },
      compressor: { ...DEFAULT_MIX.strip.compressor, enabled: false },
      limiter: { ...DEFAULT_MIX.strip.limiter, enabled: false },
      agc: { ...DEFAULT_MIX.strip.agc, enabled: false },
    };
    const out = buildMixPreviewPcm(
      pcm,
      16000,
      {
        ...DEFAULT_MIX,
        silero_vad_enabled: true,
        agc_enabled: false,
        strip: stripOff,
      },
      [{ start_ms: 0, end_ms: 100, speaker_id: 0, is_primary: true, text: 'a' }],
    );
    expect(out.mode).toBe('turn-mask');
    expect(out.pcm.length).toBe(16000);
    expect(out.pcm[0]).not.toBe(0);
    expect(out.pcm[5000]).toBe(0);
  });
});
