import { useEffect, useState } from 'react';
import { fetchPipelineSessions } from '../../lib/api';
import { useCommandCenter } from '../../context/CommandCenterContext';

export function SessionSelector({ compact = false }: { compact?: boolean }) {
  const { activeSessionId, setActiveSessionId } = useCommandCenter();
  const [sessionIds, setSessionIds] = useState<string[]>([]);

  useEffect(() => {
    fetchPipelineSessions()
      .then((res) => {
        const ids = res.session_ids ?? res.active_sessions?.map((s) => s.session_id) ?? [];
        setSessionIds(ids);
      })
      .catch(() => setSessionIds([]));
    const id = setInterval(() => {
      fetchPipelineSessions()
        .then((res) => {
          const ids = res.session_ids ?? res.active_sessions?.map((s) => s.session_id) ?? [];
          setSessionIds(ids);
        })
        .catch(() => undefined);
    }, 10_000);
    return () => clearInterval(id);
  }, []);

  return (
    <label className={compact ? 'cc-session-select compact' : 'cc-session-select'}>
      Sessao
      <select value={activeSessionId} onChange={(e) => setActiveSessionId(e.target.value)}>
        <option value="">Todas</option>
        {sessionIds.map((id) => (
          <option key={id} value={id}>
            {id}
          </option>
        ))}
      </select>
    </label>
  );
}
