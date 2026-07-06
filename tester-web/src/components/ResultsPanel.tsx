import type { BatchResult, SttResult, Transport, WindowMetrics } from '../lib/types';

interface Props {
  transport: Transport;
  onTransport: (t: Transport) => void;
  loading: boolean;
  error: string | null;
  status: string;
  batch: BatchResult | null;
  streamReports: WindowMetrics[];
  sttResult: SttResult | null;
  onAnalyze: () => void;
  onTranscribe: () => void;
  onGrpcReady: () => void;
}

export function ResultsPanel({
  transport,
  onTransport,
  loading,
  error,
  status,
  batch,
  streamReports,
  sttResult,
  onAnalyze,
  onTranscribe,
  onGrpcReady,
}: Props) {
  const lastStream = streamReports[streamReports.length - 1];

  return (
    <section className="panel">
      <h2>Transporte & resultados</h2>
      <div className="tabs">
        {(['rest', 'websocket', 'grpc'] as Transport[]).map((t) => (
          <button
            key={t}
            className={transport === t ? 'tab active' : 'tab'}
            onClick={() => onTransport(t)}
          >
            {t.toUpperCase()}
          </button>
        ))}
      </div>
      <div className="row">
        <button className="btn primary" disabled={loading} onClick={onAnalyze}>
          {loading ? 'Analisando…' : `Enviar via ${transport.toUpperCase()}`}
        </button>
        <button className="btn" disabled={loading} onClick={onTranscribe}>
          {loading ? 'Transcrevendo…' : `STT (${transport.toUpperCase()})`}
        </button>
        {transport === 'grpc' && (
          <button className="btn" disabled={loading} onClick={onGrpcReady}>
            gRPC Ready
          </button>
        )}
      </div>
      {status && <pre className="log">{status}</pre>}
      {error && <p className="error">{error}</p>}

      {lastStream && (
        <div className="score-card">
          <h3>Último report (stream)</h3>
          <ScoreView m={lastStream} />
        </div>
      )}

      {sttResult && (
        <div className="score-card">
          <h3>STT — {sttResult.model}</h3>
          <p className="stt-text">{sttResult.text || '(vazio)'}</p>
          <p className="muted">
            {sttResult.model} · {sttResult.processing_ms} ms · {sttResult.duration_ms} ms áudio
          </p>
          {sttResult.segments?.length > 0 && (
            <details>
              <summary>{sttResult.segments.length} segmentos</summary>
              <pre className="log">{JSON.stringify(sttResult, null, 2)}</pre>
            </details>
          )}
        </div>
      )}

      {batch && (
        <div className="score-card">
          <h3>Batch</h3>
          <p>
            Score: <strong>{batch.composite_score.toFixed(1)}</strong> | STT-ready:{' '}
            <Badge ok={batch.stt_ready} />
          </p>
          <details>
            <summary>{batch.windows.length} janelas</summary>
            <pre className="log">{JSON.stringify(batch, null, 2)}</pre>
          </details>
        </div>
      )}

      {streamReports.length > 0 && (
        <details>
          <summary>Timeline stream ({streamReports.length})</summary>
          <ul className="timeline">
            {streamReports.map((r, i) => (
              <li key={i}>
                {r.window_start_ms}ms — score {r.composite_score.toFixed(1)} —{' '}
                <Badge ok={r.stt_ready} />
              </li>
            ))}
          </ul>
        </details>
      )}
    </section>
  );
}

function ScoreView({ m }: { m: WindowMetrics }) {
  return (
    <div>
      <div className="meter">
        <span style={{ width: `${Math.min(100, m.composite_score)}%` }} />
      </div>
      <p>
        Score: <strong>{m.composite_score.toFixed(1)}</strong> | STT-ready:{' '}
        <Badge ok={m.stt_ready} />
      </p>
      <ul className="metrics">
        <li>SNR: {m.snr_estimate_db.toFixed(1)} dB</li>
        <li>Silêncio: {(m.silence_ratio * 100).toFixed(0)}%</li>
        <li>Clipping: {(m.clipping_ratio * 100).toFixed(2)}%</li>
        <li>RMS: {m.rms_dbfs.toFixed(1)} dBFS</li>
      </ul>
    </div>
  );
}

function Badge({ ok }: { ok: boolean }) {
  return <span className={ok ? 'badge ok' : 'badge bad'}>{ok ? 'SIM' : 'NÃO'}</span>;
}
