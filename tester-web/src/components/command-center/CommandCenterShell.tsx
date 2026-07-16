import { useCallback, useEffect, useState, type ReactNode } from 'react';
import { MonitorView } from './views/MonitorView';
import { PipelineView } from './views/PipelineView';
import { AsrView } from './views/AsrView';
import { SessionsView } from './views/SessionsView';
import { ObservabilityView } from './views/ObservabilityView';
import { ConfigView } from './views/ConfigView';
import { NocSlideDeck } from './NocSlideDeck';
import { SessionSelector } from './SessionSelector';
import { CC_ROUTES, type CcRoute } from '../../lib/domain/cc-routes';
import { useCommandCenter } from '../../context/CommandCenterContext';

function routeView(route: CcRoute): ReactNode {
  switch (route) {
    case 'pipeline':
      return <PipelineView />;
    case 'asr':
      return <AsrView />;
    case 'sessions':
      return <SessionsView />;
    case 'observability':
      return <ObservabilityView />;
    case 'config':
      return <ConfigView />;
    default:
      return <MonitorView />;
  }
}

export function CommandCenterShell() {
  const {
    route,
    navigate,
    opsConnected,
    opsStatus,
    alerts,
    exportCsv,
    fullscreen,
    setFullscreen,
    rootRef,
    disconnectOps,
  } = useCommandCenter();
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);
  const [clock, setClock] = useState(() => new Date());

  useEffect(() => {
    const id = setInterval(() => setClock(new Date()), 1000);
    return () => clearInterval(id);
  }, []);

  const toggleFullscreen = useCallback(async () => {
    if (!document.fullscreenElement) {
      await rootRef.current?.requestFullscreen();
      setFullscreen(true);
    } else {
      await document.exitFullscreen();
      setFullscreen(false);
    }
  }, [rootRef, setFullscreen]);

  const nocMode = fullscreen;

  return (
    <div
      ref={rootRef}
      className={`cc-shell ${nocMode ? 'cc-fullscreen cc-noc-mode' : ''} ${sidebarCollapsed ? 'sidebar-collapsed' : ''}`}
    >
      {!nocMode && (
        <aside className="cc-sidebar">
          <div className="cc-sidebar-head">
            <strong>Command Center</strong>
            <button
              type="button"
              className="btn icon-btn"
              onClick={() => setSidebarCollapsed((v) => !v)}
              aria-label="Colapsar menu"
            >
              menu
            </button>
          </div>
          <nav className="cc-nav">
            {CC_ROUTES.map((r) => (
              <button
                key={r.id}
                type="button"
                className={`cc-nav-item ${route === r.id ? 'active' : ''}`}
                onClick={() => navigate(r.id)}
              >
                <span className="cc-nav-label">{r.label}</span>
                <span className="cc-nav-hint">{r.hint}</span>
              </button>
            ))}
          </nav>
        </aside>
      )}

      <div className="cc-main">
        <header className="cc-header cc-header-compact">
          <div>
            <h1>{nocMode ? 'NOC VoiceQAS' : CC_ROUTES.find((r) => r.id === route)?.label ?? 'Monitor'}</h1>
            {!nocMode && (
              <p className="muted">Ops WebSocket /v1/ops/stream - auto-connect, reconexao 10s</p>
            )}
          </div>
          <div className="cc-ops-controls">
            {!nocMode && <SessionSelector compact />}
            <span className={`badge ${opsConnected ? 'ok' : 'bad'}`}>{opsConnected ? 'LIVE' : 'OFF'}</span>
            <span className="muted cc-status-text">{opsStatus}</span>
            {nocMode && <span className="badge warn">{alerts.length} alertas</span>}
            {nocMode && <span className="muted cc-clock">{clock.toLocaleTimeString()}</span>}
            <button type="button" className="btn" onClick={exportCsv}>
              Export CSV
            </button>
            {import.meta.env.DEV && (
              <button type="button" className="btn danger" onClick={disconnectOps}>
                Desconectar WS
              </button>
            )}
            <button type="button" className="btn primary" onClick={toggleFullscreen}>
              {nocMode ? 'Sair NOC' : 'NOC fullscreen'}
            </button>
          </div>
        </header>

        {nocMode ? <NocSlideDeck /> : <main className="cc-content">{routeView(route)}</main>}
      </div>
    </div>
  );
}
