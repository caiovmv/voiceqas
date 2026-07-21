import { describe, expect, it } from 'vitest';
import { upsertSession } from './session-merge';
import type { TrackedSession, WindowMetrics } from '../types';

const metric = (score: number): WindowMetrics => ({
  window_start_ms: 1,
  composite_score: score,
  stt_ready: true,
  snr_estimate_db: 20,
  silence_ratio: 0.1,
  clipping_ratio: 0,
  packet_loss_pct: 0,
  jitter_ms: 0,
  rms_dbfs: -20,
  peak_dbfs: -10,
  spectral_flatness: 0.2,
});

describe('upsertSession', () => {
  it('creates a new tracked session', () => {
    const map = upsertSession(new Map(), 's1', { latest: metric(0.8) });
    expect(map.get('s1')?.sessionId).toBe('s1');
    expect(map.get('s1')?.history).toHaveLength(1);
  });

  it('appends history on new metrics', () => {
    let map = new Map<string, TrackedSession>();
    map = upsertSession(map, 's1', { latest: metric(0.5) });
    map = upsertSession(map, 's1', { latest: metric(0.6) });
    expect(map.get('s1')?.history).toHaveLength(2);
  });

  it('tracks stt not ready since', () => {
    let map = upsertSession(new Map(), 's1', { latest: { ...metric(0.8), stt_ready: false } });
    expect(map.get('s1')?.sttNotReadySince).toBeDefined();
    map = upsertSession(map, 's1', { latest: { ...metric(0.8), stt_ready: true } });
    expect(map.get('s1')?.sttNotReadySince).toBeUndefined();
  });
});
