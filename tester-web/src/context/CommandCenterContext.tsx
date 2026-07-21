import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from 'react';
import { useOpsStream } from '../hooks/useOpsStream';
import { usePipelineSnapshot } from '../hooks/usePipelineSnapshot';
import { useServiceHealth } from '../hooks/useServiceHealth';
import { listMediaSessions } from '../lib/api';
import { ensureDefaultOpsToken, getOpsToken } from '../lib/auth';
import { ccHashFor, parseCcRoute, type CcRoute } from '../lib/domain/cc-routes';
import { computeAlerts, exportSessionsCsv, sessionAlerts } from '../lib/domain/ops-alerts';
import { upsertSession } from '../lib/domain/session-merge';
import type { OpsAlertEvent, SessionAlert, TrackedSession } from '../lib/types';

interface CommandCenterContextValue {
  route: CcRoute;
  navigate: (route: CcRoute) => void;
  activeSessionId: string;
  setActiveSessionId: (id: string) => void;
  sessions: TrackedSession[];
  sessionList: TrackedSession[];
  alerts: SessionAlert[];
  opsStatus: string;
  opsConnected: boolean;
  disconnectOps: () => void;
  health: ReturnType<typeof useServiceHealth>;
  snapshot: ReturnType<typeof usePipelineSnapshot>['snapshot'];
  pipelineLoading: boolean;
  pipelineError: string | null;
  refreshMediaSessions: () => Promise<void>;
  onMediaRegistered: (sessionId: string, meta: TrackedSession['mediaMeta']) => void;
  onMediaClosed: (sessionId: string) => void;
  exportCsv: () => void;
  fullscreen: boolean;
  setFullscreen: (v: boolean) => void;
  rootRef: React.RefObject<HTMLDivElement | null>;
  wsReconnectToken: number;
  bumpWsReconnect: () => void;
}

const CommandCenterContext = createContext<CommandCenterContextValue | null>(null);

export function CommandCenterProvider({ children }: { children: ReactNode }) {
  const rootRef = useRef<HTMLDivElement>(null);
  const health = useServiceHealth(5000);
  const [route, setRoute] = useState<CcRoute>(() => parseCcRoute(location.hash));
  const [activeSessionId, setActiveSessionId] = useState('');
  const [sessions, setSessions] = useState<Map<string, TrackedSession>>(new Map());
  const [opsStatus, setOpsStatus] = useState('Conectando...');
  const [streamAlerts, setStreamAlerts] = useState<SessionAlert[]>([]);
  const [fullscreen, setFullscreen] = useState(false);
  const [wsReconnectToken, setWsReconnectToken] = useState(0);

  useEffect(() => {
    ensureDefaultOpsToken();
  }, []);

  useEffect(() => {
    const onHash = () => setRoute(parseCcRoute(location.hash));
    window.addEventListener('hashchange', onHash);
    return () => window.removeEventListener('hashchange', onHash);
  }, []);

  useEffect(() => {
    const onFs = () => setFullscreen(Boolean(document.fullscreenElement));
    document.addEventListener('fullscreenchange', onFs);
    return () => document.removeEventListener('fullscreenchange', onFs);
  }, []);

  const navigate = useCallback((next: CcRoute) => {
    location.hash = ccHashFor(next);
    setRoute(next);
  }, []);

  const bumpWsReconnect = useCallback(() => {
    setWsReconnectToken((t) => t + 1);
  }, []);

  const onVqaWindow = useCallback((sessionId: string, metrics: TrackedSession['latest']) => {
    if (!metrics) return;
    setSessions((prev) => upsertSession(prev, sessionId, { latest: metrics }));
  }, []);

  const onSttEvent = useCallback((sessionId: string, ev: import('../lib/types').OpsEvent) => {
    if (ev.type === 'stt_final' || ev.type === 'stt_partial') {
      setSessions((prev) => upsertSession(prev, sessionId, { lastStt: ev }));
    }
  }, []);

  const onAlert = useCallback((sessionId: string, ev: import('../lib/types').OpsEvent) => {
    if (ev.type !== 'alert') return;
    const message =
      ev.message ??
      (ev.alert_kind === 'codec_suboptimal'
        ? `Codec subotimo: ${ev.actual_codec ?? '?'} (ideal ${ev.preferred_codec ?? 'rtp_g722'})`
        : ev.alert_kind);
    setStreamAlerts((prev) => {
      if (prev.some((a) => a.sessionId === sessionId && a.kind === ev.alert_kind)) {
        return prev;
      }
      return [
        ...prev,
        { sessionId, kind: ev.alert_kind as OpsAlertEvent['alert_kind'], message },
      ];
    });
  }, []);

  const filterId = activeSessionId.trim() || undefined;

  const { snapshot, error, loading, applyWsSnapshot } = usePipelineSnapshot({
    sessionId: filterId,
    pollMs: 3000,
  });

  const onPipelineSnapshot = useCallback(
    (data: import('../lib/types').PipelineSnapshot) => {
      applyWsSnapshot(data);
    },
    [applyWsSnapshot],
  );

  const { disconnect, connected } = useOpsStream({
    filterSessionId: filterId,
    onVqaWindow,
    onSttEvent,
    onAlert,
    onPipelineSnapshot,
    onStatus: setOpsStatus,
    autoConnect: true,
    autoReconnect: true,
    reconnectToken: wsReconnectToken,
  });

  const refreshMediaSessions = useCallback(async () => {
    try {
      const res = await listMediaSessions();
      setSessions((prev) => {
        let next = prev;
        for (const meta of res.sessions) {
          next = upsertSession(next, meta.session_id, { mediaMeta: meta });
        }
        return next;
      });
    } catch {
      /* optional auth */
    }
  }, []);

  useEffect(() => {
    refreshMediaSessions();
    const id = setInterval(refreshMediaSessions, 10_000);
    return () => clearInterval(id);
  }, [refreshMediaSessions]);

  useEffect(() => {
    const onStorage = (e: StorageEvent) => {
      if (e.key === 'voiceqas_ops_token') {
        bumpWsReconnect();
      }
    };
    window.addEventListener('storage', onStorage);
    return () => window.removeEventListener('storage', onStorage);
  }, [bumpWsReconnect]);

  const sessionList = useMemo(() => {
    const list = [...sessions.values()].sort((a, b) => b.lastSeen - a.lastSeen);
    return computeAlerts(list);
  }, [sessions]);

  const alerts = useMemo(
    () => [...sessionAlerts(sessionList), ...streamAlerts] as SessionAlert[],
    [sessionList, streamAlerts],
  );

  const onMediaRegistered = useCallback(
    (sessionId: string, meta: TrackedSession['mediaMeta']) => {
      setSessions((prev) => upsertSession(prev, sessionId, { mediaMeta: meta }));
      refreshMediaSessions();
    },
    [refreshMediaSessions],
  );

  const onMediaClosed = useCallback(
    (sessionId: string) => {
      setSessions((prev) => {
        const next = new Map(prev);
        next.delete(sessionId);
        return next;
      });
      refreshMediaSessions();
    },
    [refreshMediaSessions],
  );

  const exportCsv = useCallback(() => exportSessionsCsv(sessionList), [sessionList]);

  const value = useMemo<CommandCenterContextValue>(
    () => ({
      route,
      navigate,
      activeSessionId,
      setActiveSessionId,
      sessions: sessionList,
      sessionList,
      alerts,
      opsStatus,
      opsConnected: connected,
      disconnectOps: disconnect,
      health,
      snapshot,
      pipelineLoading: loading,
      pipelineError: error,
      refreshMediaSessions,
      onMediaRegistered,
      onMediaClosed,
      exportCsv,
      fullscreen,
      setFullscreen,
      rootRef,
      wsReconnectToken,
      bumpWsReconnect,
    }),
    [
      route,
      navigate,
      activeSessionId,
      sessionList,
      alerts,
      opsStatus,
      connected,
      disconnect,
      health,
      snapshot,
      loading,
      error,
      refreshMediaSessions,
      onMediaRegistered,
      onMediaClosed,
      exportCsv,
      fullscreen,
      wsReconnectToken,
      bumpWsReconnect,
    ],
  );

  return <CommandCenterContext.Provider value={value}>{children}</CommandCenterContext.Provider>;
}

export function useCommandCenter(): CommandCenterContextValue {
  const ctx = useContext(CommandCenterContext);
  if (!ctx) {
    throw new Error('useCommandCenter must be used within CommandCenterProvider');
  }
  return ctx;
}

export function probeOpsRole(): 'read' | 'write' | 'admin' | 'unknown' {
  const token = getOpsToken();
  if (token.includes('admin')) return 'admin';
  if (token.includes('write')) return 'write';
  if (token.includes('read')) return 'read';
  return 'unknown';
}
