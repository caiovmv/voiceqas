import { memo, useEffect, useMemo, useRef } from 'react';
import type { DiarizationTurn } from '../../lib/types';
import { pcmToSpectrogram } from '../../lib/domain/spectrogram';

interface Props {
  pcm: Int16Array | null;
  sampleRate: number;
  turns?: DiarizationTurn[];
  title: string;
}

function turnsFingerprint(turns: DiarizationTurn[]): string {
  if (turns.length === 0) return '';
  return turns.map((t) => `${t.start_ms}:${t.end_ms}:${t.is_primary ? 1 : 0}`).join('|');
}

function SpectrogramInner({ pcm, sampleRate, turns = [], title }: Props) {
  const ref = useRef<HTMLCanvasElement>(null);
  const turnKey = useMemo(() => turnsFingerprint(turns), [turns]);

  useEffect(() => {
    const canvas = ref.current;
    if (!canvas || !pcm || pcm.length === 0) return;
    const { rows, cols, data } = pcmToSpectrogram(pcm, sampleRate);
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const w = canvas.width;
    const h = canvas.height;
    const img = ctx.createImageData(w, h);
    let min = Infinity;
    let max = -Infinity;
    for (let i = 0; i < data.length; i++) {
      const v = data[i];
      if (v < min) min = v;
      if (v > max) max = v;
    }
    const span = Math.max(1e-6, max - min);
    for (let y = 0; y < h; y++) {
      const r = Math.floor(((h - 1 - y) / (h - 1)) * (rows - 1));
      for (let x = 0; x < w; x++) {
        const c = Math.floor((x / (w - 1)) * (cols - 1));
        const v = (data[r * cols + c] - min) / span;
        const idx = (y * w + x) * 4;
        img.data[idx] = Math.floor(20 + v * 220);
        img.data[idx + 1] = Math.floor(30 + v * 120);
        img.data[idx + 2] = Math.floor(40 + (1 - v) * 160);
        img.data[idx + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    const durationMs = (pcm.length / sampleRate) * 1000;
    for (const t of turns) {
      const x0 = (t.start_ms / durationMs) * w;
      const x1 = (t.end_ms / durationMs) * w;
      ctx.fillStyle = t.is_primary ? 'rgba(80,200,120,0.25)' : 'rgba(220,120,60,0.25)';
      ctx.fillRect(x0, 0, Math.max(1, x1 - x0), h);
    }
    // Depend on turnKey so unstable [] refs from the parent do not re-STFT.
    // eslint-disable-next-line react-hooks/exhaustive-deps -- turns captured via turnKey
  }, [pcm, sampleRate, turnKey]);

  const durationSec =
    pcm && pcm.length > 0 && sampleRate > 0 ? pcm.length / sampleRate : 0;

  return (
    <div className="analysis-spectro">
      <div className="analysis-timeline-head">
        {title}
        {durationSec > 0 ? (
          <span className="muted"> · {durationSec.toFixed(1)}s · {pcm!.length.toLocaleString()} samples</span>
        ) : null}
      </div>
      {!pcm || pcm.length === 0 ? (
        <p className="muted">Sem PCM</p>
      ) : (
        <canvas ref={ref} width={640} height={160} className="analysis-spectro-canvas" />
      )}
    </div>
  );
}

export const Spectrogram = memo(SpectrogramInner);
