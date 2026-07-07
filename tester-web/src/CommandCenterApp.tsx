import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { AlertsBar } from './components/command-center/AlertsBar';
import { AuthPanel } from './components/command-center/AuthPanel';
import { HealthStrip } from './components/command-center/HealthStrip';
import { HistoryPanel } from './components/command-center/HistoryPanel';
import { MediaSessionPanel } from './components/command-center/MediaSessionPanel';
import { VadPanel } from './components/command-center/VadPanel';
import { SessionGrid } from './components/command-center/SessionGrid';
import { useOpsStream } from './hooks/useOpsStream';
import { useServiceHealth } from './hooks/useServiceHealth';
import { listMediaSessions } from './lib/api';
import { ensureDefaultOpsToken } from './lib/auth';
import { upsertSession } from './lib/domain/session-merge';
import { computeAlerts, exportSessionsCsv, sessionAlerts } from './lib/domain/ops-alerts';
import type { TrackedSession } from './lib/types';

export function CommandCenterApp() {
  const rootRef = useRef<HTMLDivElement>(null);
  const health = useServiceHealth(5000);
  const [filterSessionId, setFilterSessionId] = useState('');
  const [sessions, setSessions] = useState<Map<string, TrackedSession>>(new Map());
  const [opsStatus, setOpsStatus] = useState('Desconectado');
  const [opsConnected, setOpsConnected] = useState(false);
  const [streamAlerts, setStreamAlerts] = useState<import('./lib/types').SessionAlert[]>([]);
  const [fullscreen, setFullscreen] = useState(false);

  useEffect(() => {
    ensureDefaultOpsToken();
  }, []);

  const onVqaWindow = useCallback((sessionId: string, metrics: TrackedSession['latest']) => {
    if (!metrics) return;
    setSessions((prev) => upsertSession(prev, sessionId, { latest: metrics }));
  }, []);

  const onSttEvent = useCallback((sessionId: string, ev: import('./lib/types').OpsEvent) => {
    if (ev.type === 'stt_final' || ev.type === 'stt_partial') {
      setSessions((prev) => upsertSession(prev, sessionId, { lastStt: ev }));
    }
  }, []);

  const onAlert = useCallback((sessionId: string, ev: import('./lib/types').OpsEvent) => {
    if (ev.type !== 'alert') return;
    const message =
      ev.message ??
      (ev.alert_kind === 'codec_suboptimal'
        ? `Codec subótimo: ${ev.actual_codec ?? '?'} (ideal ${ev.preferred_codec ?? 'rtp_g722'})`
        : ev.alert_kind);
    setStreamAlerts((prev) => {
      if (prev.some((a) => a.sessionId === sessionId && a.kind === ev.alert_kind)) {
        return prev;
      }
      return [...prev, { sessionId, kind: ev.alert_kind, message }];
    });
  }, []);

  const { connect, disconnect, connected } = useOpsStream({
    filterSessionId: filterSessionId.trim() || undefined,
    onVqaWindow,
    onSttEvent,
    onAlert,
    onStatus: setOpsStatus,
    autoReconnect: true,
  });

  useEffect(() => {
    setOpsConnected(connected);
  }, [connected]);

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
      /* media API may require token */
    }
  }, []);

  useEffect(() => {
    refreshMediaSessions();
    const id = setInterval(refreshMediaSessions, 10_000);
    return () => clearInterval(id);
  }, [refreshMediaSessions]);

  const sessionList = useMemo(() => {
    const list = [...sessions.values()].sort((a, b) => b.lastSeen - a.lastSeen);
    return computeAlerts(list);
  }, [sessions]);

  const alerts = useMemo(
    () => [...sessionAlerts(sessionList), ...streamAlerts],
    [sessionList, streamAlerts],
  );

  const onMediaRegistered = useCallback((sessionId: string, meta: TrackedSession['mediaMeta']) => {
    setSessions((prev) => upsertSession(prev, sessionId, { mediaMeta: meta }));
    refreshMediaSessions();
  }, [refreshMediaSessions]);

  const onMediaClosed = useCallback((sessionId: string) => {
    setSessions((prev) => {
      const next = new Map(prev);
      next.delete(sessionId);
      return next;
    });
    refreshMediaSessions();
  }, [refreshMediaSessions]);

  const toggleFullscreen = async () => {
    if (!document.fullscreenElement) {
      await rootRef.current?.requestFullscreen();
      setFullscreen(true);
    } else {
      await document.exitFullscreen();
      setFullscreen(false);
    }
  };

  return (
    <div ref={rootRef} className={fullscreen ? 'cc-fullscreen' : undefined}>
      <header className="cc-header">
        <div>
          <h1>Command Center</h1>
          <p className="muted">
            Ops WebSocket <code>/v1/ops/stream</code> — VQA + STT em tempo real (UDP, WS, REST).
          </p>
        </div>
        <div className="cc-ops-controls">
          <label>
            Filtro session_id
            <input
              value={filterSessionId}
              onChange={(e) => setFilterSessionId(e.target.value)}
              placeholder="vazio = todas"
            />
          </label>
          {!opsConnected ? (
            <button type="button" className="btn primary" onClick={connect}>
              Conectar ops WS
            </button>
          ) : (
            <button type="button" className="btn danger" onClick={disconnect}>
              Desconectar
            </button>
          )}
          <button type="button" className="btn" onClick={() => exportSessionsCsv(sessionList)}>
            Export CSV
          </button>
          <button type="button" className="btn" onClick={toggleFullscreen}>
            {fullscreen ? 'Sair NOC' : 'NOC fullscreen'}
          </button>
          <span className={`badge ${opsConnected ? 'ok' : 'bad'}`}>
            {opsConnected ? 'LIVE' : 'OFF'}
          </span>
        </div>
      </header>

      <AuthPanel />
      <HealthStrip health={health} />
      <VadPanel />
      <AlertsBar alerts={alerts} />

      <div className="cc-grid">
        <MediaSessionPanel
          onRegistered={onMediaRegistered}
          onClosed={onMediaClosed}
          onRefresh={refreshMediaSessions}
        />
        <section className="panel cc-sessions-panel">
          <div className="cc-panel-head">
            <h2>Sessões ao vivo ({sessionList.length})</h2>
            <span className="muted">{opsStatus}</span>
          </div>
          <SessionGrid sessions={sessionList} />
        </section>
      </div>
      <div className="cc-grid cc-grid-bottom">
        <HistoryPanel sessionId={filterSessionId.trim() || undefined} />
      </div>
    </div>
  );
}
