import type { ServiceHealth } from '../../lib/types';

function coreOk(health: ServiceHealth): boolean {
  const status = health.core?.status;
  if (status === 'ok' || status === 'ready') return true;
  return health.core?.ready === true;
}

function sttOk(health: ServiceHealth): boolean {
  if (health.stt?.status === 'ok') return true;
  const models = health.stt?.models;
  return !!models?.some((m) => m.ready);
}

export function HealthStrip({ health }: { health: ServiceHealth }) {
  const core = coreOk(health);
  const stt = sttOk(health);
  const checked = health.lastCheck
    ? new Date(health.lastCheck).toLocaleTimeString()
    : '—';

  return (
    <section className="panel cc-health">
      <div className="cc-health-cards">
        <div className={`cc-health-card ${core ? 'ok' : 'bad'}`}>
          <span className="cc-health-label">Core</span>
          <strong>{core ? 'OK' : 'DOWN'}</strong>
          <span className="muted">/ready</span>
        </div>
        <div className={`cc-health-card ${stt ? 'ok' : 'bad'}`}>
          <span className="cc-health-label">STT</span>
          <strong>{stt ? 'OK' : 'DOWN'}</strong>
          <span className="muted">/v1/stt/ready</span>
        </div>
        <div className="cc-health-card neutral">
          <span className="cc-health-label">Último poll</span>
          <strong>{checked}</strong>
          {health.error && <span className="error">{health.error}</span>}
        </div>
      </div>
      {health.stt?.models && (
        <ul className="cc-model-list">
          {health.stt.models.map((m) => (
            <li key={m.id}>
              <span>{m.name ?? m.id}</span>
              <span className={`badge ${m.ready ? 'ok' : 'bad'}`}>{m.ready ? 'ready' : 'n/a'}</span>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
