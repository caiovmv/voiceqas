import { useCallback, useEffect, useState } from 'react';
import {
  createMediaSession,
  deleteMediaSession,
  listMediaSessions,
  sendAgentPcm,
} from '../../lib/api';
import { floatToInt16 } from '../../lib/audio';
import type { AudioFormat, MediaSessionMeta } from '../../lib/types';

const FORMATS: AudioFormat[] = ['rtp_g722', 'rtp_pcmu', 'rtp_pcma', 'rtp_g729'];

interface Props {
  onRegistered: (sessionId: string, meta: MediaSessionMeta) => void;
  onClosed: (sessionId: string) => void;
  onRefresh?: () => void;
}

export function MediaSessionPanel({ onRegistered, onClosed, onRefresh }: Props) {
  const [sessionId, setSessionId] = useState(`call-${Date.now()}`);
  const [format, setFormat] = useState<AudioFormat>('rtp_g722');
  const [remoteHost, setRemoteHost] = useState('');
  const [remotePort, setRemotePort] = useState('5060');
  const [inboundHost, setInboundHost] = useState('');
  const [inboundPort, setInboundPort] = useState('');
  const [serverSessions, setServerSessions] = useState<MediaSessionMeta[]>([]);
  const [agentSessionId, setAgentSessionId] = useState('');
  const [status, setStatus] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  const loadServer = useCallback(async () => {
    try {
      const res = await listMediaSessions();
      setServerSessions(res.sessions);
      onRefresh?.();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }, [onRefresh]);

  useEffect(() => {
    loadServer();
  }, [loadServer]);

  const register = async () => {
    setLoading(true);
    setError(null);
    setStatus('');
    try {
      const port = remotePort ? Number(remotePort) : undefined;
      const inPort = inboundPort ? Number(inboundPort) : undefined;
      const res = await createMediaSession({
        session_id: sessionId,
        format,
        remote_host: remoteHost || undefined,
        remote_port: port,
        inbound_host: inboundHost || undefined,
        inbound_port: inPort,
      });
      const meta: MediaSessionMeta = {
        session_id: res.session_id,
        format,
        sample_rate: res.sample_rate,
        remote_host: remoteHost || undefined,
        remote_port: port,
        inbound_host: inboundHost || undefined,
        inbound_port: inPort,
      };
      onRegistered(meta.session_id, meta);
      setStatus(`Sessão ${res.session_id} registrada`);
      await loadServer();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  };

  const closeSession = async (id: string) => {
    setLoading(true);
    setError(null);
    try {
      await deleteMediaSession(id);
      onClosed(id);
      setStatus(`Sessão ${id} encerrada`);
      await loadServer();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  };

  const sendAgentWav = async (file: File) => {
    if (!agentSessionId) {
      setError('Selecione session_id para agent-audio');
      return;
    }
    setLoading(true);
    setError(null);
    try {
      const buf = await file.arrayBuffer();
      const ctx = new AudioContext();
      const audio = await ctx.decodeAudioData(buf);
      const pcm = floatToInt16(audio.getChannelData(0));
      const result = await sendAgentPcm(agentSessionId, pcm, audio.sampleRate);
      setStatus(`Agent audio: ${result.rtp_packets} pacotes, ${result.bytes_sent} bytes`);
      await ctx.close();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  };

  return (
    <section className="panel">
      <h2>Media relay</h2>
      <p className="muted">
        REST + bind UDP. Codec preferido de entrada: <strong>G.722</strong> (wideband 16 kHz). O core
        autodetecta pelo RTP PT; alerta se o codec real não for o ideal.
      </p>

      <div className="grid-2">
        <label>
          session_id
          <input value={sessionId} onChange={(e) => setSessionId(e.target.value)} />
        </label>
        <label>
          Codec
          <select value={format} onChange={(e) => setFormat(e.target.value as AudioFormat)}>
            {FORMATS.map((f) => (
              <option key={f} value={f}>
                {f}
              </option>
            ))}
          </select>
        </label>
        <label>
          remote_host (egress)
          <input value={remoteHost} onChange={(e) => setRemoteHost(e.target.value)} />
        </label>
        <label>
          remote_port
          <input value={remotePort} onChange={(e) => setRemotePort(e.target.value)} />
        </label>
        <label>
          inbound_host
          <input value={inboundHost} onChange={(e) => setInboundHost(e.target.value)} />
        </label>
        <label>
          inbound_port
          <input value={inboundPort} onChange={(e) => setInboundPort(e.target.value)} />
        </label>
      </div>

      <div className="row">
        <button type="button" className="btn primary" disabled={loading} onClick={register}>
          Registrar
        </button>
        <button type="button" className="btn" disabled={loading} onClick={loadServer}>
          Atualizar lista
        </button>
      </div>

      <h3 className="subhead">Agent audio (egress)</h3>
      <div className="row">
        <label>
          session_id
          <select value={agentSessionId} onChange={(e) => setAgentSessionId(e.target.value)}>
            <option value="">—</option>
            {serverSessions.map((s) => (
              <option key={s.session_id} value={s.session_id}>
                {s.session_id}
              </option>
            ))}
          </select>
        </label>
        <label className="btn file">
          Enviar WAV
          <input
            type="file"
            accept="audio/*"
            hidden
            onChange={(e) => {
              const f = e.target.files?.[0];
              if (f) void sendAgentWav(f);
            }}
          />
        </label>
      </div>

      {error && <p className="error">{error}</p>}
      {status && <p className="ok">{status}</p>}

      {serverSessions.length > 0 && (
        <>
          <h3 className="subhead">Servidor ({serverSessions.length})</h3>
          <ul className="variant-list">
            {serverSessions.map((s) => (
              <li key={s.session_id} className="variant-item">
                <span>
                  <code>{s.session_id}</code>
                  {s.inbound_host ? ` ← ${s.inbound_host}:${s.inbound_port}` : ''}
                  {s.remote_host ? ` → ${s.remote_host}:${s.remote_port}` : ''}
                </span>
                <button
                  type="button"
                  className="btn sm danger"
                  disabled={loading}
                  onClick={() => closeSession(s.session_id)}
                >
                  Encerrar
                </button>
              </li>
            ))}
          </ul>
        </>
      )}
    </section>
  );
}
