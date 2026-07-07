import { useEffect, useState } from 'react';
import type { CodecConfig, SttProvider } from '../lib/types';
import { checkSttReady } from '../lib/api';
import {
  RECORD_QUALITY_OPTIONS,
  configForRecordQuality,
  recordQualityIdForConfig,
  type RecordQualityId,
} from '../lib/types';

interface Props {
  config: CodecConfig;
  onChange: (patch: Partial<CodecConfig>) => void;
}

export function CodecConfigPanel({ config, onChange }: Props) {
  const qualityId = recordQualityIdForConfig(config);
  const [providersAvailable, setProvidersAvailable] = useState<SttProvider[]>(['cpu']);
  const [cudaCompiled, setCudaCompiled] = useState(false);
  const [serverProvider, setServerProvider] = useState('cpu');

  useEffect(() => {
    checkSttReady()
      .then((ready) => {
        const available = (ready.providers_available ?? ['cpu']).filter(
          (p): p is SttProvider => p === 'cpu' || p === 'cuda',
        );
        setProvidersAvailable(available.length > 0 ? available : ['cpu']);
        setCudaCompiled(Boolean(ready.cuda_compiled));
        if (ready.provider) {
          setServerProvider(ready.provider);
        }
      })
      .catch(() => {
        setProvidersAvailable(['cpu']);
      });
  }, []);

  const setQuality = (id: RecordQualityId) => {
    onChange(configForRecordQuality(id));
  };

  return (
    <section className="panel">
      <h2>Codec & simulação SIP</h2>
      <p className="muted">
        Escolha como o áudio gravado será simulado no tronco SIP. STT sempre recebe PCM 16 kHz após
        decode.
      </p>

      <fieldset className="radio-group">
        <legend>Qualidade de gravação / transporte</legend>
        {RECORD_QUALITY_OPTIONS.map((opt) => (
          <label key={opt.id} className="radio-option">
            <input
              type="radio"
              name="record-quality"
              value={opt.id}
              checked={qualityId === opt.id}
              onChange={() => setQuality(opt.id)}
            />
            <span className="radio-option-body">
              <strong>{opt.label}</strong>
              <span className="muted">{opt.description}</span>
            </span>
          </label>
        ))}
      </fieldset>

      <div className="grid-2">
        <label>
          Frame RTP (ms)
          <select
            value={config.frameMs}
            onChange={(e) =>
              onChange({ frameMs: Number(e.target.value) as 20 | 40 | 60 })
            }
          >
            <option value={20}>20 ms</option>
            <option value={40}>40 ms</option>
            <option value={60}>60 ms</option>
          </select>
        </label>
        <label>
          Session ID
          <input
            value={config.sessionId}
            onChange={(e) => onChange({ sessionId: e.target.value })}
          />
          <span className="field-hint">Novo ID a cada gravação ou arquivo carregado</span>
        </label>
        <label>
          Modelo STT
          <select
            value={config.sttModel}
            onChange={(e) => onChange({ sttModel: e.target.value as CodecConfig['sttModel'] })}
          >
            <option value="auto">auto (Parakeet → Whisper)</option>
            <option value="parakeet">parakeet v3 int8</option>
            <option value="whisper">whisper large-v3-turbo pt</option>
          </select>
        </label>
        <label>
          Provider STT (ONNX)
          <select
            value={config.sttProvider}
            onChange={(e) =>
              onChange({ sttProvider: e.target.value as CodecConfig['sttProvider'] })
            }
          >
            {providersAvailable.map((provider) => (
              <option key={provider} value={provider}>
                {provider === 'cuda' ? 'CUDA (GPU)' : 'CPU'}
              </option>
            ))}
          </select>
          <span className="field-hint">
            Servidor padrão: {serverProvider}
            {cudaCompiled ? ' · build com CUDA' : ' · apenas CPU no build atual'}
          </span>
        </label>
      </div>

      <fieldset className="sim-options">
        <legend>Simulação de degradação</legend>
        <div className="grid-2">
          <label className="checkbox">
            <input
              type="checkbox"
              checked={config.simulateClipping}
              onChange={(e) => onChange({ simulateClipping: e.target.checked })}
            />
            Simular clipping (saturação no PCM)
          </label>
          <label>
            Ganho clipping
            <input
              type="number"
              min={1}
              max={12}
              step={0.1}
              value={config.clippingGain}
              disabled={!config.simulateClipping}
              onChange={(e) => onChange({ clippingGain: Number(e.target.value) })}
            />
          </label>
          <label>
            Perda de pacotes RTP (%)
            <input
              type="number"
              min={0}
              max={50}
              step={1}
              value={config.simulatePacketLossPct}
              onChange={(e) => onChange({ simulatePacketLossPct: Number(e.target.value) })}
            />
          </label>
        </div>
        <p className="muted">
          Clipping é aplicado antes do encode do codec. Perda RTP descarta frames no envio WebSocket
          (VQA e STT).
        </p>
      </fieldset>
    </section>
  );
}
