import { useEffect, useState } from 'react';
import { checkSttReady, setVadModel } from '../../lib/api';

const VAD_OPTIONS = [
  { id: 'auto', label: 'auto (k2fsa preferido)' },
  { id: 'k2fsa', label: 'k2fsa — silero_vad.onnx' },
  { id: 'k2fsa-int8', label: 'k2fsa-int8 — quantizado' },
  { id: 'v5', label: 'v5 — snakers4' },
  { id: 'v4', label: 'v4 — snakers4 legado' },
];

export function VadPanel() {
  const [activeId, setActiveId] = useState('—');
  const [selected, setSelected] = useState('auto');
  const [status, setStatus] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const refresh = async () => {
    try {
      const ready = await checkSttReady();
      const active = ready.models?.find((m) => m.active || m.id === 'vad');
      const activeVad = ready.models?.find((m) => m.id.startsWith('vad:') && m.active);
      setActiveId(activeVad?.id.replace('vad:', '') ?? active?.active_id ?? '—');
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  useEffect(() => {
    refresh();
  }, []);

  const apply = async () => {
    setStatus(null);
    setError(null);
    try {
      const res = await setVadModel(selected);
      setActiveId(res.active_id);
      setStatus(`VAD ativo: ${res.active_name}`);
      await refresh();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  return (
    <section className="panel cc-vad">
      <h2>Silero VAD</h2>
      <p className="muted">
        Troca o modelo VAD em runtime (requer token write). Default produção: <code>k2fsa</code>.
      </p>
      <div className="row">
        <label>
          Modelo
          <select value={selected} onChange={(e) => setSelected(e.target.value)}>
            {VAD_OPTIONS.map((o) => (
              <option key={o.id} value={o.id}>
                {o.label}
              </option>
            ))}
          </select>
        </label>
        <button type="button" className="btn primary" onClick={apply}>
          Aplicar
        </button>
        <button type="button" className="btn" onClick={refresh}>
          Atualizar
        </button>
      </div>
      <p className="muted">
        Ativo: <strong>{activeId}</strong>
      </p>
      {status && <p className="ok">{status}</p>}
      {error && <p className="error">{error}</p>}
    </section>
  );
}
