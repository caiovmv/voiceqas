import { describe, expect, it } from 'vitest';
import { computeAsrMetrics, snrTone } from './asr-metrics';
import type { OpsSttEvent, TrackedSession, WindowMetrics } from '../types';

function metrics(partial: Partial<WindowMetrics>): WindowMetrics {
  return {
    window_start_ms: 0,
    rms_dbfs: -20,
    peak_dbfs: -10,
    clipping_ratio: 0,
    snr_estimate_db: 20,
    silence_ratio: 0.1,
    spectral_flatness: 0.2,
    packet_loss_pct: 0,
    jitter_ms: 0,
    composite_score: 80,
    stt_ready: true,
    ...partial,
  };
}

function session(id: string, latest: WindowMetrics, lastStt?: OpsSttEvent): TrackedSession {
  return {
    sessionId: id,
    firstSeen: 1,
    lastSeen: 1000,
    latest,
    history: [latest],
    lastStt,
  };
}

describe('asr-metrics', () => {
  it('computes ASR-ready rate, coverage and success', () => {
    const sessions = [
      session('a', metrics({ stt_ready: true, snr_estimate_db: 22 }), {
        type: 'stt_final',
        session_id: 'a',
        text: 'olá mundo',
        ok: true,
        processing_ms: 120,
      }),
      session('b', metrics({ stt_ready: false, snr_estimate_db: 8 })),
      session('c', metrics({ stt_ready: true, snr_estimate_db: 15 }), {
        type: 'stt_final',
        session_id: 'c',
        text: '',
        ok: true,
        processing_ms: 200,
      }),
    ];

    const snap = computeAsrMetrics(sessions, []);
    expect(snap.asrReadySessions).toBe(2);
    expect(snap.activeSessions).toBe(3);
    expect(snap.asrReadyRate).toBeCloseTo(2 / 3);
    expect(snap.coverage).toBeCloseTo(1 / 3);
    expect(snap.finalsTotal).toBe(2);
    expect(snap.finalsWithText).toBe(1);
    expect(snap.successRate).toBeCloseTo(0.5);
    expect(snap.snrAvgDb).toBeCloseTo((22 + 8 + 15) / 3);
    expect(snap.latencyP95Ms).toBe(200);
    expect(snap.werAvailable).toBe(false);
    expect(snap.cerAvailable).toBe(false);
  });

  it('merges history STT events for latency series', () => {
    const history: OpsSttEvent[] = [
      {
        type: 'stt_partial',
        session_id: 'x',
        text: 'oi',
        processing_ms: 50,
        ts_ms: 1000,
      },
      {
        type: 'stt_final',
        session_id: 'x',
        text: 'oi equipe',
        ok: true,
        processing_ms: 90,
        ts_ms: 2000,
      },
    ];
    const snap = computeAsrMetrics([], history);
    expect(snap.latencySeries).toHaveLength(2);
    expect(snap.successRate).toBe(1);
    expect(snap.recentTranscripts[0].text).toBe('oi equipe');
  });

  it('maps SNR tone thresholds', () => {
    expect(snrTone(20)).toBe('ok');
    expect(snrTone(12)).toBe('warn');
    expect(snrTone(5)).toBe('bad');
    expect(snrTone(null)).toBeUndefined();
  });
});
