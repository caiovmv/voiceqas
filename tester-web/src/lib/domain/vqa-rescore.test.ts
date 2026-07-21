import { describe, expect, it } from 'vitest';
import type { BatchResult, WindowMetrics } from '../types';
import { LEGACY_MIX, DEFAULT_MIX } from './pipeline-mix';
import { compareSummaries, rescoreBatch, speechQualityScore } from './vqa-rescore';

function win(partial: Partial<WindowMetrics>): WindowMetrics {
  return {
    window_start_ms: 0,
    rms_dbfs: -25,
    peak_dbfs: -10,
    clipping_ratio: 0,
    snr_estimate_db: 30,
    silence_ratio: 0.1,
    spectral_flatness: 0.05,
    packet_loss_pct: 0,
    jitter_ms: 0,
    composite_score: 100,
    stt_ready: true,
    ...partial,
  };
}

describe('speechQualityScore', () => {
  it('omits silence penalty when split enabled', () => {
    const noisySilence = win({ silence_ratio: 0.9, snr_estimate_db: 30 });
    const split = speechQualityScore(noisySilence, DEFAULT_MIX.gate);
    const legacy = speechQualityScore(noisySilence, LEGACY_MIX.gate);
    expect(split).toBeGreaterThan(legacy);
  });
});

describe('rescoreBatch', () => {
  it('marks high-silence windows not ready with stricter presence', () => {
    const batch: BatchResult = {
      composite_score: 80,
      stt_ready: true,
      windows: [
        win({ window_start_ms: 0, silence_ratio: 0.1, snr_estimate_db: 35 }),
        win({ window_start_ms: 500, silence_ratio: 0.1, snr_estimate_db: 35 }),
        win({ window_start_ms: 1000, silence_ratio: 0.58, snr_estimate_db: 25, composite_score: 94 }),
        win({ window_start_ms: 1500, silence_ratio: 0.58, snr_estimate_db: 25, composite_score: 94 }),
      ],
      stt_ready_segments: [],
    };
    const after = rescoreBatch(batch, DEFAULT_MIX.gate);
    const mid = after.windows.find((w) => w.window_start_ms === 1000);
    expect(mid?.stt_ready).toBe(false);
  });

  it('raises session score vs legacy when silence dominated', () => {
    const batch: BatchResult = {
      composite_score: 50,
      stt_ready: true,
      windows: [
        win({ window_start_ms: 0, silence_ratio: 0, snr_estimate_db: 40 }),
        win({ window_start_ms: 500, silence_ratio: 0, snr_estimate_db: 40 }),
        win({ window_start_ms: 1000, silence_ratio: 1, snr_estimate_db: -10, rms_dbfs: -60 }),
        win({ window_start_ms: 1500, silence_ratio: 1, snr_estimate_db: -10, rms_dbfs: -60 }),
      ],
      stt_ready_segments: [],
    };
    const before = rescoreBatch(batch, LEGACY_MIX.gate);
    const after = rescoreBatch(batch, DEFAULT_MIX.gate);
    const delta = compareSummaries(before, after);
    expect(delta.score_delta).toBeGreaterThan(0);
    expect(after.speech_window_count).toBe(2);
  });
});
