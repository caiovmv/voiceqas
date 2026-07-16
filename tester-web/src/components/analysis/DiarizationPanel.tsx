import type { DiarizationSpeaker, DiarizationTurn } from '../../lib/types';
import {
  speakerRoleLabel,
  summarizeDiarization,
} from '../../lib/domain/diarization-summary';

interface Props {
  turns: DiarizationTurn[];
  primarySpeaker?: number;
  speakers?: DiarizationSpeaker[];
  title: string;
  hint?: string;
}

const COLORS = ['#3d8b6e', '#c47a3a', '#4a6fa5', '#8b5a9e'];

export function DiarizationPanel({
  turns,
  primarySpeaker = 0,
  speakers,
  title,
  hint,
}: Props) {
  if (turns.length === 0) {
    return (
      <div className="analysis-dia empty">
        <p className="muted">{title}: sem turnos</p>
        {hint && <p className="muted">{hint}</p>}
      </div>
    );
  }

  const summary = summarizeDiarization(turns, primarySpeaker, speakers);
  const maxEnd = Math.max(...turns.map((t) => t.end_ms), 1);
  let secondaryIdx = 0;

  return (
    <div className="analysis-dia">
      <div className="analysis-timeline-head">{title}</div>
      <div className="analysis-dia-summary">
        <strong>{summary.speakerCount}</strong> interlocutor
        {summary.speakerCount === 1 ? '' : 'es'}
        <ul className="analysis-dia-speakers">
          {summary.speakers.map((s) => {
            const label = speakerRoleLabel(
              s,
              s.is_primary ? -1 : secondaryIdx++,
            );
            return (
              <li key={s.speaker_id}>
                <span
                  className="analysis-dia-swatch"
                  style={{ background: COLORS[s.speaker_id % COLORS.length] }}
                />
                <strong>{label}</strong>
                {' '}
                (S{s.speaker_id}) · {(s.duration_ms / 1000).toFixed(1)}s · {s.turn_count} turno
                {s.turn_count === 1 ? '' : 's'}
              </li>
            );
          })}
        </ul>
      </div>
      <div className="analysis-dia-track">
        {turns.map((t, i) => (
          <div
            key={`${t.start_ms}-${i}`}
            className={`analysis-dia-seg${t.is_primary ? ' primary' : ''}`}
            style={{
              left: `${(t.start_ms / maxEnd) * 100}%`,
              width: `${(Math.max(1, t.end_ms - t.start_ms) / maxEnd) * 100}%`,
              background: COLORS[t.speaker_id % COLORS.length],
            }}
            title={`S${t.speaker_id} ${t.start_ms}-${t.end_ms}ms`}
          />
        ))}
      </div>
      <ul className="analysis-dia-list">
        {turns.map((t, i) => (
          <li key={`${t.speaker_id}-${t.start_ms}-${i}`}>
            <strong>S{t.speaker_id}</strong>
            {t.is_primary || t.speaker_id === primarySpeaker ? ' (principal)' : ' (secundario)'}
            {' '}
            {t.start_ms}–{t.end_ms} ms
            {t.rms_dbfs != null ? ` · ${t.rms_dbfs.toFixed(1)} dBFS` : ''}
            {t.text ? ` — ${t.text}` : ''}
          </li>
        ))}
      </ul>
      {summary.speakerCount < 2 && (
        <p className="muted">
          So um interlocutor detectado. O Silero precisa de pausas / vales de energia para
          separar principal e secundario. Desligue Primary no mix para ASR por turno.
        </p>
      )}
    </div>
  );
}
