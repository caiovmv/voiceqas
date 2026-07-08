import type { PipelineEchartsOption, PipelineLink, PipelineNode, PipelineSnapshot } from '../types';
import {
  buildSankeyOptionFromSnapshot,
  resolveSankeyOption,
  type SankeyEchartsOption,
} from './pipeline-sankey';

export interface TransportSankeyView {
  direction: 'inbound' | 'outbound' | string;
  nodes: PipelineNode[];
  links: PipelineLink[];
  echarts?: PipelineEchartsOption;
}

export interface TransportSankeyPair {
  inbound?: TransportSankeyView;
  outbound?: TransportSankeyView;
}

function transportSnapshotFromView(view: TransportSankeyView | undefined): PipelineSnapshot | null {
  if (!view?.nodes?.length) return null;
  return {
    status: 'ok',
    scope: 'fleet',
    ts_ms: Date.now(),
    nodes: view.nodes,
    links: view.links ?? [],
    echarts: view.echarts,
  };
}

export function resolveTransportSankeyOption(
  view: TransportSankeyView | undefined,
): SankeyEchartsOption | null {
  const snap = transportSnapshotFromView(view);
  if (!snap) return null;
  const opt = resolveSankeyOption(snap);
  if (!opt) return buildSankeyOptionFromSnapshot(snap);
  return {
    ...opt,
    tooltip: {
      trigger: 'item',
      formatter: (params: {
        dataType?: string;
        name?: string;
        value?: number;
        data?: { source?: string; target?: string; value?: number };
      }) => {
        if (params.dataType === 'edge' && params.data) {
          return params.data.source + ' -> ' + params.data.target + '<br/>' + params.data.value + ' B/s';
        }
        return (params.name ?? '') + '<br/>' + (params.value ?? '') + ' B/s';
      },
    },
  };
}

export function transportLinkCount(pair: TransportSankeyPair | undefined): number {
  return (pair?.inbound?.links?.length ?? 0) + (pair?.outbound?.links?.length ?? 0);
}

export function transportSankeyFromSnapshot(snapshot: PipelineSnapshot | null): TransportSankeyPair | undefined {
  return snapshot?.transport_sankey;
}
