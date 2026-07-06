import type { CodecConfig } from '../lib/types';
import { sampleRateForFormat } from '../lib/types';

interface Props {
  config: CodecConfig;
  onChange: (patch: Partial<CodecConfig>) => void;
}

export function CodecConfigPanel({ config, onChange }: Props) {
  return (
    <section className="panel">
      <h2>Codec & simulação SIP</h2>
      <div className="grid-2">
        <label>
          Formato
          <select
            value={config.format}
            onChange={(e) => {
              const format = e.target.value as CodecConfig['format'];
              onChange({ format, sampleRate: sampleRateForFormat(format) });
            }}
          >
            <option value="pcm_s16le_8k">PCM S16LE 8 kHz</option>
            <option value="pcm_s16le_16k">PCM S16LE 16 kHz</option>
            <option value="rtp_pcmu">RTP G.711 μ-law (PCMU)</option>
            <option value="rtp_pcma">RTP G.711 A-law (PCMA)</option>
            <option value="rtp_g722">RTP G.722 wideband (PT 9)</option>
            <option value="rtp_g729">RTP G.729 (PT 18)</option>
          </select>
        </label>
        <label>
          Sample rate
          <select
            value={config.sampleRate}
            onChange={(e) =>
              onChange({ sampleRate: Number(e.target.value) as 8000 | 16000 })
            }
          >
            <option value={8000}>8000 Hz</option>
            <option value={16000}>16000 Hz</option>
          </select>
        </label>
        <label>
          Frame (ms)
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
        <label className="checkbox">
          <input
            type="checkbox"
            checked={config.simulateClipping}
            onChange={(e) => onChange({ simulateClipping: e.target.checked })}
          />
          Simular clipping (ganho alto)
        </label>
        <label>
          Ganho clipping
          <input
            type="range"
            min={1}
            max={8}
            step={0.1}
            value={config.clippingGain}
            disabled={!config.simulateClipping}
            onChange={(e) => onChange({ clippingGain: Number(e.target.value) })}
          />
          {config.clippingGain.toFixed(1)}x
        </label>
        <label>
          Perda de pacotes RTP (%)
          <input
            type="range"
            min={0}
            max={30}
            value={config.simulatePacketLossPct}
            onChange={(e) =>
              onChange({ simulatePacketLossPct: Number(e.target.value) })
            }
          />
          {config.simulatePacketLossPct}%
        </label>
      </div>
    </section>
  );
}
