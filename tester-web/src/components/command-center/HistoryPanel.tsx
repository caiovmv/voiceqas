import { useEffect, useState } from 'react';
import { fetchOpsHistory } from '../../lib/api';
import type { OpsEvent } from '../../lib/types';

export function HistoryPanel({ sessionId }: { sessionId?: string }) {
  const [events, setEvents] = useState<OpsEvent[]>([]);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    fetchOpsHistory({ sessionId, limit: 100 })
      .then((r) => setEvents(r.events))
      .catch((e) => setError(e instanceof Error ? e.message : String(e)));
    const id = setInterval(() => {
      fetchOpsHistory({ sessionId, limit: 100 })
        .then((r) => setEvents(r.events))
        .catch(() => undefined);
    }, 15_000);
    return () => clearInterval(id);
  }, [sessionId]);

  return (
    <section className="panel">
      <h2>Histórico persistido</h2>
      <p className="muted">JSONL server-side — últimos eventos via GET /v1/ops/metrics/history</p>
      {error && <p className="error">{error}</p>}
      <pre className="log cc-history-log">
        {events.length === 0
          ? 'Sem eventos (ou token read ausente)'
          : events
              .slice(-20)
              .map((e) => JSON.stringify(e))
              .join('\n')}
      </pre>
    </section>
  );
}
