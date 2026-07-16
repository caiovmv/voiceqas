import { useEffect, useState } from 'react';
import { ensureDefaultOpsToken, getOpsToken, setOpsToken } from '../../lib/auth';
import { probeOpsRole, useCommandCenter } from '../../context/CommandCenterContext';

const DEV_PRESETS = [
  { id: 'dev-write', label: 'write (VAD, media)' },
  { id: 'dev-admin', label: 'admin (tudo)' },
  { id: 'dev-read', label: 'read (só leitura)' },
];

export function AuthPanel() {
  const [token, setToken] = useState(getOpsToken());
  const [saved, setSaved] = useState(false);
  const { bumpWsReconnect } = useCommandCenter();
  const role = probeOpsRole();
  const showPresets = import.meta.env.DEV || import.meta.env.VITE_CC_DEV_PRESETS === '1';

  useEffect(() => {
    ensureDefaultOpsToken();
    setToken(getOpsToken());
  }, []);

  const save = () => {
    setOpsToken(token.trim());
    bumpWsReconnect();
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  };

  const applyPreset = (preset: string) => {
    setToken(preset);
    setOpsToken(preset);
    bumpWsReconnect();
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  };

  return (
    <section className="panel cc-auth">
      <h2>Segurança ops</h2>
      <p className="muted">
        Envie <code>X-Ops-Token</code> nas chamadas REST e no ops WebSocket. Role inferido:{' '}
        <span className="badge">{role}</span>
      </p>
      <div className="row">
        <label className="cc-token-label">
          X-Ops-Token
          <input
            type="password"
            value={token}
            onChange={(e) => setToken(e.target.value)}
            placeholder="dev-write"
          />
        </label>
        <button type="button" className="btn primary" onClick={save}>
          Salvar
        </button>
        {saved && <span className="ok">Salvo</span>}
      </div>
      {showPresets && (
      <div className="row cc-auth-presets">
        {DEV_PRESETS.map((p) => (
          <button key={p.id} type="button" className="btn" onClick={() => applyPreset(p.id)}>
            {p.label}
          </button>
        ))}
      </div>
      )}
    </section>
  );
}
