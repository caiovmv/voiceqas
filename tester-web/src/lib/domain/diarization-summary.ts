import type { DiarizationSpeaker, DiarizationTurn } from '../types';

export interface DiarizationSummary {
  speakerCount: number;
  primarySpeaker: number;
  speakers: DiarizationSpeaker[];
}

/** Build speaker rollup from API speakers[] or from turns. */
export function summarizeDiarization(
  turns: DiarizationTurn[],
  primarySpeaker = 0,
  speakersFromApi?: DiarizationSpeaker[],
): DiarizationSummary {
  if (speakersFromApi && speakersFromApi.length > 0) {
    const primary =
      speakersFromApi.find((s) => s.is_primary)?.speaker_id ?? primarySpeaker;
    return {
      speakerCount: speakersFromApi.length,
      primarySpeaker: primary,
      speakers: [...speakersFromApi].sort((a, b) => {
        if (a.is_primary !== b.is_primary) return a.is_primary ? -1 : 1;
        return a.speaker_id - b.speaker_id;
      }),
    };
  }

  const byId = new Map<number, { duration_ms: number; turn_count: number; is_primary: boolean }>();
  for (const t of turns) {
    const cur = byId.get(t.speaker_id) ?? { duration_ms: 0, turn_count: 0, is_primary: false };
    cur.duration_ms += Math.max(0, t.end_ms - t.start_ms);
    cur.turn_count += 1;
    cur.is_primary = cur.is_primary || Boolean(t.is_primary) || t.speaker_id === primarySpeaker;
    byId.set(t.speaker_id, cur);
  }

  const speakers: DiarizationSpeaker[] = [...byId.entries()].map(([speaker_id, v]) => ({
    speaker_id,
    is_primary: v.is_primary || speaker_id === primarySpeaker,
    duration_ms: v.duration_ms,
    turn_count: v.turn_count,
    role: v.is_primary || speaker_id === primarySpeaker ? 'primary' : 'secondary',
  }));
  speakers.sort((a, b) => {
    if (a.is_primary !== b.is_primary) return a.is_primary ? -1 : 1;
    return a.speaker_id - b.speaker_id;
  });

  return {
    speakerCount: speakers.length,
    primarySpeaker,
    speakers,
  };
}

export function speakerRoleLabel(s: DiarizationSpeaker, indexAmongSecondary: number): string {
  if (s.is_primary || s.role === 'primary') return 'Principal';
  if (indexAmongSecondary === 0) return 'Secundario';
  return `Interlocutor ${s.speaker_id}`;
}
