import { useEffect, useState } from 'react';
import { checkReady, checkSttReady } from '../lib/api';
import type { ServiceHealth } from '../lib/types';

export function useServiceHealth(pollMs = 5000) {
  const [health, setHealth] = useState<ServiceHealth>({
    core: null,
    stt: null,
    lastCheck: null,
    error: null,
  });

  useEffect(() => {
    let cancelled = false;

    const poll = async () => {
      try {
        const [core, stt] = await Promise.all([checkReady(), checkSttReady()]);
        if (!cancelled) {
          setHealth({
            core,
            stt,
            lastCheck: Date.now(),
            error: null,
          });
        }
      } catch (e) {
        if (!cancelled) {
          setHealth((h) => ({
            ...h,
            lastCheck: Date.now(),
            error: e instanceof Error ? e.message : String(e),
          }));
        }
      }
    };

    poll();
    const id = setInterval(poll, pollMs);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [pollMs]);

  return health;
}
