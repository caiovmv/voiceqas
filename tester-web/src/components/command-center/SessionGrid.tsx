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

export function SessionGrid({ sessions }: { sessions: TrackedSession[] }) {
  if (sessions.length === 0) {
    return (
      <p className="muted cc-empty">
        Nenhuma sessão ainda. Conecte o ops WebSocket e aguarde tráfego UDP/WS em produção.
      </p>
    );
  }

  return (
    <div className="cc-session-grid">
      {sessions.map((s) => {
        const m = s.latest;
        const scores = s.history.map((h) => h.composite_score);
        return (
          <article key={s.sessionId} className="cc-session-tile">
            <header>
              <code className="cc-session-id">{s.sessionId}</code>
              {s.mediaMeta && <span className="badge ok">media</span>}
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
                  <li>loss {m.packet_loss_pct.toFixed(1)}%</li>
                  <li>jitter {m.jitter_ms.toFixed(1)} ms</li>
                  <li>RMS {m.rms_dbfs.toFixed(1)} dBFS</li>
                </ul>
                <div className="cc-spark-row">
                  <span className="muted">score</span>
                  <Sparkline values={scores} />
                </div>
              </>
            ) : (
              <p className="muted">Aguardando métricas…</p>
            )}
            {s.mediaMeta && (
              <p className="muted cc-media-meta">
                {String(s.mediaMeta.format)} @ {s.mediaMeta.sample_rate}Hz
                {s.mediaMeta.inbound_host
                  ? ` ← ${s.mediaMeta.inbound_host}:${s.mediaMeta.inbound_port}`
                  : ''}
                {s.mediaMeta.remote_host
                  ? ` → ${s.mediaMeta.remote_host}:${s.mediaMeta.remote_port}`
                  : ''}
              </p>
            )}
            {s.lastStt?.text && (
              <p className="cc-stt-snippet" title={s.lastStt.type}>
                STT: {s.lastStt.text.slice(0, 120)}
                {s.lastStt.text.length > 120 ? '…' : ''}
              </p>
            )}
          </article>
        );
      })}
    </div>
  );
}
