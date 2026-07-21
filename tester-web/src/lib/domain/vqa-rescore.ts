import type { BatchResult, WindowMetrics } from '../types';
import type { VqaGateParams } from './pipeline-mix';

/** Per-window quality score — optionally without silence penalty. */
export function speechQualityScore(
  m: Pick<
    WindowMetrics,
    | 'clipping_ratio'
    | 'silence_ratio'
    | 'snr_estimate_db'
    | 'spectral_flatness'
    | 'packet_loss_pct'
    | 'jitter_ms'
    | 'rms_dbfs'
  >,
  gate: VqaGateParams,
): number {
  let score = 100.0;

  score -=
    Math.min(1, m.clipping_ratio / Math.max(gate.max_clipping_ratio, 1e-6)) * 30.0;

  if (!gate.split_quality_from_presence) {
    score -=
      Math.min(1, m.silence_ratio / Math.max(gate.max_silence_ratio, 1e-6)) * 25.0;
  }

  const snrFactor = Math.min(
    1.5,
    Math.max(0, m.snr_estimate_db / Math.max(gate.min_snr_db, 1e-6)),
  );
  score = score * (0.5 + 0.5 * snrFactor);

  const flatnessPenalty = Math.min(1, Math.max(0, (m.spectral_flatness - 0.3) / 0.7)) * 15.0;
  score -= flatnessPenalty;

  const rtpPenalty =
    Math.min(1, m.packet_loss_pct / 5.0) * 10.0 + Math.min(1, m.jitter_ms / 50.0) * 10.0;
  score -= rtpPenalty;

  const levelPenalty =
    m.rms_dbfs < -50.0 ? Math.min(20.0, (-50.0 - m.rms_dbfs) * 0.5) : 0.0;
  score -= levelPenalty;

  return Math.min(100, Math.max(0, score));
}

export function passesThresholds(m: WindowMetrics, quality: number, gate: VqaGateParams): boolean {
  const silenceCap = gate.max_silence_ratio_for_ready;
  return (
    quality >= gate.stt_ready_threshold &&
    m.silence_ratio <= silenceCap &&
    m.clipping_ratio <= gate.max_clipping_ratio &&
    m.snr_estimate_db >= gate.min_snr_db
  );
}

export interface RescoredWindow extends WindowMetrics {
  speech_quality_score: number;
}

export interface RescoreSummary {
  windows: RescoredWindow[];
  speech_windows: RescoredWindow[];
  speech_window_count: number;
  composite_score: number;
  overall_window_mean: number;
  speech_aggregated_score: number;
  stt_ready: boolean;
  stt_ready_segments: Array<{ start_ms: number; end_ms: number }>;
  ready_window_count: number;
  ready_ratio: number;
  stt_risk: number;
}

function applyHysteresis(
  windows: RescoredWindow[],
  gate: VqaGateParams,
): RescoredWindow[] {
  let consecutiveOk = 0;
  let consecutiveBad = 0;
  let state = false;
  return windows.map((w) => {
    const ok = passesThresholds(w, w.speech_quality_score, gate);
    if (ok) {
      consecutiveOk += 1;
      consecutiveBad = 0;
      if (!state && consecutiveOk >= gate.hysteresis_ok_windows) {
        state = true;
      }
    } else {
      consecutiveBad += 1;
      consecutiveOk = 0;
      if (state && consecutiveBad >= gate.hysteresis_bad_windows) {
        state = false;
      }
    }
    return { ...w, composite_score: w.speech_quality_score, stt_ready: state };
  });
}

function buildSegments(
  windows: RescoredWindow[],
): Array<{ start_ms: number; end_ms: number }> {
  const segs: Array<{ start_ms: number; end_ms: number }> = [];
  let segStart = -1;
  for (const w of windows) {
    if (w.stt_ready && segStart < 0) {
      segStart = w.window_start_ms;
    } else if (!w.stt_ready && segStart >= 0) {
      segs.push({ start_ms: segStart, end_ms: w.window_start_ms });
      segStart = -1;
    }
  }
  if (segStart >= 0) {
    const last = windows[windows.length - 1];
    segs.push({
      start_ms: segStart,
      end_ms: (last?.window_start_ms ?? 0) + 500,
    });
  }
  return segs;
}

/** Heuristic STT risk 0–1 from speech windows (higher = worse). */
export function estimateSttRisk(speech: RescoredWindow[]): number {
  if (speech.length === 0) return 1;
  let sum = 0;
  for (const w of speech) {
    const snrTerm = Math.min(1, Math.max(0, (20 - w.snr_estimate_db) / 20));
    const clipTerm = Math.min(1, w.clipping_ratio / 0.05);
    const qualityTerm = Math.min(1, Math.max(0, (70 - w.speech_quality_score) / 70));
    sum += 0.45 * snrTerm + 0.25 * clipTerm + 0.3 * qualityTerm;
  }
  return Math.min(1, Math.max(0, sum / speech.length));
}

export function rescoreBatch(batch: BatchResult, gate: VqaGateParams): RescoreSummary {
  const scored: RescoredWindow[] = batch.windows.map((w) => {
    const speech_quality_score = speechQualityScore(w, gate);
    return { ...w, speech_quality_score };
  });
  const withReady = applyHysteresis(scored, gate);

  const speech = withReady.filter((w) => w.silence_ratio <= gate.max_silence_ratio);
  let speechScoreSum = 0;
  let speechWeight = 0;
  for (const w of speech) {
    const weight = Math.max(0, 1 - w.silence_ratio);
    speechScoreSum += w.speech_quality_score * (weight > 1e-12 ? weight : 1);
    speechWeight += weight > 1e-12 ? weight : 1;
  }
  const speech_aggregated_score =
    speechWeight > 0 ? speechScoreSum / speechWeight : 0;

  let overall = 0;
  for (const w of withReady) {
    overall += w.speech_quality_score;
  }
  overall = withReady.length ? overall / withReady.length : 0;

  const segments = buildSegments(withReady);
  const ready_window_count = withReady.filter((w) => w.stt_ready).length;

  return {
    windows: withReady,
    speech_windows: speech,
    speech_window_count: speech.length,
    composite_score: gate.split_quality_from_presence
      ? speech.length > 0
        ? speech_aggregated_score
        : overall
      : overall,
    overall_window_mean: overall,
    speech_aggregated_score,
    stt_ready: segments.length > 0,
    stt_ready_segments: segments,
    ready_window_count,
    ready_ratio: withReady.length ? ready_window_count / withReady.length : 0,
    stt_risk: estimateSttRisk(speech),
  };
}

export function compareSummaries(before: RescoreSummary, after: RescoreSummary) {
  return {
    score_delta: after.composite_score - before.composite_score,
    ready_ratio_delta: after.ready_ratio - before.ready_ratio,
    risk_delta: after.stt_risk - before.stt_risk,
    speech_windows_delta: after.speech_window_count - before.speech_window_count,
  };
}
