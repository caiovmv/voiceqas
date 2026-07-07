import type { TrackedSession } from '../types';

const MAX_HISTORY = 120;

export function upsertSession(
  map: Map<string, TrackedSession>,
  sessionId: string,
  patch: Partial<TrackedSession>,
  maxHistory = MAX_HISTORY,
): Map<string, TrackedSession> {
  const next = new Map(map);
  const prev = next.get(sessionId);
  const history = prev?.history ?? [];
  const latest = patch.latest ?? prev?.latest;
  const mergedHistory =
    latest && latest !== prev?.latest ? [...history, latest].slice(-maxHistory) : history;

  let sttNotReadySince = patch.sttNotReadySince ?? prev?.sttNotReadySince;
  const effectiveLatest = latest ?? prev?.latest;
  if (effectiveLatest) {
    if (!effectiveLatest.stt_ready) {
      sttNotReadySince ??= Date.now();
    } else {
      sttNotReadySince = undefined;
    }
  }

  next.set(sessionId, {
    sessionId,
    firstSeen: prev?.firstSeen ?? Date.now(),
    lastSeen: Date.now(),
    latest: effectiveLatest,
    history: mergedHistory,
    mediaMeta: patch.mediaMeta ?? prev?.mediaMeta,
    lastStt: patch.lastStt ?? prev?.lastStt,
    sttNotReadySince,
  });
  return next;
}
