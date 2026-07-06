import { useCallback, useEffect, useState } from 'react';
import { AudioPreviewPanel } from './components/AudioPreviewPanel';
import { CodecConfigPanel } from './components/CodecConfig';
import { RecorderPanel } from './components/RecorderPanel';
import { ResultsPanel } from './components/ResultsPanel';
import { useRecorder } from './hooks/useRecorder';
import {
  analyzeGrpcBatch,
  analyzeGrpcStream,
  analyzeRestBatch,
  analyzeWebSocket,
  checkReady,
  grpcReady,
  transcribeGrpcBatch,
  transcribeRestBatch,
  transcribeRestWav,
  transcribeWebSocket,
} from './lib/api';
import { encodeForTransport } from './lib/audio';
import type { BatchResult, CodecConfig, SttResult, Transport, WindowMetrics } from './lib/types';
import { DEFAULT_CODEC } from './lib/types';

export default function App() {
  const [config, setConfig] = useState<CodecConfig>(DEFAULT_CODEC);
  const [transport, setTransport] = useState<Transport>('rest');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [status, setStatus] = useState('');
  const [batch, setBatch] = useState<BatchResult | null>(null);
  const [sttResult, setSttResult] = useState<SttResult | null>(null);
  const [streamReports, setStreamReports] = useState<WindowMetrics[]>([]);
  const [serviceReady, setServiceReady] = useState<string | null>(null);

  const { state, pcm, pcmRate, start, stop, loadFile } = useRecorder();

  const patchConfig = useCallback((patch: Partial<CodecConfig>) => {
    setConfig((c) => ({ ...c, ...patch }));
  }, []);

  useEffect(() => {
    checkReady()
      .then((r) => setServiceReady(JSON.stringify(r, null, 2)))
      .catch((e) => setServiceReady(`offline: ${e.message}`));
  }, []);

  const transcribeWavFile = useCallback(
    async (file: File) => {
      setLoading(true);
      setError(null);
      setSttResult(null);
      setStatus('');
      try {
        setStatus(`Enviando WAV para STT (${config.sttModel})…`);
        const result = await transcribeRestWav(file, config.sttModel);
        setSttResult(result);
        setStatus('STT WAV concluído');
      } catch (e) {
        setError(e instanceof Error ? e.message : String(e));
      } finally {
        setLoading(false);
      }
    },
    [config.sttModel],
  );

  const analyze = useCallback(async () => {
    if (!pcm || pcm.length === 0) {
      setError('Grave ou carregue um áudio primeiro');
      return;
    }
    setLoading(true);
    setError(null);
    setBatch(null);
    setStreamReports([]);
    setStatus('');

    try {
      const encoded = await encodeForTransport(pcm, pcmRate, config);

      if (transport === 'rest') {
        setStatus('Enviando batch REST…');
        const result = await analyzeRestBatch(encoded, config);
        setBatch(result);
        setStatus('REST batch concluído');
      } else if (transport === 'grpc') {
        setStatus('Enviando gRPC playground (stream SSE)…');
        await analyzeGrpcStream(encoded, config, (r) => {
          setStreamReports((prev) => [...prev, r]);
        });
        setStatus('gRPC stream concluído');
        const batchResult = await analyzeGrpcBatch(encoded, config);
        setBatch(batchResult);
      } else {
        setStatus('Conectando WebSocket…');
        const ws = analyzeWebSocket(
          encoded,
          config,
          (r) => setStreamReports((prev) => [...prev, r]),
          setStatus,
          config.simulatePacketLossPct,
        );
        await new Promise((r) => setTimeout(r, 300));
        setStatus('Enviando frames…');
        await ws.send();
        ws.close();
        setStatus('WebSocket concluído');
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }, [pcm, pcmRate, config, transport]);

  const transcribe = useCallback(async () => {
    if (!pcm || pcm.length === 0) {
      setError('Grave ou carregue um áudio primeiro');
      return;
    }
    setLoading(true);
    setError(null);
    setSttResult(null);
    setStatus('');

    try {
      const encoded = await encodeForTransport(pcm, pcmRate, config);

      if (transport === 'rest') {
        setStatus('Enviando STT REST…');
        const result = await transcribeRestBatch(encoded, config);
        setSttResult(result);
        setStatus('STT REST concluído');
      } else if (transport === 'grpc') {
        setStatus('Enviando STT gRPC playground…');
        const result = await transcribeGrpcBatch(encoded, config);
        setSttResult(result);
        setStatus('STT gRPC concluído');
      } else {
        setStatus('Conectando STT WebSocket…');
        const ws = transcribeWebSocket(encoded, config, setStatus);
        await new Promise((r) => setTimeout(r, 300));
        const result = await ws.send();
        setSttResult(result);
        setStatus('STT WebSocket concluído');
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }, [pcm, pcmRate, config, transport]);

  const onGrpcReady = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const r = await grpcReady();
      setStatus(JSON.stringify(r, null, 2));
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }, []);

  return (
    <div className="layout">
      <header>
        <h1>voiceqas tester</h1>
        <p>
          Grave áudio no navegador, simule codecs SIP e teste STT via REST / WebSocket / gRPC —{' '}
          <a href="/docs/swagger" target="_blank" rel="noreferrer">
            Swagger
          </a>
        </p>
      </header>

      <RecorderPanel
        isRecording={state.isRecording}
        durationSec={state.durationSec}
        error={state.error}
        hasAudio={!!pcm?.length}
        sampleCount={pcm?.length ?? 0}
        sourceRate={pcmRate}
        onStart={start}
        onStop={stop}
        onFile={loadFile}
        onWavStt={transcribeWavFile}
      />

      <AudioPreviewPanel pcm={pcm} pcmRate={pcmRate} config={config} />

      <CodecConfigPanel config={config} onChange={patchConfig} />

      <ResultsPanel
        transport={transport}
        onTransport={setTransport}
        loading={loading}
        error={error}
        status={status}
        batch={batch}
        streamReports={streamReports}
        sttResult={sttResult}
        onAnalyze={analyze}
        onTranscribe={transcribe}
        onGrpcReady={onGrpcReady}
      />

      {serviceReady && (
        <section className="panel">
          <h2>voiceqas /ready</h2>
          <pre className="log">{serviceReady}</pre>
        </section>
      )}
    </div>
  );
}
