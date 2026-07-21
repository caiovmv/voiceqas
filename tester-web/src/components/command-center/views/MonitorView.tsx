import { AlertsBar } from '../AlertsBar';
import { HealthStrip } from '../HealthStrip';
import { SessionGrid } from '../SessionGrid';
import { useCommandCenter } from '../../../context/CommandCenterContext';

export function MonitorView() {
  const { health, alerts, sessionList, opsStatus, opsConnected } = useCommandCenter();

  return (
    <>
      <HealthStrip health={health} />
      <AlertsBar alerts={alerts} />
      <section className="panel cc-sessions-panel">
        <div className="cc-panel-head">
          <h2>Overview ({sessionList.length} sessoes)</h2>
          <span className={`badge ${opsConnected ? 'ok' : 'bad'}`}>{opsStatus}</span>
        </div>
        <p className="muted cc-monitor-hint">
          Ops WebSocket conecta automaticamente. Pipeline: Decode, Strip, VAD, Diarization, VQA, STT.
        </p>
        <SessionGrid sessions={sessionList.slice(0, 6)} compact />
        {sessionList.length === 0 && (
          <p className="cc-empty muted">Aguardando trafego ou registre uma sessao media em Sessoes.</p>
        )}
      </section>
      <section className="panel cc-analysis-banner">
        <p>Calibre limiares e compare antes/depois na mesa de mix do Analysis Lab.</p>
        <a className="btn primary" href="#analysis">
          Abrir Analise
        </a>
      </section>
    </>
  );
}
