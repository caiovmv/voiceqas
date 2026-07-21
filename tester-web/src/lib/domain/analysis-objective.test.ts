import { describe, expect, it } from 'vitest';
import { buildAnalysisObjective, tokenF1 } from './analysis-objective';
import type { RescoreSummary, RescoredWindow } from './vqa-rescore';

function win(partial: Partial<RescoredWindow> & { window_start_ms: number }): RescoredWindow {
  return {
    window_start_ms: partial.window_start_ms,
    rms_dbfs: partial.rms_dbfs ?? -20,
    peak_dbfs: partial.peak_dbfs ?? -6,
    clipping_ratio: partial.clipping_ratio ?? 0,
    snr_estimate_db: partial.snr_estimate_db ?? 30,
    silence_ratio: partial.silence_ratio ?? 0.1,
    spectral_flatness: partial.spectral_flatness ?? 0.2,
    packet_loss_pct: 0,
    jitter_ms: 0,
    composite_score: partial.composite_score ?? 80,
    stt_ready: partial.stt_ready ?? true,
    speech_quality_score: partial.speech_quality_score ?? 80,
  };
}

function summary(speech: RescoredWindow[]): RescoreSummary {
  return {
    windows: speech,
    speech_windows: speech,
    speech_window_count: speech.length,
    composite_score: 80,
    overall_window_mean: 80,
    speech_aggregated_score: 82,
    stt_ready: true,
    stt_ready_segments: [],
    ready_window_count: speech.length,
    ready_ratio: 0.9,
    stt_risk: 0.1,
  };
}

describe('tokenF1', () => {
  it('is 1 for identical text', () => {
    expect(tokenF1('oi caio suporte', 'oi caio suporte')).toBe(1);
  });
  it('drops when tokens diverge', () => {
    expect(tokenF1('latissima suporte', 'laticia forte')).toBeLessThan(0.5);
  });
});

describe('buildAnalysisObjective', () => {
  it('aggregates speech SNR/crest and marks WER unavailable', () => {
    const before = summary([
      win({ window_start_ms: 0, snr_estimate_db: 20, rms_dbfs: -24, peak_dbfs: -8 }),
      win({ window_start_ms: 100, snr_estimate_db: 40, rms_dbfs: -22, peak_dbfs: -10 }),
    ]);
    const after = summary([
      win({ window_start_ms: 0, snr_estimate_db: 35, rms_dbfs: -18, peak_dbfs: -4, clipping_ratio: 0.01 }),
    ]);
    const obj = buildAnalysisObjective({
      beforeSummary: before,
      afterSummary: after,
      beforeSttText: 'Oi Caio latissima suporte',
      afterSttText: 'Oi Caio latissima suporte',
      beforeSttMs: 2000,
      afterSttMs: 1800,
    });
    expect(obj.before.snr_db_mean).toBe(30);
    expect(obj.after.snr_db_mean).toBe(35);
    expect(obj.after.crest_db_mean).toBe(14);
    expect(obj.stt_token_f1_after_vs_before).toBe(1);
    expect(obj.unavailable).toContain('WER');
    expect(obj.unavailable).toContain('PESQ');
    expect(obj.after.stt_processing_ms).toBe(1800);
  });
});
