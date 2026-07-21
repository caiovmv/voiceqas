import { useCallback, useEffect, useRef, useState } from 'react';
import { fetchPipelineSnapshot } from '../lib/api';
import type { PipelineSnapshot } from '../lib/types';

interface UsePipelineSnapshotOptions {
  sessionId?: string;
  pollMs?: number;
  enabled?: boolean;
}

export function usePipelineSnapshot({
  sessionId,
  pollMs = 3000,
  enabled = true,
}: UsePipelineSnapshotOptions) {
  const [snapshot, setSnapshot] = useState<PipelineSnapshot | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const sessionRef = useRef(sessionId);
  sessionRef.current = sessionId;

  const refresh = useCallback(async () => {
    if (!enabled) return;
    try {
      const data = await fetchPipelineSnapshot(sessionRef.current?.trim() || undefined);
      setSnapshot(data);
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }, [enabled]);

  const applyWsSnapshot = useCallback((data: PipelineSnapshot) => {
    const filter = sessionRef.current?.trim();
    if (filter && data.session_id && data.session_id !== filter && data.scope === 'session') {
      return;
    }
    setSnapshot(data);
    setError(null);
    setLoading(false);
  }, []);

  useEffect(() => {
    setLoading(true);
    refresh();
    if (!enabled) return;
    const id = window.setInterval(refresh, pollMs);
    return () => window.clearInterval(id);
  }, [enabled, pollMs, refresh, sessionId]);

  return { snapshot, error, loading, refresh, applyWsSnapshot };
}
