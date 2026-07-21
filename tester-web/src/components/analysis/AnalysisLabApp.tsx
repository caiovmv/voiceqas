import { useCallback, useEffect, useMemo, useState } from 'react';
import { useRecorder } from '../../hooks/useRecorder';
import { useAudioPlayer } from '../../hooks/useAudioPlayer';
import { CodecConfigPanel } from '../CodecConfig';
import { RecorderPanel } from '../RecorderPanel';
import {
  analyzeRestBatch,
  headersFromMix,
  processAudioStripWav,
  requestAnalysisLlm,
  transcribeRestBatch,
} from '../../lib/api';
import { createWavBlob, encodeForTransport, type EncodedPayload } from '../../lib/audio';
import {
  DEFAULT_MIX,
  LEGACY_MIX,
  type PipelineMixState,
} from '../../lib/domain/pipeline-mix';
import { buildAnalysisObjective } from '../../lib/domain/analysis-objective';
import { buildMixPreviewPcm } from '../../lib/domain/mix-preview';
import {
  afterViewChunks,
  beforeViewChunks,
  diffWords,
} from '../../lib/domain/stt-diff';
import {
  compareSummaries,
  rescoreBatch,
  type RescoreSummary,
} from '../../lib/domain/vqa-rescore';
import type { BatchResult, CodecConfig, DiarizationTurn, SttResult } from '../../lib/types';
import { DEFAULT_CODEC, newTestSessionId } from '../../lib/types';
import { CopyTextButton } from './CopyTextButton';
import { DiarizationPanel } from './DiarizationPanel';
import { DiffText } from './SttDiff';
import { LlmAnalysisPanel } from './LlmAnalysisPanel';
import { MixDesk } from './MixDesk';
import { ScoreTimeline } from './ScoreTimeline';
import { Spectrogram } from './Spectrogram';

/** Stable empty array — `?? []` allocates every render and re-triggers spectrogram/WAV. */
const EMPTY_TURNS: DiarizationTurn[] = [];

interface PassState {
  label: string;
  mix: PipelineMixState;
  batch: BatchResult | null;
  summary: RescoreSummary | null;
  stt: SttResult | null;
  error?: string | null;
}

function pcmTransportConfig(config: CodecConfig, simulateTrunk: boolean): CodecConfig {
  if (simulateTrunk) return config;
  return {
    ...config,
    format: 'pcm_s16le_16k',
    sampleRate: 16000,
  };
}

function emptyStt(model: string, error: string): SttResult {
  return {
    text: '',
    model,
    language: 'pt',
    duration_ms: 0,
    processing_ms: 0,
    segments: [],
    ok: false,
    error,
  };
}

/** Prefer richer of top-level text vs joined diarization turns. */
function displaySttText(stt: SttResult | null | undefined): string {
  if (!stt) return '';
  const top = (stt.text ?? '').trim();
  const turns = stt.diarization?.turns ?? [];
  const joined = turns
    .map((t) => (t.text ?? '').trim())
    .filter(Boolean)
    .join(' ')
    .trim();
  if (!top) return joined;
  if (!joined) return stt.text;
  // Prefer joined turns only when clearly longer (top can be Whisper stubs).
  if (joined.length > top.length * 1.5) return joined;
  return stt.text;
}

export function AnalysisLabApp() {
  const [config, setConfig] = useState<CodecConfig>(() => ({
    ...DEFAULT_CODEC,
    sessionId: newTestSessionId(),
  }));
  const [mix, setMix] = useState<PipelineMixState>(DEFAULT_MIX);
  const [before, setBefore] = useState<PassState | null>(null);
  const [after, setAfter] = useState<PassState | null>(null);
  const [loading, setLoading] = useState(false);
  const [sttLoading, setSttLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [status, setStatus] = useState('');
  const [llmLoading, setLlmLoading] = useState(false);
  const [llmText, setLlmText] = useState<string | null>(null);
  const [llmError, setLlmError] = useState<string | null>(null);
  const [llmModel, setLlmModel] = useState<string | undefined>();
  const [llmFormatOk, setLlmFormatOk] = useState<boolean | null>(null);
  const [llmFormatErrors, setLlmFormatErrors] = useState<string[]>([]);
  const [llmAttempts, setLlmAttempts] = useState<number | null>(null);

  const { state, pcm, pcmRate, liveWave, start, stop, loadFile } = useRecorder();
  const { playingId, playUrl, stop: stopPlay } = useAudioPlayer();

  const delta = useMemo(() => {
    if (!before?.summary || !after?.summary) return null;
    return compareSummaries(before.summary, after.summary);
  }, [before, after]);

  const patchConfig = (patch: Partial<CodecConfig>) => {
    setConfig((c) => ({ ...c, ...patch }));
  };

  const safeTranscribe = useCallback(
    async (
      encoded: EncodedPayload,
      transport: CodecConfig,
      headers: Record<string, string>,
    ): Promise<SttResult> => {
      try {
        return await transcribeRestBatch(encoded, transport, headers);
      } catch (e) {
        return emptyStt(
          transport.sttModel,
          e instanceof Error ? e.message : String(e),
        );
      }
    },
    [],
  );

  const processAnalysis = useCallback(async () => {
    if (!pcm || pcm.length === 0) {
      setError('Grave ou carregue um audio primeiro');
      return;
    }
    setLoading(true);
    setSttLoading(false);
    setError(null);
    setLlmText(null);
    setLlmError(null);
    setBefore(null);
    setAfter(null);

    const legacy: PipelineMixState = {
      ...LEGACY_MIX,
      simulate_trunk: mix.simulate_trunk,
    };

    try {
      const sessionBase = newTestSessionId();
      const transportBefore = pcmTransportConfig(
        { ...config, sessionId: `${sessionBase}-before` },
        mix.simulate_trunk,
      );
      const transportAfter = pcmTransportConfig(
        { ...config, sessionId: `${sessionBase}-after` },
        mix.simulate_trunk,
      );

      setStatus('Encoding…');
      const encoded = await encodeForTransport(pcm, pcmRate, transportBefore);

      setStatus('VQA antes + depois…');
      const headersBefore = headersFromMix(legacy);
      const headersAfter = headersFromMix(mix);

      const [batchBefore, batchAfter] = await Promise.all([
        analyzeRestBatch(encoded, transportBefore, headersBefore),
        analyzeRestBatch(encoded, transportAfter, headersAfter),
      ]);

      const laneBefore: PassState = {
        label: 'Antes (legado)',
        mix: legacy,
        batch: batchBefore,
        summary: rescoreBatch(batchBefore, legacy.gate),
        stt: null,
      };
      const laneAfter: PassState = {
        label: 'Depois (mix)',
        mix,
        batch: batchAfter,
        summary: rescoreBatch(batchAfter, mix.gate),
        stt: null,
      };
      setBefore(laneBefore);
      setAfter(laneAfter);
      setLoading(false);
      setSttLoading(true);
      // Sequencial: evita pressão no recognizer compartilhado (mutex) + timeouts nginx.
      setStatus('STT antes…');
      const sttBefore = await safeTranscribe(encoded, transportBefore, headersBefore);
      setBefore((prev) => (prev ? { ...prev, stt: sttBefore } : prev));

      setStatus('STT depois…');
      const sttAfter = await safeTranscribe(encoded, transportAfter, headersAfter);
      setAfter((prev) => (prev ? { ...prev, stt: sttAfter } : prev));
      setStatus('Processamento concluido');
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      setError(msg);
      setStatus('Falhou');
    } finally {
      setLoading(false);
      setSttLoading(false);
    }
  }, [config, pcm, pcmRate, mix, safeTranscribe]);

  const runLlm = useCallback(async () => {
    if (!before?.summary || !after?.summary) return;
    setLlmLoading(true);
    setLlmError(null);
    setLlmFormatOk(null);
    setLlmFormatErrors([]);
    setLlmAttempts(null);
    try {
      const objective = buildAnalysisObjective({
        beforeSummary: before.summary,
        afterSummary: after.summary,
        beforeSttText: before.stt?.text ?? null,
        afterSttText: after.stt?.text ?? null,
        beforeSttMs: before.stt?.processing_ms ?? null,
        afterSttMs: after.stt?.processing_ms ?? null,
      });
      const res = await requestAnalysisLlm({
        mix,
        before: {
          vqa: before.summary,
          stt: before.stt,
          turns: before.stt?.diarization?.turns ?? [],
        },
        after: {
          vqa: after.summary,
          stt: after.stt,
          turns: after.stt?.diarization?.turns ?? [],
        },
        objective,
      });
      setLlmText(res.analysis);
      setLlmModel(res.model);
      setLlmFormatOk(res.format_ok ?? res.ok);
      setLlmFormatErrors(res.format_errors ?? []);
      setLlmAttempts(res.attempts ?? null);
      if (!res.ok || res.format_ok === false) {
        setLlmError(res.error ?? 'Formato do parecer rejeitado apos retries');
      }
    } catch (e) {
      setLlmError(e instanceof Error ? e.message : String(e));
    } finally {
      setLlmLoading(false);
    }
  }, [before, after, mix]);

  const afterTurns = after?.stt?.diarization?.turns ?? EMPTY_TURNS;
  const beforeTurns = before?.stt?.diarization?.turns ?? EMPTY_TURNS;
  const beforeText = displaySttText(before?.stt);
  const afterText = displaySttText(after?.stt);
  const diffChunks = useMemo(
    () => diffWords(beforeText, afterText),
    [beforeText, afterText],
  );
  const beforeChunks = useMemo(() => beforeViewChunks(diffChunks), [diffChunks]);
  const afterChunks = useMemo(() => afterViewChunks(diffChunks), [diffChunks]);
  const canLlm = Boolean(before?.summary && after?.summary);

  const mixPreview = useMemo(() => {
    if (!pcm || pcm.length === 0) return null;
    return buildMixPreviewPcm(pcm, pcmRate, mix, afterTurns);
  }, [pcm, pcmRate, mix, afterTurns]);

  const [origUrl, setOrigUrl] = useState<string | null>(null);
  const [mixUrl, setMixUrl] = useState<string | null>(null);

  const [serverMixPcm, setServerMixPcm] = useState<Int16Array | null>(null);
  const [serverMixRate, setServerMixRate] = useState(16000);
  const [mixExportError, setMixExportError] = useState<string | null>(null);
  const [mixExportLoading, setMixExportLoading] = useState(false);


  useEffect(() => {
    if (!pcm || pcm.length === 0) {
      setOrigUrl((u) => {
        if (u) URL.revokeObjectURL(u);
        return null;
      });
      return;
    }
    const url = URL.createObjectURL(createWavBlob(pcm, pcmRate));
    setOrigUrl((prev) => {
      if (prev) URL.revokeObjectURL(prev);
      return url;
    });
    return () => URL.revokeObjectURL(url);
  }, [pcm, pcmRate]);

  // Server strip WAV for listen/download (full timeline; not browser Silero mask).
  useEffect(() => {
    if (!pcm || pcm.length === 0) {
      setMixUrl((u) => {
        if (u) URL.revokeObjectURL(u);
        return null;
      });
      setServerMixPcm(null);
      setMixExportError(null);
      return;
    }
    let cancelled = false;
    const timer = window.setTimeout(() => {
      setMixExportLoading(true);
      setMixExportError(null);
      void processAudioStripWav(pcm, pcmRate, mix)
        .then((out) => {
          if (cancelled) {
            URL.revokeObjectURL(out.url);
            return;
          }
          setMixUrl((prev) => {
            if (prev) URL.revokeObjectURL(prev);
            return out.url;
          });
          setServerMixPcm(out.pcm);
          setServerMixRate(out.sampleRate);
        })
        .catch((e: unknown) => {
          if (cancelled) return;
          setMixExportError(e instanceof Error ? e.message : String(e));
          if (mixPreview) {
            const url = URL.createObjectURL(
              createWavBlob(mixPreview.pcm, mixPreview.sampleRate),
            );
            setMixUrl((prev) => {
              if (prev) URL.revokeObjectURL(prev);
              return url;
            });
            setServerMixPcm(mixPreview.pcm);
            setServerMixRate(mixPreview.sampleRate);
          }
        })
        .finally(() => {
          if (!cancelled) setMixExportLoading(false);
        });
    }, 350);
    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, [pcm, pcmRate, mix, mixPreview]);

  return (
    <div className="analysis-lab">
      <header>
        <h1>Analise — lab de pipeline</h1>
        <p>
          Grave ou carregue audio, ajuste codec e mesa de mix, processe (antes legado vs depois) e
          compare scores, espectro, interlocutores, STT e sugestoes LLM.
        </p>
      </header>

      <section className="analysis-section">
        <h2 className="analysis-step">1. Fonte</h2>
        <RecorderPanel
          isRecording={state.isRecording}
          durationSec={state.durationSec}
          error={state.error}
          hasAudio={!!pcm?.length}
          sampleCount={pcm?.length ?? 0}
          sourceRate={pcmRate}
          pcm={pcm}
          liveWave={liveWave}
          onStart={() => void start()}
          onStop={stop}
          onFile={(f) => void loadFile(f)}
        />
      </section>

      <section className="analysis-section panel">
        <h2 className="analysis-step">2. Codec (opcional)</h2>
        <label className="checkbox mix-codec-opt">
          <input
            type="checkbox"
            checked={mix.simulate_trunk}
            onChange={(e) => setMix({ ...mix, simulate_trunk: e.target.checked })}
          />
          Simular tronco (codec G.711 / G.722 / PCM) — desligado envia PCM16 bruto
        </label>
        {mix.simulate_trunk && (
          <CodecConfigPanel config={config} onChange={patchConfig} />
        )}
      </section>

      <section className="analysis-section panel">
        <h2 className="analysis-step">3–4. Mesa de mix e gate VQA</h2>
        <MixDesk mix={mix} onChange={setMix} showCodecToggle={false} />
      </section>

      <section className="analysis-section panel">
        <h2 className="analysis-step">5. Processar</h2>
        <p className="muted">
          VQA dos dois lados em paralelo; em seguida STT. O agente libera assim que o VQA terminar.
        </p>
        <div className="row">
          <button type="button" className="btn" disabled={loading || sttLoading} onClick={() => setMix(DEFAULT_MIX)}>
            Reset mix recomendado
          </button>
          <button
            type="button"
            className="btn primary"
            data-testid="analysis-process"
            disabled={loading || sttLoading || !pcm?.length}
            onClick={() => void processAnalysis()}
          >
            {loading ? 'VQA…' : sttLoading ? 'STT…' : 'Processar'}
          </button>
        </div>
        {status && <pre className="log">{status}</pre>}
        {error && <p className="error">{error}</p>}
      </section>

      {(before || after) && (
        <section className="analysis-section panel">
          <h2>Resultados VQA</h2>
          {delta && (
            <ul className="metrics analysis-delta">
              <li>
                Score:{' '}
                <strong className={delta.score_delta >= 0 ? 'ok' : 'error'}>
                  {delta.score_delta >= 0 ? '+' : ''}
                  {delta.score_delta.toFixed(1)}
                </strong>
              </li>
              <li>
                Ready:{' '}
                <strong>
                  {delta.ready_ratio_delta >= 0 ? '+' : ''}
                  {(delta.ready_ratio_delta * 100).toFixed(1)} pp
                </strong>
              </li>
              <li>
                Risk:{' '}
                <strong className={delta.risk_delta <= 0 ? 'ok' : 'error'}>
                  {delta.risk_delta >= 0 ? '+' : ''}
                  {(delta.risk_delta * 100).toFixed(1)} pp
                </strong>
              </li>
            </ul>
          )}
          <div className="analysis-metrics-pair">
            <PassMetrics label="Antes" pass={before} />
            <PassMetrics label="Depois" pass={after} />
          </div>
          <div className="analysis-timeline-pair">
            <ScoreTimeline
              windows={before?.summary?.windows ?? []}
              title="Antes — scores"
              variant="before"
            />
            <ScoreTimeline
              windows={after?.summary?.windows ?? []}
              title="Depois — scores"
              variant="after"
            />
          </div>
          <div className="analysis-audio-pair">
            <div className="analysis-audio-card">
              <h3 className="subhead">Audio original</h3>
              <div className="row analysis-audio-actions">
                <button
                  type="button"
                  className="btn"
                  disabled={!origUrl}
                  onClick={() => {
                    if (playingId === 'orig') stopPlay();
                    else if (origUrl) void playUrl('orig', origUrl, 'Original');
                  }}
                >
                  {playingId === 'orig' ? 'Parar' : 'Ouvir original'}
                </button>
                <a
                  className={`btn ${origUrl ? '' : 'disabled'}`}
                  href={origUrl ?? undefined}
                  download="voiceqas-original.wav"
                  aria-disabled={!origUrl}
                  onClick={(e) => {
                    if (!origUrl) e.preventDefault();
                  }}
                >
                  Download
                </a>
              </div>
            </div>
            <div className="analysis-audio-card">
              <h3 className="subhead">Audio mix (strip servidor)</h3>
              <p className="muted">
                WAV após channel strip no servidor (timeline completa, sem máscara Silero).
                {mixExportLoading ? ' Gerando...' : ''}
                {mixExportError ? ` Fallback preview: ${mixExportError}` : ''}
              </p>
              <div className="row analysis-audio-actions">
                <button
                  type="button"
                  className="btn"
                  disabled={!mixUrl || mixExportLoading}
                  onClick={() => {
                    if (playingId === 'mix') stopPlay();
                    else if (mixUrl) void playUrl('mix', mixUrl, 'Mix');
                  }}
                >
                  {playingId === 'mix' ? 'Parar' : 'Ouvir mix'}
                </button>
                <a
                  className={`btn ${mixUrl ? '' : 'disabled'}`}
                  href={mixUrl ?? undefined}
                  download="voiceqas-mix-strip.wav"
                  aria-disabled={!mixUrl}
                  onClick={(e) => {
                    if (!mixUrl) e.preventDefault();
                  }}
                >
                  Download
                </a>
              </div>
            </div>
          </div>
          <div className="analysis-spectro-pair">
            <Spectrogram
              pcm={pcm}
              sampleRate={pcmRate}
              turns={beforeTurns}
              title="Espectro original"
            />
            <Spectrogram
              pcm={serverMixPcm ?? mixPreview?.pcm ?? null}
              sampleRate={serverMixPcm ? serverMixRate : (mixPreview?.sampleRate ?? pcmRate)}
              turns={afterTurns}
              title={
                serverMixPcm
                  ? 'Espectro mix (strip servidor)'
                  : `Espectro mix preview (${mixPreview?.mode ?? '?'})`
              }
            />
          </div>
          <div className="analysis-dia-pair">
            <DiarizationPanel
              turns={afterTurns}
              primarySpeaker={after?.stt?.diarization?.primary_speaker}
              speakers={after?.stt?.diarization?.speakers}
              title="Interlocutores (mix depois)"
              hint={
                !mix.silero_vad_enabled
                  ? 'Ligue Silero VAD na mesa e processe de novo para diarizacao.'
                  : sttLoading
                    ? 'Aguardando STT…'
                    : afterTurns.length === 0
                      ? 'Sem turnos no pass depois — confira Silero ON e grave dialogos com pausas.'
                      : undefined
              }
            />
            <DiarizationPanel
              turns={beforeTurns}
              primarySpeaker={before?.stt?.diarization?.primary_speaker}
              speakers={before?.stt?.diarization?.speakers}
              title="Interlocutores (legado antes)"
              hint="Pass legado desliga diarizacao — esperado sem turnos."
            />
          </div>
        </section>
      )}

      <section className="analysis-section panel">
        <h2 className="analysis-step">6. STT</h2>
        <div className="grid-2">
          <label>
            Modelo
            <select
              value={config.sttModel}
              onChange={(e) =>
                patchConfig({ sttModel: e.target.value as CodecConfig['sttModel'] })
              }
            >
              <option value="auto">auto</option>
              <option value="parakeet">parakeet</option>
              <option value="whisper">whisper</option>
            </select>
          </label>
          <label>
            Provider
            <select
              value={config.sttProvider}
              onChange={(e) =>
                patchConfig({ sttProvider: e.target.value as CodecConfig['sttProvider'] })
              }
            >
              <option value="cpu">cpu</option>
              <option value="cuda">cuda</option>
            </select>
          </label>
        </div>
        <p className="muted">
          Diff nas caixas: vermelho = so no antes, verde = so no depois.
          {sttLoading ? ' Transcrevendo…' : ''}
        </p>
        <div className="analysis-stt-pair">
          <div data-testid="stt-before-box">
            <div className="analysis-panel-head">
              <h3 className="subhead">Texto antes</h3>
              <CopyTextButton text={beforeText} />
            </div>
            <SttBlock
              stt={before?.stt}
              text={beforeText}
              chunks={beforeChunks}
              pending={sttLoading && !before?.stt}
              testId="stt-before-text"
            />
          </div>
          <div data-testid="stt-after-box">
            <div className="analysis-panel-head">
              <h3 className="subhead">Texto depois</h3>
              <CopyTextButton text={afterText} />
            </div>
            <SttBlock
              stt={after?.stt}
              text={afterText}
              chunks={afterChunks}
              pending={sttLoading && !after?.stt}
              testId="stt-after-text"
            />
          </div>
        </div>
      </section>

      <LlmAnalysisPanel
        loading={llmLoading}
        analysis={llmText}
        error={llmError}
        model={llmModel}
        formatOk={llmFormatOk}
        formatErrors={llmFormatErrors}
        attempts={llmAttempts}
        disabled={!canLlm || llmLoading}
        onAnalyze={() => void runLlm()}
      />
    </div>
  );
}

function SttBlock({
  stt,
  text,
  chunks,
  pending,
  testId,
}: {
  stt: SttResult | null | undefined;
  text: string;
  chunks: ReturnType<typeof beforeViewChunks>;
  pending?: boolean;
  testId?: string;
}) {
  if (pending) {
    return <p className="muted">Transcrevendo…</p>;
  }
  if (!stt) {
    return <p className="muted">(ainda sem STT — rode Processar)</p>;
  }
  if (stt.ok === false && stt.error) {
    return <p className="error">{stt.error}</p>;
  }
  if (!text.trim()) {
    const hint =
      stt.model === 'diarization-filtered'
        ? 'Filtro primary removeu a fala — desligue Primary ou Silero no mix e processe de novo.'
        : stt.model === 'vad-filtered'
          ? 'VAD removeu toda a fala neste pass.'
          : 'Sem texto nesta passagem.';
    return (
      <>
        <p className="stt-text">(vazio)</p>
        <p className="muted">{hint}</p>
        <p className="muted">
          {stt.model} · {stt.processing_ms ?? '—'} ms
        </p>
      </>
    );
  }
  return (
    <div data-testid={testId}>
      <DiffText chunks={chunks} />
      <p className="muted" data-testid={testId ? `${testId}-meta` : undefined}>
        {stt.model} · {stt.processing_ms ?? '—'} ms · {text.trim().split(/\s+/).filter(Boolean).length} palavras
      </p>
    </div>
  );
}

function PassMetrics({ label, pass }: { label: string; pass: PassState | null }) {
  if (!pass?.summary) {
    return (
      <div className="analysis-pass-metrics">
        <h3>{label}</h3>
        <p className="muted">—</p>
      </div>
    );
  }
  const s = pass.summary;
  return (
    <div className="analysis-pass-metrics">
      <h3>{label}</h3>
      <ul className="metrics">
        <li>
          Score: <strong>{s.composite_score.toFixed(1)}</strong>
        </li>
        <li>
          Ready:{' '}
          <span className={s.stt_ready ? 'badge ok' : 'badge bad'}>
            {s.stt_ready ? 'SIM' : 'NAO'}
          </span>{' '}
          ({(s.ready_ratio * 100).toFixed(0)}%)
        </li>
        <li>Risk: {(s.stt_risk * 100).toFixed(0)}%</li>
        <li>
          Mix: strip AGC{' '}
          {(pass.mix.strip?.agc?.enabled ?? pass.mix.agc_enabled) ? 'on' : 'off'} · NR{' '}
          {(pass.mix.strip?.nr?.enabled ?? pass.mix.rnnoise_enabled) ? 'on' : 'off'} · Dia{' '}
          {pass.mix.silero_vad_enabled ? 'on' : 'off'} · Prim{' '}
          {pass.mix.focus_primary ? 'on' : 'off'}
        </li>
      </ul>
    </div>
  );
}
