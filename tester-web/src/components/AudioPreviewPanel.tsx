import { useEffect, useMemo, useState } from 'react';
import {
  buildCodecRoundTripPcm,
  buildSttEnhancedPcm,
  createPreviewUrl,
  createWavBlob,
  downloadBlob,
  downloadComparisonPackZip,
  encodeForTransport,
  type ComparisonPackFile,
} from '../lib/audio';
import { RECORD_QUALITY_OPTIONS, formatConfigLabel, type CodecConfig } from '../lib/types';
import { useAudioPlayer } from '../hooks/useAudioPlayer';

interface Props {
  pcm: Int16Array | null;
  pcmRate: number;
  config: CodecConfig;
}

export function AudioPreviewPanel({ pcm, pcmRate, config }: Props) {
  const { playingId, queueLabel, playUrl, stop } = useAudioPlayer();
  const [codecPreviewUrl, setCodecPreviewUrl] = useState<string | null>(null);
  const [sttPreviewUrl, setSttPreviewUrl] = useState<string | null>(null);
  const [packFiles, setPackFiles] = useState<ComparisonPackFile[]>([]);
  const [previewError, setPreviewError] = useState<string | null>(null);
  const [packLoading, setPackLoading] = useState(false);

  const qualityLabel =
    RECORD_QUALITY_OPTIONS.find(
      (o) => o.format === config.format && o.sampleRate === config.sampleRate,
    )?.label ?? formatConfigLabel(config);

  const originalUrl = useMemo(() => {
    if (!pcm?.length) return null;
    return URL.createObjectURL(createWavBlob(pcm, pcmRate));
  }, [pcm, pcmRate]);

  useEffect(() => {
    if (!pcm?.length) {
      setCodecPreviewUrl(null);
      setSttPreviewUrl(null);
      setPackFiles([]);
      return;
    }
    let cancelled = false;
    setPreviewError(null);
    setPackLoading(true);

    (async () => {
      try {
        const [roundTrip, sttPcm] = await Promise.all([
          buildCodecRoundTripPcm(pcm, pcmRate, config),
          buildSttEnhancedPcm(pcm, pcmRate, config),
        ]);
        if (cancelled) return;

        const encoded = await encodeForTransport(pcm, pcmRate, config);
        const codecUrl = await createPreviewUrl(encoded);
        const sttUrl = URL.createObjectURL(createWavBlob(sttPcm, 16000));

        setCodecPreviewUrl((prev) => {
          if (prev) URL.revokeObjectURL(prev);
          return codecUrl;
        });
        setSttPreviewUrl((prev) => {
          if (prev) URL.revokeObjectURL(prev);
          return sttUrl;
        });
        setPackFiles([
          { name: '01-original.wav', blob: createWavBlob(pcm, pcmRate) },
          {
            name: `02-codec-${config.format.replace(/rtp_|pcm_s16le_/g, '')}-roundtrip.wav`,
            blob: createWavBlob(roundTrip.pcm, roundTrip.sampleRate),
          },
          { name: '03-stt-enhanced-16khz.wav', blob: createWavBlob(sttPcm, 16000) },
        ]);
      } catch (e) {
        if (!cancelled) {
          setPreviewError(e instanceof Error ? e.message : String(e));
        }
      } finally {
        if (!cancelled) setPackLoading(false);
      }
    })();

    return () => {
      cancelled = true;
    };
  }, [pcm, pcmRate, config]);

  useEffect(() => {
    return () => {
      if (originalUrl) URL.revokeObjectURL(originalUrl);
      if (codecPreviewUrl) URL.revokeObjectURL(codecPreviewUrl);
      if (sttPreviewUrl) URL.revokeObjectURL(sttPreviewUrl);
    };
  }, [originalUrl, codecPreviewUrl, sttPreviewUrl]);

  const downloadPack = async () => {
    if (!pcm?.length) return;
    setPackLoading(true);
    setPreviewError(null);
    try {
      await downloadComparisonPackZip(pcm, pcmRate, config);
    } catch (e) {
      setPreviewError(e instanceof Error ? e.message : String(e));
    } finally {
      setPackLoading(false);
    }
  };

  if (!pcm?.length) {
    return (
      <section className="panel">
        <h2>Preview & download</h2>
        <p className="muted">Grave ou carregue um áudio para comparar original, codec e STT.</p>
      </section>
    );
  }

  return (
    <section className="panel">
      <h2>Preview & download</h2>
      <p className="muted">
        <strong>01 Original</strong> — exatamente o que o microfone capturou ({pcmRate} Hz).{' '}
        <strong>02 Codec</strong> — após encode/decode {qualityLabel} (como no tronco SIP; soa mais
        estreito). <strong>03 STT</strong> — PCM 16 kHz enviado ao Parakeet/Whisper.
      </p>

      {previewError && <p className="error">{previewError}</p>}
      {packLoading && <p className="muted">Processando áudio…</p>}

      <div className="row player-row">
        <button className="btn" onClick={() => originalUrl && playUrl('original', originalUrl, 'Original')} disabled={playingId === 'original'}>
          ▶ Original ({pcmRate} Hz)
        </button>
        <button
          className="btn primary"
          onClick={() => codecPreviewUrl && playUrl('codec', codecPreviewUrl, qualityLabel)}
          disabled={playingId === 'codec' || !codecPreviewUrl}
        >
          ▶ Codec ({qualityLabel})
        </button>
        <button
          className="btn primary"
          onClick={() => sttPreviewUrl && playUrl('stt', sttPreviewUrl, 'STT enhanced')}
          disabled={playingId === 'stt' || !sttPreviewUrl}
        >
          ▶ STT enhanced (16 kHz)
        </button>
        {playingId && (
          <button className="btn" onClick={stop}>
            ■ Parar
          </button>
        )}
      </div>
      {queueLabel && <p className="playing-label">Tocando: {queueLabel}</p>}

      <div className="row">
        <button className="btn primary" type="button" onClick={() => void downloadPack()} disabled={packLoading}>
          ⬇ Baixar pack ZIP (3 arquivos)
        </button>
      </div>

      {packFiles.length > 0 && (
        <ul className="variant-list pack-list">
          {packFiles.map((f) => (
            <li key={f.name} className="variant-item">
              <span>
                <code>{f.name}</code>
              </span>
              <button
                type="button"
                className="btn sm"
                onClick={() => downloadBlob(f.blob, f.name)}
              >
                ⬇ WAV
              </button>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
