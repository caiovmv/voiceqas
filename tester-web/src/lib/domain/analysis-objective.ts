/** Aggregate VQA/STT evidence for Analysis Lab LLM (objective DSP validation). */

import type { RescoreSummary } from './vqa-rescore';

export const ANALYSIS_UNAVAILABLE_METRICS = [
  'LUFS',
  'PESQ',
  'STOI',
  'WER',
  'MOS',
  'SDR',
  'Sibilance Index',
  'Formantes F1-F3',
] as const;

export interface SideObjectiveMetrics {
  snr_db_mean: number | null;
  snr_db_p50: number | null;
  rms_dbfs_mean: number | null;
  peak_dbfs_mean: number | null;
  crest_db_mean: number | null;
  clipping_ratio_mean: number | null;
  clipping_ratio_max: number | null;
  spectral_flatness_mean: number | null;
  silence_ratio_mean: number | null;
  ready_ratio: number;
  composite_score: number;
  speech_aggregated_score: number;
  speech_window_count: number;
  stt_chars: number;
  stt_processing_ms: number | null;
}

export interface AnalysisObjective {
  before: SideObjectiveMetrics;
  after: SideObjectiveMetrics;
  /** Lexical token F1 of after.stt vs before.stt ? NOT Word Error Rate. */
  stt_token_f1_after_vs_before: number | null;
  unavailable: readonly string[];
  notes: string[];
}

function mean(xs: number[]): number | null {
  if (xs.length === 0) return null;
  return xs.reduce((a, b) => a + b, 0) / xs.length;
}

function p50(xs: number[]): number | null {
  if (xs.length === 0) return null;
  const s = [...xs].sort((a, b) => a - b);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 === 1 ? s[mid]! : (s[mid - 1]! + s[mid]!) / 2;
}

function round2(n: number | null): number | null {
  if (n === null || Number.isNaN(n)) return null;
  return Math.round(n * 100) / 100;
}

export function tokenF1(ref: string, hyp: string): number {
  const a = ref
    .toLocaleLowerCase('pt-BR')
    .normalize('NFD')
    .replace(/\p{M}/gu, '')
    .split(/\s+/)
    .filter(Boolean);
  const b = hyp
    .toLocaleLowerCase('pt-BR')
    .normalize('NFD')
    .replace(/\p{M}/gu, '')
    .split(/\s+/)
    .filter(Boolean);
  if (a.length === 0 && b.length === 0) return 1;
  if (a.length === 0 || b.length === 0) return 0;
  const ca = new Map<string, number>();
  const cb = new Map<string, number>();
  for (const t of a) ca.set(t, (ca.get(t) ?? 0) + 1);
  for (const t of b) cb.set(t, (cb.get(t) ?? 0) + 1);
  let overlap = 0;
  for (const [t, n] of ca) {
    overlap += Math.min(n, cb.get(t) ?? 0);
  }
  const prec = overlap / b.length;
  const rec = overlap / a.length;
  if (prec + rec === 0) return 0;
  return (2 * prec * rec) / (prec + rec);
}

function sideFromSummary(
  summary: RescoreSummary,
  sttText: string | null | undefined,
  sttMs: number | null | undefined,
): SideObjectiveMetrics {
  const speech = summary.speech_windows?.length
    ? summary.speech_windows
    : summary.windows.filter((w) => w.silence_ratio < 0.85);

  const snrs = speech.map((w) => w.snr_estimate_db).filter((x) => Number.isFinite(x));
  const rms = speech.map((w) => w.rms_dbfs).filter((x) => Number.isFinite(x));
  const peaks = speech.map((w) => w.peak_dbfs).filter((x) => Number.isFinite(x));
  const clips = speech.map((w) => w.clipping_ratio).filter((x) => Number.isFinite(x));
  const flat = speech.map((w) => w.spectral_flatness).filter((x) => Number.isFinite(x));
  const sil = speech.map((w) => w.silence_ratio).filter((x) => Number.isFinite(x));
  const crests: number[] = [];
  for (const w of speech) {
    if (Number.isFinite(w.peak_dbfs) && Number.isFinite(w.rms_dbfs)) {
      crests.push(w.peak_dbfs - w.rms_dbfs);
    }
  }

  return {
    snr_db_mean: round2(mean(snrs)),
    snr_db_p50: round2(p50(snrs)),
    rms_dbfs_mean: round2(mean(rms)),
    peak_dbfs_mean: round2(mean(peaks)),
    crest_db_mean: round2(mean(crests)),
    clipping_ratio_mean: round2(mean(clips)),
    clipping_ratio_max: round2(clips.length ? Math.max(...clips) : null),
    spectral_flatness_mean: round2(mean(flat)),
    silence_ratio_mean: round2(mean(sil)),
    ready_ratio: round2(summary.ready_ratio) ?? 0,
    composite_score: round2(summary.composite_score) ?? 0,
    speech_aggregated_score: round2(summary.speech_aggregated_score) ?? 0,
    speech_window_count: summary.speech_window_count,
    stt_chars: (sttText ?? '').length,
    stt_processing_ms:
      sttMs === undefined || sttMs === null || Number.isNaN(sttMs) ? null : Math.round(sttMs),
  };
}

export function buildAnalysisObjective(input: {
  beforeSummary: RescoreSummary;
  afterSummary: RescoreSummary;
  beforeSttText?: string | null;
  afterSttText?: string | null;
  beforeSttMs?: number | null;
  afterSttMs?: number | null;
}): AnalysisObjective {
  const beforeText = input.beforeSttText ?? '';
  const afterText = input.afterSttText ?? '';
  const f1 =
    beforeText.trim() || afterText.trim() ? tokenF1(beforeText, afterText) : null;

  return {
    before: sideFromSummary(input.beforeSummary, beforeText, input.beforeSttMs),
    after: sideFromSummary(input.afterSummary, afterText, input.afterSttMs),
    stt_token_f1_after_vs_before: f1 === null ? null : round2(f1),
    unavailable: ANALYSIS_UNAVAILABLE_METRICS,
    notes: [
      'Aggregates use speech_windows (fallback: silence_ratio < 0.85).',
      'stt_token_f1_after_vs_before is lexical overlap vs LEGACY text, not WER.',
      'Crest factor approximated as peak_dbfs - rms_dbfs on speech windows.',
      'AGC target is RMS dBFS proxy, not LUFS.',
    ],
  };
}
