import { useCallback, useEffect, useState } from 'react';
import { MixDesk } from '../analysis/MixDesk';
import {
  fetchPipelineDefault,
  savePipelineDefault,
  type PipelineDefaultResponse,
} from '../../lib/api/config';
import { probeOpsRole } from '../../context/CommandCenterContext';
import {
  DEFAULT_GATE,
  DEFAULT_MIX,
  DEFAULT_STRIP,
  type PipelineMixState,
} from '../../lib/domain/pipeline-mix';

function pipelineToMix(body: PipelineDefaultResponse): PipelineMixState {
  const strip = body.audio?.strip as unknown as PipelineMixState['strip'] | undefined;
  return {
    ...DEFAULT_MIX,
    agc_enabled: strip?.agc?.enabled ?? DEFAULT_STRIP.agc.enabled,
    rnnoise_enabled: body.audio?.enhancement?.enabled ?? false,
    require_stt_ready: body.stt?.require_stt_ready ?? DEFAULT_MIX.require_stt_ready,
    focus_primary: body.stt?.diarization_focus_primary ?? DEFAULT_MIX.focus_primary,
    strip: strip ?? DEFAULT_STRIP,
    gate: DEFAULT_GATE,
  };
}

function mixToPayload(mix: PipelineMixState): PipelineDefaultResponse {
  return {
    audio: {
      enhancement: { enabled: mix.rnnoise_enabled, wet_dry: mix.strip.nr.wet_dry },
      strip: mix.strip as unknown as Record<string, unknown>,
    },
    stt: {
      require_stt_ready: mix.require_stt_ready,
      diarization_focus_primary: mix.focus_primary,
    },
  };
}

export function PipelineDefaultPanel() {
  const [mix, setMix] = useState<PipelineMixState>(DEFAULT_MIX);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [saved, setSaved] = useState(false);
  const role = probeOpsRole();
  const readOnly = role === 'read';

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const data = await fetchPipelineDefault();
      setMix(pipelineToMix(data));
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const save = async () => {
    setError(null);
    try {
      await savePipelineDefault(mixToPayload(mix));
      setSaved(true);
      setTimeout(() => setSaved(false), 2000);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  return (
    <section className="panel cc-pipeline-default">
      <div className="cc-panel-head">
        <div>
          <h2>Pipeline default (global)</h2>
          <p className="muted">Config base para todos os canais (S15 calibrado).</p>
        </div>
        <span className="badge ok">S15 default</span>
      </div>
      {loading && <p className="muted">Carregando...</p>}
      {error && <p className="error">{error}</p>}
      {!loading && (
        <>
          <MixDesk mix={mix} onChange={setMix} showCodecToggle={false} />
          {!readOnly && (
            <div className="row">
              <button type="button" className="btn primary" onClick={() => void save()}>
                Salvar pipeline default
              </button>
              {saved && <span className="ok">Salvo</span>}
            </div>
          )}
          {readOnly && <p className="muted">Modo leitura - token read nao pode salvar.</p>}
        </>
      )}
    </section>
  );
}
