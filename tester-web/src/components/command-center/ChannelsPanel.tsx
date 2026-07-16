import { useCallback, useEffect, useState } from 'react';
import {
  createChannel,
  deleteChannel,
  fetchChannels,
  type ChannelProfile,
  type ChannelTransport,
} from '../../lib/api/config';
import { probeOpsRole } from '../../context/CommandCenterContext';

const TRANSPORTS: { id: ChannelTransport; label: string }[] = [
  { id: 'sip_trunk', label: 'SIP Trunk' },
  { id: 'websocket', label: 'WebSocket' },
  { id: 'webrtc', label: 'WebRTC' },
  { id: 'rest', label: 'REST' },
  { id: 'grpc', label: 'gRPC' },
];

export function ChannelsPanel() {
  const [channels, setChannels] = useState<ChannelProfile[]>([]);
  const [defaultId, setDefaultId] = useState('default');
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [draft, setDraft] = useState<Partial<ChannelProfile> | null>(null);
  const readOnly = probeOpsRole() === 'read';

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const res = await fetchChannels();
      setChannels(res.channels);
      setDefaultId(res.default_channel_id);
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const startNew = () => {
    setDraft({
      id: '',
      label: '',
      transport: 'rest',
      enabled: true,
      inherit_pipeline: true,
    });
  };

  const saveDraft = async () => {
    if (!draft?.id?.trim()) return;
    setError(null);
    try {
      await createChannel({
        id: draft.id.trim(),
        label: draft.label || draft.id,
        transport: draft.transport ?? 'rest',
        enabled: draft.enabled ?? true,
        inherit_pipeline: draft.inherit_pipeline ?? true,
        media: draft.media,
        pipeline_override: draft.pipeline_override,
        stt_override: draft.stt_override,
      });
      setDraft(null);
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const remove = async (id: string) => {
    if (!confirm(`Remover canal ${id}?`)) return;
    try {
      await deleteChannel(id);
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  return (
    <section className="panel cc-channels">
      <div className="cc-panel-head">
        <div>
          <h2>Canais de ingresso</h2>
          <p className="muted">
            SIP trunk, WebSocket, WebRTC, REST ou gRPC. Default: <code>{defaultId}</code>
          </p>
        </div>
        {!readOnly && (
          <button type="button" className="btn primary" onClick={startNew}>
            Novo canal
          </button>
        )}
      </div>

      {loading && <p className="muted">Carregando canais...</p>}
      {error && <p className="error">{error}</p>}

      {draft && (
        <div className="cc-channel-wizard panel nested">
          <h3>Novo canal</h3>
          <div className="row">
            <label>
              ID
              <input value={draft.id} onChange={(e) => setDraft({ ...draft, id: e.target.value })} />
            </label>
            <label>
              Nome
              <input value={draft.label ?? ''} onChange={(e) => setDraft({ ...draft, label: e.target.value })} />
            </label>
            <label>
              Transporte
              <select
                value={draft.transport ?? 'rest'}
                onChange={(e) => setDraft({ ...draft, transport: e.target.value as ChannelTransport })}
              >
                {TRANSPORTS.map((t) => (
                  <option key={t.id} value={t.id}>
                    {t.label}
                  </option>
                ))}
              </select>
            </label>
          </div>
          {(draft.transport === 'sip_trunk' || draft.transport === 'webrtc') && (
            <p className="badge warn">Signaling pendente - canal registravel; midia requer stack futura.</p>
          )}
          <label className="checkbox-row">
            <input
              type="checkbox"
              checked={draft.inherit_pipeline ?? true}
              onChange={(e) => setDraft({ ...draft, inherit_pipeline: e.target.checked })}
            />
            Herdar pipeline default
          </label>
          <div className="row">
            <button type="button" className="btn primary" onClick={() => void saveDraft()}>
              Criar
            </button>
            <button type="button" className="btn" onClick={() => setDraft(null)}>
              Cancelar
            </button>
          </div>
        </div>
      )}

      <div className="cc-channel-grid">
        {channels.map((ch) => (
          <article key={ch.id} className="cc-channel-card">
            <header>
              <strong>{ch.label}</strong>
              <code>{ch.id}</code>
            </header>
            <ul className="metrics">
              <li>
                Transporte <span className="badge">{ch.transport}</span>
              </li>
              <li>
                Status <span className={`badge ${ch.enabled ? 'ok' : 'bad'}`}>{ch.enabled ? 'ativo' : 'off'}</span>
              </li>
              {ch.has_override && <li className="badge warn">override pipeline</li>}
              {ch.signaling === 'pending' && <li className="badge warn">signaling pending</li>}
              {ch.media?.preferred_ingress_codec && <li>Codec {ch.media.preferred_ingress_codec}</li>}
            </ul>
            {!readOnly && ch.id !== defaultId && (
              <button type="button" className="btn danger" onClick={() => void remove(ch.id)}>
                Remover
              </button>
            )}
          </article>
        ))}
      </div>
    </section>
  );
}
