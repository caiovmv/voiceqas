import { describe, expect, it } from 'vitest';
import { resolveTransportSankeyOption, transportLinkCount } from './transport-sankey';
import type { TransportSankeyPair } from '../types';

const metrics = {
  bytes_in: 0,
  bytes_out: 900,
  bytes_per_sec: 900,
  composite_score: 0,
  jitter_ms: 0,
  latency_ms_p50: 0,
  latency_ms_p95: 0,
  packet_loss_pct: 0,
  dropped_bytes: 0,
};

const pair: TransportSankeyPair = {
  inbound: {
    direction: 'inbound',
    nodes: [
      { name: 'sip_trunk', label: 'SIP Trunk', direction: 'inbound', metrics: { ...metrics, bytes_out: 900 } },
      { name: 'media_pipeline', label: 'Media Pipeline', direction: 'inbound', metrics: { ...metrics, bytes_out: 800 } },
      { name: 'external_ai', label: 'AI Externa (Ollama)', direction: 'inbound', metrics: { ...metrics, bytes_out: 400 } },
    ],
    links: [
      { source: 'sip_trunk', target: 'media_pipeline', value: 900 },
      { source: 'media_pipeline', target: 'external_ai', value: 400 },
    ],
  },
};

describe('transport-sankey', () => {
  it('builds inbound transport sankey', () => {
    const opt = resolveTransportSankeyOption(pair.inbound);
    expect(opt?.series?.[0]?.type).toBe('sankey');
    expect(opt?.series?.[0]?.links).toHaveLength(2);
  });

  it('counts transport links', () => {
    expect(transportLinkCount(pair)).toBe(2);
    expect(transportLinkCount(undefined)).toBe(0);
  });
});
