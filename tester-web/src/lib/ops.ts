import type { TrackedSession } from '../lib/types';

const SCORE_ALERT = 0.45;
const STT_NOT_READY_MS = 30_000;

export function computeAlerts(sessions: TrackedSession[]): TrackedSession[] {
  const now = Date.now();
  return sessions.map((s) => {
    let sttNotReadySince = s.sttNotReadySince;
    if (s.latest) {
      if (!s.latest.stt_ready) {
        sttNotReadySince ??= now;
      } else {
        sttNotReadySince = undefined;
      }
    }
    return { ...s, sttNotReadySince };
  });
}

export function sessionAlerts(sessions: TrackedSession[]) {
  const alerts: Array<{ sessionId: string; message: string; kind: string }> = [];
  const now = Date.now();
  for (const s of sessions) {
    if (s.latest && s.latest.composite_score < SCORE_ALERT) {
      alerts.push({
        sessionId: s.sessionId,
        kind: 'low_score',
        message: `Score baixo (${(s.latest.composite_score * 100).toFixed(0)})`,
      });
    }
    if (s.sttNotReadySince && now - s.sttNotReadySince > STT_NOT_READY_MS) {
      alerts.push({
        sessionId: s.sessionId,
        kind: 'stt_not_ready',
        message: 'stt_ready false > 30s',
      });
    }
  }
  return alerts;
}

export function exportSessionsCsv(sessions: TrackedSession[]) {
  const rows = [
    [
      'session_id',
      'timestamp',
      'composite_score',
      'stt_ready',
      'snr_db',
      'packet_loss_pct',
      'jitter_ms',
      'rms_dbfs',
    ].join(','),
  ];
  for (const s of sessions) {
    for (const h of s.history) {
      rows.push(
        [
          s.sessionId,
          h.window_start_ms,
          h.composite_score.toFixed(4),
          h.stt_ready,
          h.snr_estimate_db.toFixed(2),
          h.packet_loss_pct.toFixed(2),
          h.jitter_ms.toFixed(2),
          h.rms_dbfs.toFixed(2),
        ].join(','),
      );
    }
  }
  const blob = new Blob([rows.join('\n')], { type: 'text/csv' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `voiceqas-sessions-${Date.now()}.csv`;
  a.click();
  URL.revokeObjectURL(url);
}
