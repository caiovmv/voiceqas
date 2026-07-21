import { describe, expect, it } from 'vitest';
import { buildSankeyOptionFromSnapshot, resolveSankeyOption } from './pipeline-sankey';
import type { PipelineNode, PipelineSnapshot } from '../types';

function node(name: string, label: string, bytesOut: number, direction: 'inbound' | 'outbound' = 'inbound'): PipelineNode {
  return {
    name,
    label,
    direction,
    metrics: {
      bytes_in: bytesOut,
      bytes_out: bytesOut,
      bytes_per_sec: 0,
      composite_score: name === 'vqa' ? 80 : 0,
      jitter_ms: 0,
      latency_ms_p50: 1,
      latency_ms_p95: 2,
      packet_loss_pct: 0,
      dropped_bytes: 0,
    },
  };
}

const snap: PipelineSnapshot = {
  status: 'ok',
  scope: 'fleet',
  ts_ms: 1,
  nodes: [
    node('sip_in', 'SIP in', 1000),
    node('rtp_ingress', 'RTP', 900),
    node('vqa', 'VQA', 800),
  ],
  links: [
    { source: 'sip_in', target: 'rtp_ingress', value: 900 },
    { source: 'rtp_ingress', target: 'vqa', value: 800 },
  ],
};

describe('pipeline-sankey', () => {
  it('builds echarts sankey from nodes/links', () => {
    const opt = buildSankeyOptionFromSnapshot(snap);
    expect(opt?.series?.[0]?.type).toBe('sankey');
    expect(opt?.series?.[0]?.data?.map((d) => d.name)).toEqual(['sip_in', 'rtp_ingress', 'vqa']);
    expect(opt?.series?.[0]?.links).toHaveLength(2);
    expect(opt?.series?.[0]?.data?.find((d) => d.name === 'vqa')?.itemStyle?.color).toBe('#3ecf8e');
  });

  it('prefers server echarts payload when links exist', () => {
    const withServer: PipelineSnapshot = {
      ...snap,
      echarts: {
        series: [
          {
            type: 'sankey',
            data: [{ name: 'sip_in', label: 'SIP' }],
            links: [{ source: 'sip_in', target: 'rtp_ingress', value: 42 }],
          },
        ],
      },
    };
    const opt = resolveSankeyOption(withServer);
    expect(opt?.series?.[0]?.links?.[0]?.value).toBe(42);
  });

  it('returns null when no flow', () => {
    expect(buildSankeyOptionFromSnapshot({ ...snap, links: [] })).toBeNull();
    expect(resolveSankeyOption(null)).toBeNull();
  });
});
