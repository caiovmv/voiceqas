/** Gate / analyzer defaults - mirror voiceqas AnalyzerConfig + planned mix. */
export interface VqaGateParams {
  stt_ready_threshold: number;
  min_snr_db: number;
  max_clipping_ratio: number;
  max_silence_ratio: number;
  max_silence_ratio_for_ready: number;
  hysteresis_ok_windows: number;
  hysteresis_bad_windows: number;
  split_quality_from_presence: boolean;
}

export interface EqBandState {
  freq_hz: number;
  gain_db: number;
  q: number;
}

export interface DspStripState {
  nr: { enabled: boolean; wet_dry: number };
  hpf: { enabled: boolean; cutoff_hz: number };
  eq: { enabled: boolean; bands: EqBandState[] };
  deesser: {
    enabled: boolean;
    center_hz: number;
    bandwidth_hz: number;
    threshold_db: number;
    ratio: number;
    attack_ms: number;
    release_ms: number;
  };
  compressor: {
    enabled: boolean;
    threshold_db: number;
    ratio: number;
    attack_ms: number;
    release_ms: number;
    makeup_db: number;
  };
  limiter: { enabled: boolean; ceiling_dbfs: number };
  agc: {
    enabled: boolean;
    target_rms_dbfs: number;
    max_gain_db: number;
    attack_ms: number;
    release_ms: number;
  };
}

export interface PipelineMixState {
  /** Legacy mirrors of strip.agc / strip.nr for older UI bits. */
  agc_enabled: boolean;
  rnnoise_enabled: boolean;
  silero_vad_enabled: boolean;
  focus_primary: boolean;
  require_stt_ready: boolean;
  /** When false, send raw PCM16; when true, encode to selected trunk codec. */
  simulate_trunk: boolean;
  gate: VqaGateParams;
  strip: DspStripState;
}

export const DEFAULT_GATE: VqaGateParams = {
  stt_ready_threshold: 55,
  min_snr_db: 12,
  max_clipping_ratio: 0.02,
  max_silence_ratio: 0.6,
  max_silence_ratio_for_ready: 0.55,
  hysteresis_ok_windows: 2,
  hysteresis_bad_windows: 1,
  split_quality_from_presence: true,
};

/** STT/contact-center channel strip defaults (plan). */
/** Calibrated for Whisper match-before (S15): NR/DeEss off, mild EQ, soft comp. */
export const DEFAULT_STRIP: DspStripState = {
  nr: { enabled: false, wet_dry: 1.0 },
  hpf: { enabled: true, cutoff_hz: 80 },
  eq: {
    enabled: true,
    bands: [
      { freq_hz: 250, gain_db: -1.5, q: 1.0 },
      { freq_hz: 450, gain_db: -1.0, q: 1.2 },
      { freq_hz: 2500, gain_db: 1.0, q: 0.8 },
      { freq_hz: 3500, gain_db: 1.0, q: 0.8 },
    ],
  },
  deesser: {
    enabled: false,
    center_hz: 6500,
    bandwidth_hz: 2000,
    threshold_db: -25,
    ratio: 3,
    attack_ms: 1,
    release_ms: 40,
  },
  compressor: {
    enabled: true,
    threshold_db: -20,
    ratio: 2,
    attack_ms: 5,
    release_ms: 80,
    makeup_db: 0,
  },
  limiter: { enabled: true, ceiling_dbfs: -1 },
  agc: {
    enabled: true,
    target_rms_dbfs: -18,
    max_gain_db: 24,
    attack_ms: 5,
    release_ms: 100,
  },
};

/** A/B baseline: AGC only (legacy −20), no spectral / dynamics strip. */
export const LEGACY_STRIP: DspStripState = {
  nr: { enabled: false, wet_dry: 1.0 },
  hpf: { enabled: false, cutoff_hz: 80 },
  eq: {
    enabled: false,
    bands: DEFAULT_STRIP.eq.bands.map((b) => ({ ...b })),
  },
  deesser: { ...DEFAULT_STRIP.deesser, enabled: false },
  compressor: { ...DEFAULT_STRIP.compressor, enabled: false },
  limiter: { enabled: true, ceiling_dbfs: -3 },
  agc: {
    enabled: true,
    target_rms_dbfs: -20,
    max_gain_db: 24,
    attack_ms: 5,
    release_ms: 100,
  },
};

export const DEFAULT_MIX: PipelineMixState = {
  agc_enabled: DEFAULT_STRIP.agc.enabled,
  rnnoise_enabled: DEFAULT_STRIP.nr.enabled,
  silero_vad_enabled: true,
  focus_primary: false,
  require_stt_ready: true,
  simulate_trunk: false,
  gate: { ...DEFAULT_GATE },
  strip: structuredClone(DEFAULT_STRIP),
};

export const LEGACY_MIX: PipelineMixState = {
  agc_enabled: true,
  rnnoise_enabled: false,
  silero_vad_enabled: false,
  focus_primary: false,
  require_stt_ready: false,
  simulate_trunk: false,
  gate: {
    ...DEFAULT_GATE,
    max_silence_ratio_for_ready: 0.6,
    split_quality_from_presence: false,
  },
  strip: structuredClone(LEGACY_STRIP),
};

export type MixStageId =
  | 'decode'
  | 'nr'
  | 'hpf'
  | 'eq'
  | 'deesser'
  | 'comp'
  | 'lim'
  | 'agc'
  | 'rnnoise'
  | 'silero'
  | 'primary'
  | 'vqa'
  | 'stt';

export interface MixStageMeta {
  id: MixStageId;
  label: string;
  description: string;
  controllable: boolean;
}

export const MIX_STAGES: MixStageMeta[] = [
  { id: 'decode', label: 'Decode', description: 'RTP/PCM -> PCM16', controllable: false },
  { id: 'nr', label: 'NR', description: 'RNNoise wet/dry', controllable: true },
  { id: 'hpf', label: 'HPF', description: 'High-pass rumble', controllable: true },
  { id: 'eq', label: 'EQ', description: 'Parametric 4 bandas', controllable: true },
  { id: 'deesser', label: 'DeEss', description: 'Sibilancia 6.5 kHz', controllable: true },
  { id: 'comp', label: 'Comp', description: 'Compressor voz', controllable: true },
  { id: 'lim', label: 'Lim', description: 'Peak limiter', controllable: true },
  { id: 'agc', label: 'AGC', description: 'Nivel RMS', controllable: true },
  { id: 'silero', label: 'Silero', description: 'Turnos de fala', controllable: true },
  { id: 'primary', label: 'Primary', description: 'Foco interlocutor', controllable: true },
  { id: 'vqa', label: 'VQA', description: 'Gate qualidade', controllable: true },
  { id: 'stt', label: 'STT', description: 'ASR offline', controllable: false },
];

export function stageEnabled(mix: PipelineMixState, id: MixStageId): boolean {
  switch (id) {
    case 'decode':
    case 'vqa':
    case 'stt':
      return true;
    case 'nr':
    case 'rnnoise':
      return mix.strip.nr.enabled;
    case 'hpf':
      return mix.strip.hpf.enabled;
    case 'eq':
      return mix.strip.eq.enabled;
    case 'deesser':
      return mix.strip.deesser.enabled;
    case 'comp':
      return mix.strip.compressor.enabled;
    case 'lim':
      return mix.strip.limiter.enabled;
    case 'agc':
      return mix.strip.agc.enabled;
    case 'silero':
      return mix.silero_vad_enabled;
    case 'primary':
      return mix.focus_primary;
    default:
      return true;
  }
}

/** Keep legacy flags and strip.agc/nr in sync after a patch. */
export function syncMixFlags(mix: PipelineMixState): PipelineMixState {
  return {
    ...mix,
    agc_enabled: mix.strip.agc.enabled,
    rnnoise_enabled: mix.strip.nr.enabled,
  };
}

export function processFlagsFromMix(mix: PipelineMixState) {
  return {
    agc: mix.strip.agc.enabled,
    enhancement: mix.strip.nr.enabled,
    diarization: mix.silero_vad_enabled,
    focusPrimary: mix.focus_primary && mix.silero_vad_enabled,
  };
}

export function stripJsonForHeader(strip: DspStripState): string {
  return JSON.stringify(strip);
}
