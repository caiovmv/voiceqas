import { describe, expect, it } from 'vitest';
import {
  activeLinkCount,
  formatBytes,
  formatBps,
  formatStageJitter,
  formatStageKbps,
  formatStageLatency,
  formatStageTotalTimeS,
  pipelineNodeToSpanAttributes,
  pipelineStagesForApm,
  scoreClass,
  stageActivity,
  stageLiveMetrics,
} from './pipeline-stages';
import type { PipelineNode, PipelineSnapshot } from '../types';

describe('formatBytes', () => {
  it('formats scales', () => {
    expect(formatBytes(0)).toBe('0 B');
    expect(formatBytes(512)).toBe('512 B');
    expect(formatBytes(2048)).toBe('2.0 KB');
    expect(formatBytes(1_572_864)).toBe('1.50 MB');
  });
});

describe('formatBps', () => {
  it('returns empty for zero', () => {
    expect(formatBps(0)).toBe('');
    expect(formatBps(1024)).toBe('1.0 KB/s');
  });
});

describe('stageLiveMetrics', () => {
  const node: PipelineNode = {
    name: 'vad',
    label: 'Silero VAD',
    direction: 'inbound',
    metrics: {
      bytes_in: 1000,
      bytes_out: 1000,
      bytes_per_sec: 1250,
      composite_score: 0,
      jitter_ms: 2.34,
      latency_ms_p50: 12.5,
      latency_ms_p95: 18,
      packet_loss_pct: 0,
      dropped_bytes: 0,
      processing_ms: 450,
    },
  };

  it('always exposes four live fields', () => {
    const m = stageLiveMetrics(node);
    expect(m.jitter).toBe('2.3 ms');
    expect(m.latency).toBe('12.5 ms');
    expect(m.totalTime).toBe('0.45 s');
    expect(m.kbps).toBe('10.0 Kb/s');
  });

  it('formats zero metrics without hiding', () => {
    const idle = stageLiveMetrics({
      ...node,
      metrics: { ...node.metrics, jitter_ms: 0, latency_ms_p50: 0, latency_ms_p95: 0, processing_ms: 0, bytes_per_sec: 0 },
    });
    expect(idle.jitter).toBe('0.0 ms');
    expect(idle.latency).toBe('0.0 ms');
    expect(idle.totalTime).toBe('0.00 s');
    expect(idle.kbps).toBe('0.0 Kb/s');
  });
});

describe('formatStage helpers', () => {
  it('uses latency when processing_ms absent', () => {
    expect(formatStageTotalTimeS(undefined, 25, 0)).toBe('0.03 s');
    expect(formatStageLatency(0, 9)).toBe('9.0 ms');
    expect(formatStageKbps(500)).toBe('4.0 Kb/s');
    expect(formatStageJitter(0)).toBe('0.0 ms');
  });
});

describe('scoreClass', () => {
  it('maps thresholds', () => {
    expect(scoreClass(0)).toBe('idle');
    expect(scoreClass(100)).toBe('ok');
    expect(scoreClass(50)).toBe('warn');
    expect(scoreClass(30)).toBe('bad');
  });
});

describe('stageActivity', () => {
  const base: PipelineNode = {
    name: 'vqa',
    label: 'VQA',
    direction: 'inbound',
    metrics: {
      bytes_in: 0,
      bytes_out: 0,
      bytes_per_sec: 0,
      composite_score: 0,
      jitter_ms: 0,
      latency_ms_p50: 0,
      latency_ms_p95: 0,
      packet_loss_pct: 0,
      dropped_bytes: 0,
    },
  };

  it('detects live vs warm vs idle', () => {
    expect(stageActivity({ ...base, metrics: { ...base.metrics, bytes_per_sec: 100 } })).toBe('live');
    expect(stageActivity({ ...base, metrics: { ...base.metrics, bytes_out: 1000 } })).toBe('warm');
    expect(stageActivity(base)).toBe('idle');
  });
});

describe('pipelineStagesForApm', () => {
  const node = (name: string, label: string, latency: number, bytesOut = 100): PipelineNode => ({
    name,
    label,
    direction: 'inbound',
    metrics: {
      bytes_in: 200,
      bytes_out: bytesOut,
      bytes_per_sec: 1024,
      composite_score: 72,
      jitter_ms: 1.2,
      latency_ms_p50: latency,
      latency_ms_p95: latency * 2,
      packet_loss_pct: 0,
      dropped_bytes: 0,
      processing_ms: latency,
      snr_db: 18.5,
    },
  });

  it('builds span attributes from node metrics', () => {
    const vad = node('vad', 'Silero VAD', 3.5);
    const attrs = pipelineNodeToSpanAttributes(vad, {
      status: 'ok',
      scope: 'session',
      session_id: 'sess-1',
      ts_ms: 1000,
      nodes: [],
      links: [],
    });
    expect(attrs['pipeline.stage']).toBe('vad');
    expect(attrs['pipeline.activity']).toBe('live');
    expect(attrs['latency_ms_p50']).toBe(3.5);
    expect(attrs['composite_score']).toBe(72);
    expect(attrs['pipeline.session_id']).toBe('sess-1');
  });

  it('lists active stages from snapshot', () => {
    const snap: PipelineSnapshot = {
      status: 'ok',
      scope: 'fleet',
      ts_ms: 1,
      nodes: [node('vad', 'Silero VAD', 2), node('asr', 'ASR', 0, 0)],
      links: [],
    };
    const stages = pipelineStagesForApm(snap);
    expect(stages.map((s) => s.name)).toEqual(['vad']);
  });
});

describe('activeLinkCount', () => {
  it('counts links with value', () => {
    const snap: PipelineSnapshot = {
      status: 'ok',
      scope: 'fleet',
      ts_ms: 1,
      nodes: [],
      links: [
        { source: 'a', target: 'b', value: 0 },
        { source: 'b', target: 'c', value: 10 },
      ],
    };
    expect(activeLinkCount(snap)).toBe(1);
  });
});
