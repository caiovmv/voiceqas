import { useCallback, useRef, useState } from 'react';
import { floatToInt16 } from '../lib/audio';

export interface RecordingState {
  isRecording: boolean;
  durationSec: number;
  error: string | null;
}

/** Grava PCM na taxa nativa do browser/arquivo — resample ocorre por variante de codec. */
export function useRecorder() {
  const [state, setState] = useState<RecordingState>({
    isRecording: false,
    durationSec: 0,
    error: null,
  });
  const [pcm, setPcm] = useState<Int16Array | null>(null);
  const [pcmRate, setPcmRate] = useState(48000);

  const ctxRef = useRef<AudioContext | null>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const chunksRef = useRef<Float32Array[]>([]);
  const timerRef = useRef<number | null>(null);
  const startRef = useRef(0);

  const stopTracks = useCallback(() => {
    streamRef.current?.getTracks().forEach((t) => t.stop());
    streamRef.current = null;
    if (timerRef.current) {
      window.clearInterval(timerRef.current);
      timerRef.current = null;
    }
  }, []);

  const start = useCallback(async () => {
    setState({ isRecording: true, durationSec: 0, error: null });
    chunksRef.current = [];
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false,
        },
      });
      streamRef.current = stream;
      const ctx = new AudioContext();
      ctxRef.current = ctx;
      setPcmRate(ctx.sampleRate);

      const source = ctx.createMediaStreamSource(stream);
      const processor = ctx.createScriptProcessor(4096, 1, 1);
      processor.onaudioprocess = (e) => {
        const input = e.inputBuffer.getChannelData(0);
        chunksRef.current.push(new Float32Array(input));
      };
      source.connect(processor);
      processor.connect(ctx.destination);

      startRef.current = Date.now();
      timerRef.current = window.setInterval(() => {
        setState((s) => ({
          ...s,
          durationSec: (Date.now() - startRef.current) / 1000,
        }));
      }, 200);
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Falha ao acessar microfone';
      setState({ isRecording: false, durationSec: 0, error: msg });
      stopTracks();
    }
  }, [stopTracks]);

  const stop = useCallback(async () => {
    stopTracks();
    const ctx = ctxRef.current;
    const rate = ctx?.sampleRate ?? pcmRate;
    if (ctx) {
      await ctx.close();
      ctxRef.current = null;
    }

    const total = chunksRef.current.reduce((n, c) => n + c.length, 0);
    const merged = new Float32Array(total);
    let off = 0;
    for (const c of chunksRef.current) {
      merged.set(c, off);
      off += c.length;
    }

    setPcm(floatToInt16(merged));
    setPcmRate(rate);
    setState((s) => ({ ...s, isRecording: false }));
  }, [pcmRate, stopTracks]);

  const loadFile = useCallback(async (file: File) => {
    setState({ isRecording: false, durationSec: 0, error: null });
    const buf = await file.arrayBuffer();
    const ctx = new AudioContext();
    const audio = await ctx.decodeAudioData(buf);
    const channel = audio.getChannelData(0);
    setPcm(floatToInt16(channel));
    setPcmRate(audio.sampleRate);
    setState({ isRecording: false, durationSec: audio.duration, error: null });
    await ctx.close();
  }, []);

  return { state, pcm, pcmRate, start, stop, loadFile, setPcm };
}
