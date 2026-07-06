export type AudioFormat =
  | 'pcm_s16le_8k'
  | 'pcm_s16le_16k'
  | 'rtp_pcmu'
  | 'rtp_pcma'
  | 'rtp_g722'
  | 'rtp_g729';

export type SttModel = 'parakeet' | 'whisper' | 'auto';

export type Transport = 'rest' | 'websocket' | 'grpc';

export interface CodecConfig {
  format: AudioFormat;
  sampleRate: 8000 | 16000;
  frameMs: 20 | 40 | 60;
  simulateClipping: boolean;
  clippingGain: number;
  simulatePacketLossPct: number;
  sessionId: string;
  sttModel: SttModel;
}

export interface WindowMetrics {
  window_start_ms: number;
  rms_dbfs: number;
  peak_dbfs: number;
  clipping_ratio: number;
  snr_estimate_db: number;
  silence_ratio: number;
  spectral_flatness: number;
  packet_loss_pct: number;
  jitter_ms: number;
  composite_score: number;
  stt_ready: boolean;
  session_id?: string;
}

export interface BatchResult {
  composite_score: number;
  stt_ready: boolean;
  windows: WindowMetrics[];
  stt_ready_segments: Array<{ start_ms: number; end_ms: number }>;
}

export interface SttSegment {
  start_ms: number;
  end_ms: number;
  text: string;
}

export interface SttResult {
  text: string;
  model: string;
  language: string;
  duration_ms: number;
  processing_ms: number;
  segments: SttSegment[];
  ok: boolean;
  error?: string;
}

export const FORMAT_ENUM: Record<AudioFormat, number> = {
  pcm_s16le_8k: 1,
  pcm_s16le_16k: 2,
  rtp_pcmu: 3,
  rtp_pcma: 4,
  rtp_g722: 5,
  rtp_g729: 6,
};

export const COMPARE_PRESETS: Array<{
  id: string;
  label: string;
  config: Omit<CodecConfig, 'sessionId' | 'sttModel'>;
}> = [
  {
    id: 'pcm8',
    label: 'PCM 8 kHz',
    config: {
      format: 'pcm_s16le_8k',
      sampleRate: 8000,
      frameMs: 20,
      simulateClipping: false,
      clippingGain: 3.5,
      simulatePacketLossPct: 0,
    },
  },
  {
    id: 'pcm16',
    label: 'PCM 16 kHz',
    config: {
      format: 'pcm_s16le_16k',
      sampleRate: 16000,
      frameMs: 20,
      simulateClipping: false,
      clippingGain: 3.5,
      simulatePacketLossPct: 0,
    },
  },
  {
    id: 'pcmu',
    label: 'RTP G.711 μ-law',
    config: {
      format: 'rtp_pcmu',
      sampleRate: 8000,
      frameMs: 20,
      simulateClipping: false,
      clippingGain: 3.5,
      simulatePacketLossPct: 0,
    },
  },
  {
    id: 'pcma',
    label: 'RTP G.711 A-law',
    config: {
      format: 'rtp_pcma',
      sampleRate: 8000,
      frameMs: 20,
      simulateClipping: false,
      clippingGain: 3.5,
      simulatePacketLossPct: 0,
    },
  },
  {
    id: 'g722',
    label: 'RTP G.722 (wideband)',
    config: {
      format: 'rtp_g722',
      sampleRate: 16000,
      frameMs: 20,
      simulateClipping: false,
      clippingGain: 3.5,
      simulatePacketLossPct: 0,
    },
  },
  {
    id: 'g729',
    label: 'RTP G.729',
    config: {
      format: 'rtp_g729',
      sampleRate: 8000,
      frameMs: 20,
      simulateClipping: false,
      clippingGain: 3.5,
      simulatePacketLossPct: 0,
    },
  },
  {
    id: 'clip',
    label: 'PCM 8k + clipping',
    config: {
      format: 'pcm_s16le_8k',
      sampleRate: 8000,
      frameMs: 20,
      simulateClipping: true,
      clippingGain: 5,
      simulatePacketLossPct: 0,
    },
  },
  {
    id: 'pcmu_loss',
    label: 'RTP PCMU (perda 10% no envio)',
    config: {
      format: 'rtp_pcmu',
      sampleRate: 8000,
      frameMs: 20,
      simulateClipping: false,
      clippingGain: 3.5,
      simulatePacketLossPct: 10,
    },
  },
];

export function formatConfigLabel(config: CodecConfig): string {
  const fmt = config.format.replace(/_/g, ' ');
  const extras: string[] = [];
  if (config.simulateClipping) extras.push(`clip ${config.clippingGain}x`);
  if (config.simulatePacketLossPct > 0) extras.push(`loss ${config.simulatePacketLossPct}%`);
  extras.push(`${config.frameMs}ms`);
  return `${fmt} @ ${config.sampleRate}Hz${extras.length ? ` (${extras.join(', ')})` : ''}`;
}

export const DEFAULT_CODEC: CodecConfig = {
  format: 'pcm_s16le_8k',
  sampleRate: 8000,
  frameMs: 20,
  simulateClipping: false,
  clippingGain: 3.5,
  simulatePacketLossPct: 0,
  sessionId: `web-${Date.now()}`,
  sttModel: 'auto',
};

export function sampleRateForFormat(format: AudioFormat): 8000 | 16000 {
  if (format === 'pcm_s16le_16k' || format === 'rtp_g722') return 16000;
  return 8000;
}
