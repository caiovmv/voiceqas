import type { TrackedSession } from '../../lib/types';

function scoreClass(score: number): string {
  if (score >= 0.7) return 'ok';
  if (score >= 0.45) return 'warn';
  return 'bad';
}

function Sparkline({ values }: { values: number[] }) {
  if (values.length < 2) {
    return <span className="muted">—</span>;
  }
  const min = Math.min(...values);
  const max = Math.max(...values);
  const range = max - min || 1;
  const w = 80;
  const h = 24;
  const points = values
    .map((v, i) => {
      const x = (i / (values.length - 1)) * w;
      const y = h - ((v - min) / range) * h;
      return `${x},${y}`;
    })
    .join(' ');

  return (
    <svg width={w} height={h} className="sparkline" aria-hidden>
      <polyline fill="none" stroke="currentColor" strokeWidth="1.5" points={points} />
    </svg>
  );
}

function transportLabel(meta: TrackedSession['mediaMeta']): string {
  if (!meta) return '—';
  if (meta.channel_id && meta.channel_id !== 'default') return meta.channel_id;
  return 'default';
}

export function SessionGrid({
  sessions,
  compact = false,
  showChannelColumn = false,
}: {
  sessions: TrackedSession[];
  compact?: boolean;
  showChannelColumn?: boolean;
}) {
  if (sessions.length === 0) {
    return (
      <p className="muted cc-empty">
        Nenhuma sessão ainda. Ops WS reconecta a cada 10s — aguarde tráfego ou registre media.
      </p>
    );
  }

  return (
    <div className={`cc-session-grid ${compact ? 'compact' : ''}`}>
      {sessions.map((s) => {
        const m = s.latest;
        const scores = s.history.map((h) => h.composite_score);
        return (
          <article key={s.sessionId} className="cc-session-tile">
            <header>
              <code className="cc-session-id">{s.sessionId}</code>
              {s.mediaMeta && <span className="badge ok">media</span>}
              {showChannelColumn && (
                <span className="badge" title="Canal / transporte">
                  {transportLabel(s.mediaMeta)}
                </span>
              )}
            </header>
            {m ? (
              <>
                <div className={`cc-score ${scoreClass(m.composite_score)}`}>
                  {(m.composite_score * 100).toFixed(0)}
                  <span>score</span>
                </div>
                <ul className="metrics">
                  <li>
                    STT{' '}
                    <span className={`badge ${m.stt_ready ? 'ok' : 'bad'}`}>
                      {m.stt_ready ? 'ready' : 'no'}
                    </span>
                  </li>
                  <li>SNR {m.snr_estimate_db.toFixed(1)} dB</li>
                  {!compact && (
                    <>
                      <li>loss {m.packet_loss_pct.toFixed(1)}%</li>
                      <li>jitter {m.jitter_ms.toFixed(1)} ms</li>
                      <li>RMS {m.rms_dbfs.toFixed(1)} dBFS</li>
                    </>
                  )}
                </ul>
                {!compact && (
                  <div className="cc-spark-row">
                    <span className="muted">score</span>
                    <Sparkline values={scores} />
                  </div>
                )}
              </>
            ) : (
              <p className="muted">Aguardando métricas…</p>
            )}
            {s.mediaMeta && !compact && (
              <p className="muted cc-media-meta">
                {String(s.mediaMeta.format)} @ {s.mediaMeta.sample_rate}Hz
                {s.mediaMeta.channel_id ? ` · canal ${s.mediaMeta.channel_id}` : ''}
                {s.mediaMeta.inbound_host
                  ? ` ← ${s.mediaMeta.inbound_host}:${s.mediaMeta.inbound_port}`
                  : ''}
              </p>
            )}
            {s.lastStt?.text && (
              <p className="cc-stt-snippet" title={s.lastStt.type}>
                STT: {s.lastStt.text.slice(0, compact ? 80 : 120)}
                {s.lastStt.text.length > (compact ? 80 : 120) ? '…' : ''}
              </p>
            )}
          </article>
        );
      })}
    </div>
  );
}
