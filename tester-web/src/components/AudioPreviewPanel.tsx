import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  buildVariants,
  createPreviewUrl,
  createWavBlob,
  encodeForTransport,
  revokeVariantUrls,
  type EncodedVariant,
} from '../lib/audio';
import { COMPARE_PRESETS, formatConfigLabel, type CodecConfig } from '../lib/types';
import { useAudioPlayer } from '../hooks/useAudioPlayer';

interface Props {
  pcm: Int16Array | null;
  pcmRate: number;
  config: CodecConfig;
}

export function AudioPreviewPanel({ pcm, pcmRate, config }: Props) {
  const { playingId, queueLabel, playUrl, playAll, stop } = useAudioPlayer();
  const [variants, setVariants] = useState<EncodedVariant[]>([]);
  const [selectedPresets, setSelectedPresets] = useState<string[]>(
    COMPARE_PRESETS.map((p) => p.id),
  );
  const [currentPreviewUrl, setCurrentPreviewUrl] = useState<string | null>(null);
  const [previewError, setPreviewError] = useState<string | null>(null);
  const [variantsLoading, setVariantsLoading] = useState(false);

  const originalUrl = useMemo(() => {
    if (!pcm?.length) return null;
    return URL.createObjectURL(createWavBlob(pcm, pcmRate));
  }, [pcm, pcmRate]);

  useEffect(() => {
    if (!pcm?.length) {
      setCurrentPreviewUrl(null);
      return;
    }
    let cancelled = false;
    setPreviewError(null);
    encodeForTransport(pcm, pcmRate, config)
      .then((encoded) => createPreviewUrl(encoded))
      .then((url) => {
        if (!cancelled) setCurrentPreviewUrl(url);
      })
      .catch((e) => {
        if (!cancelled) {
          setPreviewError(e instanceof Error ? e.message : String(e));
          setCurrentPreviewUrl(null);
        }
      });
    return () => {
      cancelled = true;
    };
  }, [pcm, pcmRate, config]);

  useEffect(() => {
    return () => {
      if (originalUrl) URL.revokeObjectURL(originalUrl);
      if (currentPreviewUrl) URL.revokeObjectURL(currentPreviewUrl);
      revokeVariantUrls(variants);
    };
  }, [originalUrl, currentPreviewUrl, variants]);

  const generateVariants = useCallback(async () => {
    if (!pcm?.length) return;
    setVariantsLoading(true);
    setPreviewError(null);
    try {
      const next = await buildVariants(pcm, pcmRate, selectedPresets, config.sessionId);
      setVariants((prev) => {
        revokeVariantUrls(prev);
        return next;
      });
    } catch (e) {
      setPreviewError(e instanceof Error ? e.message : String(e));
    } finally {
      setVariantsLoading(false);
    }
  }, [pcm, pcmRate, selectedPresets, config.sessionId]);

  const togglePreset = (id: string) => {
    setSelectedPresets((prev) =>
      prev.includes(id) ? prev.filter((x) => x !== id) : [...prev, id],
    );
  };

  const playOriginal = () => originalUrl && playUrl('original', originalUrl, 'Original');
  const playCurrent = () =>
    currentPreviewUrl &&
    playUrl('current', currentPreviewUrl, `Enviado: ${formatConfigLabel(config)}`);

  const playAllVariants = () => {
    const items = variants.map((v) => ({
      id: v.id,
      url: v.previewUrl,
      label: v.label,
    }));
    if (originalUrl) {
      items.unshift({ id: 'original', url: originalUrl, label: 'Original' });
    }
    playAll(items);
  };

  if (!pcm?.length) {
    return (
      <section className="panel">
        <h2>Preview de áudio</h2>
        <p className="muted">Grave ou carregue um áudio para ouvir o original e as variantes.</p>
      </section>
    );
  }

  return (
    <section className="panel">
      <h2>Preview de áudio</h2>
      <p className="muted">
        Ouça o original e como ficou após encode/decode (round-trip) — o mesmo fluxo enviado à API.
        G.722 e G.729 usam o backend para pack/decode RTP.
      </p>

      {previewError && <p className="error">{previewError}</p>}

      <div className="row player-row">
        <button className="btn" onClick={playOriginal} disabled={playingId === 'original'}>
          ▶ Original ({pcmRate} Hz)
        </button>
        <button
          className="btn primary"
          onClick={playCurrent}
          disabled={playingId === 'current' || !currentPreviewUrl}
        >
          ▶ Como enviado (config atual)
        </button>
        {playingId && (
          <button className="btn" onClick={stop}>
            ■ Parar
          </button>
        )}
      </div>
      {queueLabel && <p className="playing-label">Tocando: {queueLabel}</p>}

      <h3 className="subhead">Comparar múltiplas configurações</h3>
      <p className="muted">Mesmo áudio gravado, codificado com presets SIP diferentes.</p>

      <div className="preset-grid">
        {COMPARE_PRESETS.map((p) => (
          <label key={p.id} className="checkbox preset-chip">
            <input
              type="checkbox"
              checked={selectedPresets.includes(p.id)}
              onChange={() => togglePreset(p.id)}
            />
            {p.label}
          </label>
        ))}
      </div>

      <div className="row">
        <button
          className="btn primary"
          onClick={() => void generateVariants()}
          disabled={selectedPresets.length === 0 || variantsLoading}
        >
          {variantsLoading ? 'Gerando…' : `Gerar ${selectedPresets.length} variantes`}
        </button>
        {variants.length > 0 && (
          <button className="btn" onClick={playAllVariants}>
            ▶ Tocar todas ({variants.length + 1} com original)
          </button>
        )}
      </div>

      {variants.length > 0 && (
        <ul className="variant-list">
          {variants.map((v) => (
            <li key={v.id} className="variant-item">
              <span>{v.label}</span>
              <button
                className="btn sm"
                onClick={() => playUrl(v.id, v.previewUrl, v.label)}
                disabled={playingId === v.id}
              >
                ▶
              </button>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
