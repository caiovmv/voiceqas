import { useEffect, useState } from 'react';
import { ensureDefaultOpsToken, getOpsToken, setOpsToken } from '../../lib/auth';

const DEV_PRESETS = [
  { id: 'dev-write', label: 'write (VAD, media)' },
  { id: 'dev-admin', label: 'admin (tudo)' },
  { id: 'dev-read', label: 'read (só leitura)' },
];

export function AuthPanel() {
  const [token, setToken] = useState(getOpsToken());
  const [saved, setSaved] = useState(false);

  useEffect(() => {
    ensureDefaultOpsToken();
    setToken(getOpsToken());
  }, []);

  const save = () => {
    setOpsToken(token.trim());
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  };

  const applyPreset = (preset: string) => {
    setToken(preset);
    setOpsToken(preset);
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  };

  return (
    <section className="panel cc-auth">
      <h2>Auth ops</h2>
      <p className="muted">
        Envie <code>X-Ops-Token</code> nas chamadas REST e no ops WebSocket.{' '}
        <strong>Write</strong> ou <strong>admin</strong> são necessários para VAD, media e mutações.
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
      <div className="row cc-auth-presets">
        {DEV_PRESETS.map((p) => (
          <button key={p.id} type="button" className="btn" onClick={() => applyPreset(p.id)}>
            {p.label}
          </button>
        ))}
      </div>
    </section>
  );
}
