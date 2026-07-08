import type { PipelineNode, PipelineSnapshot } from '../types';

export const INBOUND_STAGES = [
  'sip_in',
  'rtp_ingress',
  'decode_vqa',
  'agc_vqa',
  'enhancement',
  'vqa',
  'stt_gate',
  'decode_stt',
  'agc_stt',
  'resample_16k',
  'stt_buffer',
  'vad',
  'asr',
  'ai_agent',
] as const;

export const OUTBOUND_STAGES = [
  'agent_pcm_in',
  'resample_out',
  'peak_limit',
  'encode',
  'rtp_packetize',
  'rtp_egress',
  'sip_out',
] as const;

export function formatBytes(n: number): string {
  if (!n || n <= 0) return '0 B';
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / (1024 * 1024)).toFixed(2)} MB`;
}

export function formatBps(n: number): string {
  if (!n || n <= 0) return '';
  return `${formatBytes(n)}/s`;
}

/** Jitter sempre visível nas caixinhas (ms). */
export function formatStageJitter(jitterMs: number): string {
  return `${(jitterMs || 0).toFixed(1)} ms`;
}

/** Latência de estágio (p50, fallback p95). */
export function formatStageLatency(latencyP50: number, latencyP95 = 0): string {
  const ms = latencyP50 > 0 ? latencyP50 : latencyP95;
  return `${ms.toFixed(1)} ms`;
}

/** Tempo total do estágio em segundos (processing_ms ou latência). */
export function formatStageTotalTimeS(processingMs?: number, latencyP50 = 0, latencyP95 = 0): string {
  const ms = processingMs && processingMs > 0
    ? processingMs
    : latencyP50 > 0
      ? latencyP50
      : latencyP95;
  return `${(ms / 1000).toFixed(2)} s`;
}

/** Throughput live em Kb/s (kilobits por segundo). */
export function formatStageKbps(bytesPerSec: number): string {
  const kbps = ((bytesPerSec || 0) * 8) / 1000;
  return `${kbps.toFixed(1)} Kb/s`;
}

export interface StageLiveMetrics {
  jitter: string;
  latency: string;
  totalTime: string;
  kbps: string;
}

/** Quatro métricas fixas exibidas em cada caixinha Inbound/Outbound. */
export function stageLiveMetrics(node: PipelineNode): StageLiveMetrics {
  const m = node.metrics;
  return {
    jitter: formatStageJitter(m.jitter_ms),
    latency: formatStageLatency(m.latency_ms_p50, m.latency_ms_p95),
    totalTime: formatStageTotalTimeS(m.processing_ms, m.latency_ms_p50, m.latency_ms_p95),
    kbps: formatStageKbps(m.bytes_per_sec),
  };
}

export function scoreClass(score: number): 'ok' | 'warn' | 'bad' | 'idle' {
  if (score <= 0) return 'idle';
  if (score >= 65) return 'ok';
  if (score >= 45) return 'warn';
  return 'bad';
}

export function nodeByName(snapshot: PipelineSnapshot | null, name: string): PipelineNode | undefined {
  return snapshot?.nodes.find((n) => n.name === name);
}

export function activeLinkCount(snapshot: PipelineSnapshot | null): number {
  if (!snapshot) return 0;
  return snapshot.links.filter((l) => l.value > 0).length;
}

export interface PipelineStageForApm {
  name: string;
  label: string;
  durationMs: number;
  node: PipelineNode;
}

export function pipelineStagesForApm(snapshot: PipelineSnapshot | null): PipelineStageForApm[] {
  const names = [...INBOUND_STAGES, ...OUTBOUND_STAGES];
  const out: PipelineStageForApm[] = [];
  for (const name of names) {
    const node = snapshot?.nodes.find((n) => n.name === name);
    if (!node) continue;
    const durationMs = node.metrics.latency_ms_p50 || node.metrics.processing_ms || 0;
    if (durationMs <= 0 && node.metrics.bytes_out <= 0) continue;
    out.push({ name, label: node.label, durationMs: Math.max(durationMs, 0.2), node });
  }
  return out;
}

/** @deprecated use pipelineStagesForApm */
export function pipelineStageDurations(snapshot: PipelineSnapshot | null) {
  return pipelineStagesForApm(snapshot).map(({ name, label, durationMs }) => ({ name, label, durationMs }));
}

function setMetric(
  attrs: Record<string, string | number>,
  key: string,
  val: number | boolean | undefined,
  fmt?: (v: number) => string | number,
) {
  if (val === undefined) return;
  if (typeof val === 'boolean') {
    attrs[key] = val ? 'true' : 'false';
    return;
  }
  if (val === 0) return;
  attrs[key] = fmt ? fmt(val) : val;
}

export function pipelineNodeToSpanAttributes(
  node: PipelineNode,
  snapshot: PipelineSnapshot | null,
): Record<string, string | number> {
  const m = node.metrics;
  const attrs: Record<string, string | number> = {
    'pipeline.stage': node.name,
    'pipeline.label': node.label,
    'pipeline.direction': node.direction,
    'pipeline.activity': stageActivity(node),
  };
  if (snapshot?.scope) attrs['pipeline.scope'] = snapshot.scope;
  if (snapshot?.session_id) attrs['pipeline.session_id'] = snapshot.session_id;
  if (snapshot?.codec) attrs['pipeline.codec'] = snapshot.codec;
  if (snapshot?.ts_ms) attrs['pipeline.snapshot_ts'] = snapshot.ts_ms;

  setMetric(attrs, 'bytes_in', m.bytes_in);
  setMetric(attrs, 'bytes_out', m.bytes_out);
  setMetric(attrs, 'bytes_per_sec', m.bytes_per_sec);
  setMetric(attrs, 'packets_in', m.packets_in);
  setMetric(attrs, 'packets_out', m.packets_out);
  setMetric(attrs, 'dropped_bytes', m.dropped_bytes);
  setMetric(attrs, 'latency_ms_p50', m.latency_ms_p50);
  setMetric(attrs, 'latency_ms_p95', m.latency_ms_p95);
  setMetric(attrs, 'processing_ms', m.processing_ms);
  setMetric(attrs, 'buffer_ms', m.buffer_ms);
  setMetric(attrs, 'jitter_ms', m.jitter_ms, (v) => Number(v.toFixed(2)));
  setMetric(attrs, 'packet_loss_pct', m.packet_loss_pct, (v) => Number(v.toFixed(2)));
  setMetric(attrs, 'composite_score', m.composite_score, (v) => Math.round(v));
  setMetric(attrs, 'snr_db', m.snr_db, (v) => Number(v.toFixed(1)));
  setMetric(attrs, 'rms_dbfs', m.rms_dbfs, (v) => Number(v.toFixed(1)));
  setMetric(attrs, 'stt_ready', m.stt_ready);

  return attrs;
}

export function maxBytesOut(nodes: PipelineNode[]): number {
  let max = 1;
  for (const n of nodes) {
    if (n.metrics.bytes_out > max) max = n.metrics.bytes_out;
  }
  return max;
}

export function stageActivity(node: PipelineNode): 'live' | 'warm' | 'idle' {
  const m = node.metrics;
  if (m.bytes_per_sec > 0) return 'live';
  if (m.bytes_out > 0 || m.bytes_in > 0) return 'warm';
  return 'idle';
}
