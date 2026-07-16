import { useMemo, useState } from 'react';
import type { DspStripState, MixStageId, PipelineMixState } from '../../lib/domain/pipeline-mix';
import { MIX_STAGES, stageEnabled, syncMixFlags } from '../../lib/domain/pipeline-mix';
import { eqResponseDb } from '../../lib/domain/dsp-preview';

interface Props {
  mix: PipelineMixState;
  onChange: (next: PipelineMixState) => void;
  showCodecToggle?: boolean;
}

const DSP_IDS: MixStageId[] = ['nr', 'hpf', 'eq', 'deesser', 'comp', 'lim', 'agc'];

function faderHeight(mix: PipelineMixState, id: MixStageId): string {
  if (!stageEnabled(mix, id)) return '12%';
  switch (id) {
    case 'nr':
      return `${12 + mix.strip.nr.wet_dry * 70}%`;
    case 'hpf':
      return `${20 + Math.min(80, mix.strip.hpf.cutoff_hz) / 2}%`;
    case 'eq': {
      const avg =
        mix.strip.eq.bands.reduce((s, b) => s + Math.abs(b.gain_db), 0) /
        Math.max(1, mix.strip.eq.bands.length);
      return `${25 + Math.min(avg, 6) * 10}%`;
    }
    case 'deesser':
      return `${40 + Math.min(30, Math.abs(mix.strip.deesser.threshold_db))}%`;
    case 'comp':
      return `${35 + mix.strip.compressor.ratio * 8}%`;
    case 'lim':
      return `${50 + Math.abs(mix.strip.limiter.ceiling_dbfs) * 8}%`;
    case 'agc':
      return `${45 + Math.abs(mix.strip.agc.target_rms_dbfs)}%`;
    default:
      return '78%';
  }
}

function EqCurve({ strip }: { strip: DspStripState }) {
  const points = useMemo(() => {
    const sr = 16000;
    const freqs: number[] = [];
    for (let i = 0; i < 64; i++) {
      freqs.push(40 * 10 ** ((i / 63) * Math.log10(8000 / 40)));
    }
    return freqs.map((f) => {
      const db = eqResponseDb(
        sr,
        f,
        strip.hpf.enabled ? strip.hpf.cutoff_hz : null,
        strip.eq.enabled ? strip.eq.bands : [],
      );
      return { f, db };
    });
  }, [strip]);

  const w = 320;
  const h = 120;
  const minDb = -12;
  const maxDb = 12;
  const path = points
    .map((p, i) => {
      const x = (i / (points.length - 1)) * (w - 8) + 4;
      const y = h - 4 - ((p.db - minDb) / (maxDb - minDb)) * (h - 8);
      return `${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(' ');

  return (
    <svg className="eq-curve" viewBox={`0 0 ${w} ${h}`} role="img" aria-label="Curva EQ">
      <line x1="4" y1={h / 2} x2={w - 4} y2={h / 2} className="eq-curve-zero" />
      <path d={path} className="eq-curve-path" fill="none" />
    </svg>
  );
}

export function MixDesk({ mix, onChange, showCodecToggle = true }: Props) {
  const [selected, setSelected] = useState<MixStageId>('eq');

  const commit = (next: PipelineMixState) => onChange(syncMixFlags(next));
  const patch = (p: Partial<PipelineMixState>) => commit({ ...mix, ...p });
  const patchGate = (p: Partial<PipelineMixState['gate']>) =>
    commit({ ...mix, gate: { ...mix.gate, ...p } });
  const patchStrip = (p: Partial<DspStripState>) =>
    commit({ ...mix, strip: { ...mix.strip, ...p } });

  const setStageOn = (id: MixStageId, v: boolean) => {
    const s = mix.strip;
    switch (id) {
      case 'nr':
        patchStrip({ nr: { ...s.nr, enabled: v } });
        break;
      case 'hpf':
        patchStrip({ hpf: { ...s.hpf, enabled: v } });
        break;
      case 'eq':
        patchStrip({ eq: { ...s.eq, enabled: v } });
        break;
      case 'deesser':
        patchStrip({ deesser: { ...s.deesser, enabled: v } });
        break;
      case 'comp':
        patchStrip({ compressor: { ...s.compressor, enabled: v } });
        break;
      case 'lim':
        patchStrip({ limiter: { ...s.limiter, enabled: v } });
        break;
      case 'agc':
        patchStrip({ agc: { ...s.agc, enabled: v } });
        break;
      case 'silero':
        patch({
          silero_vad_enabled: v,
          focus_primary: v ? mix.focus_primary : false,
        });
        break;
      case 'primary':
        if (v) patch({ focus_primary: true, silero_vad_enabled: true });
        else patch({ focus_primary: false });
        break;
      default:
        break;
    }
  };

  const stripPanel = () => {
    const s = mix.strip;
    if (selected === 'nr') {
      return (
        <label>
          Wet/dry ({s.nr.wet_dry.toFixed(2)})
          <input
            type="range"
            min={0}
            max={100}
            value={Math.round(s.nr.wet_dry * 100)}
            onChange={(e) =>
              patchStrip({ nr: { ...s.nr, wet_dry: Number(e.target.value) / 100 } })
            }
          />
        </label>
      );
    }
    if (selected === 'hpf') {
      return (
        <label>
          Cutoff Hz ({s.hpf.cutoff_hz})
          <input
            type="range"
            min={40}
            max={200}
            value={s.hpf.cutoff_hz}
            onChange={(e) =>
              patchStrip({ hpf: { ...s.hpf, cutoff_hz: Number(e.target.value) } })
            }
          />
        </label>
      );
    }
    if (selected === 'eq') {
      return (
        <>
          <EqCurve strip={s} />
          <div className="eq-bands">
            {s.eq.bands.map((band, i) => (
              <div key={i} className="eq-band">
                <div className="eq-band-title">Banda {i + 1}</div>
                <label>
                  Freq {band.freq_hz} Hz
                  <input
                    type="range"
                    min={80}
                    max={8000}
                    step={10}
                    value={band.freq_hz}
                    onChange={(e) => {
                      const bands = s.eq.bands.map((b, j) =>
                        j === i ? { ...b, freq_hz: Number(e.target.value) } : b,
                      );
                      patchStrip({ eq: { ...s.eq, bands } });
                    }}
                  />
                </label>
                <label>
                  Gain {band.gain_db.toFixed(1)} dB
                  <input
                    type="range"
                    min={-12}
                    max={12}
                    step={0.5}
                    value={band.gain_db}
                    onChange={(e) => {
                      const bands = s.eq.bands.map((b, j) =>
                        j === i ? { ...b, gain_db: Number(e.target.value) } : b,
                      );
                      patchStrip({ eq: { ...s.eq, bands } });
                    }}
                  />
                </label>
                <label>
                  Q {band.q.toFixed(2)}
                  <input
                    type="range"
                    min={0.3}
                    max={4}
                    step={0.05}
                    value={band.q}
                    onChange={(e) => {
                      const bands = s.eq.bands.map((b, j) =>
                        j === i ? { ...b, q: Number(e.target.value) } : b,
                      );
                      patchStrip({ eq: { ...s.eq, bands } });
                    }}
                  />
                </label>
              </div>
            ))}
          </div>
        </>
      );
    }
    if (selected === 'deesser') {
      const d = s.deesser;
      return (
        <div className="mix-sliders grid-2">
          <label>
            Center {d.center_hz} Hz
            <input
              type="range"
              min={4000}
              max={9000}
              step={50}
              value={d.center_hz}
              onChange={(e) =>
                patchStrip({ deesser: { ...d, center_hz: Number(e.target.value) } })
              }
            />
          </label>
          <label>
            BW {d.bandwidth_hz} Hz
            <input
              type="range"
              min={500}
              max={4000}
              step={50}
              value={d.bandwidth_hz}
              onChange={(e) =>
                patchStrip({ deesser: { ...d, bandwidth_hz: Number(e.target.value) } })
              }
            />
          </label>
          <label>
            Thr {d.threshold_db} dB
            <input
              type="range"
              min={-40}
              max={-10}
              value={d.threshold_db}
              onChange={(e) =>
                patchStrip({ deesser: { ...d, threshold_db: Number(e.target.value) } })
              }
            />
          </label>
          <label>
            Ratio {d.ratio.toFixed(1)}:1
            <input
              type="range"
              min={1}
              max={8}
              step={0.1}
              value={d.ratio}
              onChange={(e) =>
                patchStrip({ deesser: { ...d, ratio: Number(e.target.value) } })
              }
            />
          </label>
        </div>
      );
    }
    if (selected === 'comp') {
      const c = s.compressor;
      return (
        <div className="mix-sliders grid-2">
          <label>
            Thr {c.threshold_db} dB
            <input
              type="range"
              min={-40}
              max={-6}
              value={c.threshold_db}
              onChange={(e) =>
                patchStrip({ compressor: { ...c, threshold_db: Number(e.target.value) } })
              }
            />
          </label>
          <label>
            Ratio {c.ratio.toFixed(1)}:1
            <input
              type="range"
              min={1}
              max={8}
              step={0.1}
              value={c.ratio}
              onChange={(e) =>
                patchStrip({ compressor: { ...c, ratio: Number(e.target.value) } })
              }
            />
          </label>
          <label>
            Attack {c.attack_ms} ms
            <input
              type="range"
              min={1}
              max={50}
              value={c.attack_ms}
              onChange={(e) =>
                patchStrip({ compressor: { ...c, attack_ms: Number(e.target.value) } })
              }
            />
          </label>
          <label>
            Release {c.release_ms} ms
            <input
              type="range"
              min={20}
              max={300}
              value={c.release_ms}
              onChange={(e) =>
                patchStrip({ compressor: { ...c, release_ms: Number(e.target.value) } })
              }
            />
          </label>
          <label>
            Makeup {c.makeup_db.toFixed(1)} dB
            <input
              type="range"
              min={0}
              max={12}
              step={0.5}
              value={c.makeup_db}
              onChange={(e) =>
                patchStrip({ compressor: { ...c, makeup_db: Number(e.target.value) } })
              }
            />
          </label>
        </div>
      );
    }
    if (selected === 'lim') {
      return (
        <label>
          Ceiling {s.limiter.ceiling_dbfs} dBFS
          <input
            type="range"
            min={-6}
            max={0}
            step={0.5}
            value={s.limiter.ceiling_dbfs}
            onChange={(e) =>
              patchStrip({
                limiter: { ...s.limiter, ceiling_dbfs: Number(e.target.value) },
              })
            }
          />
        </label>
      );
    }
    if (selected === 'agc') {
      const a = s.agc;
      return (
        <div className="mix-sliders grid-2">
          <label>
            Target {a.target_rms_dbfs} dBFS
            <input
              type="range"
              min={-30}
              max={-10}
              value={a.target_rms_dbfs}
              onChange={(e) =>
                patchStrip({ agc: { ...a, target_rms_dbfs: Number(e.target.value) } })
              }
            />
          </label>
          <label>
            Max gain {a.max_gain_db} dB
            <input
              type="range"
              min={0}
              max={36}
              value={a.max_gain_db}
              onChange={(e) =>
                patchStrip({ agc: { ...a, max_gain_db: Number(e.target.value) } })
              }
            />
          </label>
        </div>
      );
    }
    return (
      <p className="muted">Selecione um canal DSP (NR…AGC) para ajustar knobs.</p>
    );
  };

  return (
    <div className="mix-desk-body">
      {showCodecToggle && (
        <label className="checkbox mix-codec-opt">
          <input
            type="checkbox"
            checked={mix.simulate_trunk}
            onChange={(e) => patch({ simulate_trunk: e.target.checked })}
          />
          Simular tronco (codec G.711 / G.722 / PCM) — desligado envia PCM16 bruto
        </label>
      )}

      <h3 className="subhead">Mesa de mix — channel strip</h3>
      <p className="muted mix-desk-hint">
        Ordem: NR → HPF → EQ → DeEss → Comp → Lim → AGC. Clique no canal para knobs.
        Processar aplica os mesmos valores no servidor.
      </p>
      <div className="mix-strip">
        {MIX_STAGES.filter((s) => s.id !== 'vqa' && s.id !== 'rnnoise').map((s) => {
          const on = stageEnabled(mix, s.id);
          const isDsp = DSP_IDS.includes(s.id);
          const sel = selected === s.id;
          return (
            <div
              key={s.id}
              className={`mix-channel ${on ? 'on' : 'off'} ${sel ? 'selected' : ''}`}
              onClick={() => {
                if (isDsp || s.id === 'silero' || s.id === 'primary') setSelected(s.id);
              }}
              role={isDsp ? 'button' : undefined}
            >
              <div className="mix-channel-label">{s.label}</div>
              <div className="mix-fader-track" aria-hidden>
                <div
                  className="mix-fader-fill"
                  style={{ height: isDsp ? faderHeight(mix, s.id) : on ? '78%' : '12%' }}
                />
              </div>
              {s.controllable ? (
                <label className="mix-toggle" onClick={(e) => e.stopPropagation()}>
                  <input
                    type="checkbox"
                    checked={on}
                    onChange={(e) => setStageOn(s.id, e.target.checked)}
                  />
                  {on ? 'ON' : 'OFF'}
                </label>
              ) : (
                <span className="mix-fixed">FIXO</span>
              )}
              <div className="mix-channel-desc">{s.description}</div>
            </div>
          );
        })}
      </div>

      <div className="mix-inspector">
        <h3 className="subhead">Canal: {selected.toUpperCase()}</h3>
        <div className="mix-inspector-body">{stripPanel()}</div>
      </div>

      <p className="muted mix-primary-hint">
        Primary só aplica com Silero ON (crop do interlocutor principal no STT).
      </p>

      <h3 className="subhead">Gate VQA</h3>
      <div className="mix-sliders grid-2">
        <label>
          Score minimo ready ({mix.gate.stt_ready_threshold})
          <input
            type="range"
            min={40}
            max={95}
            value={mix.gate.stt_ready_threshold}
            onChange={(e) => patchGate({ stt_ready_threshold: Number(e.target.value) })}
          />
        </label>
        <label>
          SNR minimo dB ({mix.gate.min_snr_db})
          <input
            type="range"
            min={0}
            max={30}
            value={mix.gate.min_snr_db}
            onChange={(e) => patchGate({ min_snr_db: Number(e.target.value) })}
          />
        </label>
        <label>
          Silencio max. ready ({(mix.gate.max_silence_ratio_for_ready * 100).toFixed(0)}%)
          <input
            type="range"
            min={20}
            max={80}
            value={Math.round(mix.gate.max_silence_ratio_for_ready * 100)}
            onChange={(e) =>
              patchGate({ max_silence_ratio_for_ready: Number(e.target.value) / 100 })
            }
          />
        </label>
        <label>
          Histerese OK ({mix.gate.hysteresis_ok_windows})
          <input
            type="range"
            min={1}
            max={5}
            value={mix.gate.hysteresis_ok_windows}
            onChange={(e) => patchGate({ hysteresis_ok_windows: Number(e.target.value) })}
          />
        </label>
        <label className="checkbox">
          <input
            type="checkbox"
            checked={mix.gate.split_quality_from_presence}
            onChange={(e) => patchGate({ split_quality_from_presence: e.target.checked })}
          />
          Qualidade != presenca (nao penalizar silencio no score)
        </label>
        <label className="checkbox">
          <input
            type="checkbox"
            checked={mix.require_stt_ready}
            onChange={(e) => patch({ require_stt_ready: e.target.checked })}
          />
          require_stt_ready (referencia do gate)
        </label>
      </div>
    </div>
  );
}
