import { useEffect, useRef } from 'react';

interface WaveformProps {
  pcm: Int16Array | null;
  liveSamples?: Float32Array | null;
  height?: number;
  className?: string;
}

export function Waveform({ pcm, liveSamples, height = 64, className }: WaveformProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = '#0d1218';
    ctx.fillRect(0, 0, w, h);

    let data: Float32Array;
    if (liveSamples && liveSamples.length > 0) {
      data = liveSamples;
    } else if (pcm && pcm.length > 0) {
      const step = Math.max(1, Math.floor(pcm.length / w));
      data = new Float32Array(w);
      for (let i = 0; i < w; i++) {
        data[i] = pcm[i * step] / 32768;
      }
    } else {
      ctx.strokeStyle = '#2a3555';
      ctx.beginPath();
      ctx.moveTo(0, h / 2);
      ctx.lineTo(w, h / 2);
      ctx.stroke();
      return;
    }

    const bars = Math.min(w, data.length);
    const mid = h / 2;
    ctx.strokeStyle = '#4da3ff';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let i = 0; i < bars; i++) {
      const x = (i / bars) * w;
      const amp = data[Math.floor((i / bars) * data.length)] * mid * 0.9;
      ctx.moveTo(x, mid - amp);
      ctx.lineTo(x, mid + amp);
    }
    ctx.stroke();
  }, [pcm, liveSamples, height]);

  return (
    <canvas
      ref={canvasRef}
      width={640}
      height={height}
      className={className ?? 'waveform-canvas'}
      aria-label="Waveform"
    />
  );
}
