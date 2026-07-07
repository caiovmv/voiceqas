import { describe, expect, it } from 'vitest';
import { computeAlerts, exportSessionsCsv, sessionAlerts } from './ops-alerts';
import { mockAnchorClick } from '../../test/setup';
import type { TrackedSession, WindowMetrics } from '../types';

function makeSession(id: string, score: number, sttReady: boolean): TrackedSession {
  const latest: WindowMetrics = {
    window_start_ms: 1,
    composite_score: score,
    stt_ready: sttReady,
    snr_estimate_db: 20,
    silence_ratio: 0.1,
    clipping_ratio: 0,
    packet_loss_pct: 0,
    jitter_ms: 0,
    rms_dbfs: -20,
    peak_dbfs: -10,
    spectral_flatness: 0.2,
  };
  return { sessionId: id, firstSeen: 0, lastSeen: 0, latest, history: [latest] };
}

describe('sessionAlerts', () => {
  it('flags low composite score', () => {
    const alerts = sessionAlerts([makeSession('a', 0.2, true)]);
    expect(alerts.some((a) => a.kind === 'low_score')).toBe(true);
  });

  it('flags prolonged stt not ready', () => {
    const s = makeSession('b', 0.9, false);
    s.sttNotReadySince = Date.now() - 60_000;
    const alerts = sessionAlerts([s]);
    expect(alerts.some((a) => a.kind === 'stt_not_ready')).toBe(true);
  });

  it('returns empty when healthy', () => {
    const alerts = sessionAlerts([makeSession('c', 0.9, true)]);
    expect(alerts).toHaveLength(0);
  });
});

describe('computeAlerts', () => {
  it('tracks stt not ready since first unhealthy window', () => {
    const session = makeSession('d', 0.9, false);
    const [tracked] = computeAlerts([session]);
    expect(tracked.sttNotReadySince).toBeTypeOf('number');
  });

  it('clears stt not ready when quality recovers', () => {
    const session = makeSession('e', 0.9, false);
    session.sttNotReadySince = Date.now() - 1000;
    const recovered = makeSession('e', 0.9, true);
    const [tracked] = computeAlerts([recovered]);
    expect(tracked.sttNotReadySince).toBeUndefined();
  });
});

describe('exportSessionsCsv', () => {
  it('triggers csv download anchor', () => {
    mockAnchorClick.mockClear();
    exportSessionsCsv([makeSession('f', 0.8, true)]);
    expect(mockAnchorClick).toHaveBeenCalledOnce();
  });
});
