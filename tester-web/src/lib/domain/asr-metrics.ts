import type { OpsEvent, OpsSttEvent, TrackedSession, WindowMetrics } from '../types';

export interface AsrLatencyPoint {
  t: number;
  v: number;
  sessionId: string;
  partial: boolean;
}

export interface AsrSnrPoint {
  t: number;
  v: number;
  sessionId: string;
}

export interface AsrMetricsSnapshot {
  /** Sessões com VQA gate stt_ready=true (ASR-ready). */
  asrReadySessions: number;
  activeSessions: number;
  /** asrReady / active (0–1). */
  asrReadyRate: number;
  /** Finais ok com texto / finais total (0–1). */
  successRate: number;
  finalsTotal: number;
  finalsOk: number;
  finalsWithText: number;
  /** Sessões ativas com alguma transcrição / ativas (0–1). */
  coverage: number;
  sessionsWithTranscript: number;
  /** SNR médio das sessões com VQA recente. */
  snrAvgDb: number | null;
  snrMinDb: number | null;
  snrMaxDb: number | null;
  /** Latência ASR (processing_ms) dos eventos finais. */
  latencyAvgMs: number | null;
  latencyP95Ms: number | null;
  lastLatencyMs: number | null;
  /** Parciais + finais recentes. */
  latencySeries: AsrLatencyPoint[];
  snrSeries: AsrSnrPoint[];
  recentTranscripts: Array<{
    sessionId: string;
    type: 'stt_final' | 'stt_partial';
    text: string;
    model?: string;
    processingMs?: number;
    ok: boolean;
    t: number;
  }>;
  /** WER/CER exigem referência humana — não calculados neste serviço. */
  werAvailable: false;
  cerAvailable: false;
}

function isSttEvent(ev: OpsEvent): ev is OpsSttEvent {
  return ev.type === 'stt_final' || ev.type === 'stt_partial';
}

function percentile(sortedAsc: number[], p: number): number | null {
  if (sortedAsc.length === 0) return null;
  const idx = Math.min(sortedAsc.length - 1, Math.max(0, Math.ceil(p * sortedAsc.length) - 1));
  return sortedAsc[idx];
}

function eventTimeMs(ev: OpsSttEvent, fallback: number): number {
  if (typeof ev.ts_ms === 'number' && Number.isFinite(ev.ts_ms)) return ev.ts_ms;
  return fallback;
}

/**
 * Agrega métricas ASR/STT a partir de sessões ao vivo + histórico ops.
 * WER/CER ficam indisponíveis (sem ground truth no voiceqas).
 */
export function computeAsrMetrics(
  sessions: TrackedSession[],
  historyEvents: OpsEvent[] = [],
  nowMs = Date.now(),
): AsrMetricsSnapshot {
  const active = sessions.filter((s) => s.latest);
  const asrReadySessions = active.filter((s) => s.latest?.stt_ready).length;
  const activeSessions = active.length;
  const asrReadyRate = activeSessions > 0 ? asrReadySessions / activeSessions : 0;

  const sessionsWithTranscript = active.filter((s) => {
    const t = s.lastStt?.text?.trim();
    return Boolean(t);
  }).length;
  const coverage = activeSessions > 0 ? sessionsWithTranscript / activeSessions : 0;

  const snrValues = active
    .map((s) => s.latest?.snr_estimate_db)
    .filter((v): v is number => typeof v === 'number' && Number.isFinite(v));
  const snrAvgDb =
    snrValues.length > 0 ? snrValues.reduce((a, b) => a + b, 0) / snrValues.length : null;
  const snrMinDb = snrValues.length > 0 ? Math.min(...snrValues) : null;
  const snrMaxDb = snrValues.length > 0 ? Math.max(...snrValues) : null;

  const sttFromHistory = historyEvents.filter(isSttEvent);
  const sttFromSessions = sessions
    .map((s) => s.lastStt)
    .filter((e): e is OpsSttEvent => Boolean(e));

  const byKey = new Map<string, OpsSttEvent>();
  for (const ev of [...sttFromHistory, ...sttFromSessions]) {
    const key = `${ev.session_id}|${ev.type}|${ev.text}|${ev.processing_ms ?? ''}|${ev.duration_ms ?? ''}`;
    byKey.set(key, ev);
  }
  const sttEvents = [...byKey.values()];

  const finals = sttEvents.filter((e) => e.type === 'stt_final');
  const finalsTotal = finals.length;
  const finalsOk = finals.filter((e) => e.ok !== false && !e.error).length;
  const finalsWithText = finals.filter((e) => e.ok !== false && (e.text?.trim().length ?? 0) > 0).length;
  const successRate = finalsTotal > 0 ? finalsWithText / finalsTotal : 0;

  const latencySamples = sttEvents
    .filter((e) => typeof e.processing_ms === 'number' && (e.processing_ms as number) >= 0)
    .map((e) => e.processing_ms as number);
  const latencySorted = [...latencySamples].sort((a, b) => a - b);
  const latencyAvgMs =
    latencySorted.length > 0
      ? latencySorted.reduce((a, b) => a + b, 0) / latencySorted.length
      : null;
  const latencyP95Ms = percentile(latencySorted, 0.95);
  const lastLatencyMs = latencySorted.length > 0 ? latencySamples[latencySamples.length - 1] : null;

  const latencySeries: AsrLatencyPoint[] = sttEvents
    .filter((e) => typeof e.processing_ms === 'number')
    .map((e, i) => ({
      t: eventTimeMs(e, nowMs - (sttEvents.length - i) * 1000),
      v: e.processing_ms as number,
      sessionId: e.session_id,
      partial: e.type === 'stt_partial',
    }))
    .sort((a, b) => a.t - b.t)
    .slice(-60);

  const snrSeries: AsrSnrPoint[] = [];
  for (const s of sessions) {
    const hist = s.history.slice(-20);
    hist.forEach((w, i) => {
      if (typeof w.snr_estimate_db === 'number') {
        snrSeries.push({
          t: s.lastSeen - (hist.length - i) * 500,
          v: w.snr_estimate_db,
          sessionId: s.sessionId,
        });
      }
    });
    if (s.latest && typeof s.latest.snr_estimate_db === 'number') {
      snrSeries.push({ t: s.lastSeen, v: s.latest.snr_estimate_db, sessionId: s.sessionId });
    }
  }
  snrSeries.sort((a, b) => a.t - b.t);

  const recentTranscripts = sttEvents
    .map((e, i) => ({
      sessionId: e.session_id,
      type: e.type,
      text: e.text ?? '',
      model: e.model,
      processingMs: e.processing_ms,
      ok: e.ok !== false && !e.error,
      t: eventTimeMs(e, nowMs - (sttEvents.length - i) * 1000),
    }))
    .sort((a, b) => b.t - a.t)
    .slice(0, 12);

  return {
    asrReadySessions,
    activeSessions,
    asrReadyRate,
    successRate,
    finalsTotal,
    finalsOk,
    finalsWithText,
    coverage,
    sessionsWithTranscript,
    snrAvgDb,
    snrMinDb,
    snrMaxDb,
    latencyAvgMs,
    latencyP95Ms,
    lastLatencyMs,
    latencySeries,
    snrSeries: snrSeries.slice(-80),
    recentTranscripts,
    werAvailable: false,
    cerAvailable: false,
  };
}

export function snrTone(snrDb: number | null): 'ok' | 'warn' | 'bad' | undefined {
  if (snrDb === null) return undefined;
  if (snrDb >= 18) return 'ok';
  if (snrDb >= 12) return 'warn';
  return 'bad';
}

/** Concatena histórico VQA de um evento para série when needed. */
export function windowSnrPoints(windows: WindowMetrics[], sessionId: string, baseTs: number): AsrSnrPoint[] {
  return windows.map((w, i) => ({
    t: baseTs + (w.window_start_ms || i * 500),
    v: w.snr_estimate_db,
    sessionId,
  }));
}
