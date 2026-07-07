import { Waveform } from './Waveform';

interface Props {
  isRecording: boolean;
  durationSec: number;
  error: string | null;
  hasAudio: boolean;
  sampleCount: number;
  sourceRate: number;
  pcm?: Int16Array | null;
  liveWave?: Float32Array | null;
  onStart: () => void;
  onStop: () => void;
  onFile: (f: File) => void;
  onWavStt?: (f: File) => void;
}

export function RecorderPanel({
  isRecording,
  durationSec,
  error,
  hasAudio,
  sampleCount,
  sourceRate,
  pcm,
  liveWave,
  onStart,
  onStop,
  onFile,
  onWavStt,
}: Props) {
  return (
    <section className="panel">
      <h2>Gravação (browser / WebRTC)</h2>
      <p className="muted">
        Captura via <code>getUserMedia</code> a 16 kHz (wideband). Sem monitor no alto-falante
        durante a gravação.
      </p>
      <div className="row">
        {!isRecording ? (
          <button className="btn primary" onClick={onStart}>
            Gravar microfone
          </button>
        ) : (
          <button className="btn danger" onClick={onStop}>
            Parar ({durationSec.toFixed(1)}s)
          </button>
        )}
        <label className="btn file">
          Carregar WAV
          <input
            type="file"
            accept="audio/*"
            hidden
            onChange={(e) => {
              const f = e.target.files?.[0];
              if (f) onFile(f);
            }}
          />
        </label>
        {onWavStt && (
          <label className="btn">
            STT direto (WAV)
            <input
              type="file"
              accept="audio/wav,audio/*"
              hidden
              onChange={(e) => {
                const f = e.target.files?.[0];
                if (f) onWavStt(f);
              }}
            />
          </label>
        )}
      </div>
      {error && <p className="error">{error}</p>}
      {(isRecording || hasAudio) && (
        <Waveform pcm={pcm ?? null} liveSamples={isRecording ? liveWave : null} />
      )}
      {hasAudio && (
        <p className="ok">
          Áudio pronto: {sampleCount} amostras (origem ~{sourceRate} Hz)
        </p>
      )}
    </section>
  );
}
