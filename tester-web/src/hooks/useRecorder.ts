import { useCallback, useRef, useState } from 'react';
import { floatToInt16 } from '../lib/audio';

export interface RecordingState {
  isRecording: boolean;
  durationSec: number;
  error: string | null;
}

/** Taxa alvo para gravação no tester (wideband telefonia / G.722). */
const TARGET_SAMPLE_RATE = 16000;

/** Grava PCM na taxa do AudioContext — resample por codec só quando necessário. */
export function useRecorder() {
  const [state, setState] = useState<RecordingState>({
    isRecording: false,
    durationSec: 0,
    error: null,
  });
  const [pcm, setPcm] = useState<Int16Array | null>(null);
  const [pcmRate, setPcmRate] = useState(TARGET_SAMPLE_RATE);
  const [liveWave, setLiveWave] = useState<Float32Array | null>(null);

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
          sampleRate: TARGET_SAMPLE_RATE,
          channelCount: 1,
        },
      });
      streamRef.current = stream;
      const ctx = new AudioContext({ sampleRate: TARGET_SAMPLE_RATE });
      ctxRef.current = ctx;
      setPcmRate(ctx.sampleRate);

      const source = ctx.createMediaStreamSource(stream);
      const processor = ctx.createScriptProcessor(4096, 1, 1);
      processor.onaudioprocess = (e) => {
        const input = e.inputBuffer.getChannelData(0);
        chunksRef.current.push(new Float32Array(input));
        setLiveWave(new Float32Array(input));
      };
      // ScriptProcessor precisa de destino, mas não monitorar no alto-falante.
      const silent = ctx.createGain();
      silent.gain.value = 0;
      source.connect(processor);
      processor.connect(silent);
      silent.connect(ctx.destination);

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
    setLiveWave(null);
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

  return { state, pcm, pcmRate, liveWave, start, stop, loadFile, setPcm };
}
