import type { PipelineLink, PipelineNode, PipelineSnapshot } from '../types';
import { scoreClass } from './pipeline-stages';

export interface SankeyNodeDatum {
  name: string;
  value?: number;
  itemStyle?: { color?: string };
  label?: string | { formatter?: string };
}

export interface SankeyLinkDatum {
  source: string;
  target: string;
  value: number;
}

export interface SankeyEchartsOption {
  tooltip?: Record<string, unknown>;
  series?: Array<{
    type?: string;
    data?: SankeyNodeDatum[];
    links?: SankeyLinkDatum[];
    [key: string]: unknown;
  }>;
  [key: string]: unknown;
}

function nodeColor(node: PipelineNode): string {
  const score = node.metrics.composite_score;
  const cls = scoreClass(score);
  if (cls === 'ok') return '#3ecf8e';
  if (cls === 'warn') return '#f5a623';
  if (cls === 'bad') return '#f5555d';
  return node.direction === 'outbound' ? '#4da3ff' : '#8b949e';
}

/** Monta option ECharts a partir de nodes/links quando o payload `echarts` não vier. */
export function buildSankeyOptionFromSnapshot(snapshot: PipelineSnapshot | null): SankeyEchartsOption | null {
  if (!snapshot?.nodes?.length) return null;

  const nodeByName = new Map(snapshot.nodes.map((n) => [n.name, n]));
  const data: SankeyNodeDatum[] = snapshot.nodes.map((n) => ({
    name: n.name,
    value: Math.max(1, n.metrics.bytes_out || n.metrics.bytes_in || 1),
    itemStyle: { color: nodeColor(n) },
    label: n.label || n.name,
  }));

  const links: SankeyLinkDatum[] = (snapshot.links ?? [])
    .filter((l) => l.value > 0 && nodeByName.has(l.source) && nodeByName.has(l.target))
    .map((l: PipelineLink) => ({
      source: l.source,
      target: l.target,
      value: Math.max(1, l.value),
    }));

  if (links.length === 0) return null;

  return {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'item',
      formatter: (params: { dataType?: string; name?: string; value?: number; data?: SankeyLinkDatum }) => {
        if (params.dataType === 'edge' && params.data) {
          return `${params.data.source} → ${params.data.target}<br/>${params.data.value} B`;
        }
        return `${params.name ?? ''}<br/>${params.value ?? ''} B`;
      },
    },
    series: [
      {
        type: 'sankey',
        emphasis: { focus: 'adjacency' },
        nodeAlign: 'justify',
        orient: 'horizontal',
        nodeGap: 10,
        nodeWidth: 14,
        layoutIterations: 32,
        lineStyle: { color: 'gradient', curveness: 0.5, opacity: 0.45 },
        label: {
          color: '#c9d1d9',
          fontSize: 11,
          formatter: (p: { data?: SankeyNodeDatum }) => {
            const raw = p.data?.label;
            if (typeof raw === 'string') return raw;
            return p.data?.name ?? '';
          },
        },
        data,
        links,
      },
    ],
  };
}

/** Prefer payload nativo do servidor (`echarts`), senão reconstrói de nodes/links. */
export function resolveSankeyOption(snapshot: PipelineSnapshot | null): SankeyEchartsOption | null {
  if (!snapshot) return null;
  const server = snapshot.echarts;
  if (server && Array.isArray(server.series) && (server.series[0]?.links?.length ?? 0) > 0) {
    return {
      ...server,
      backgroundColor: 'transparent',
      tooltip: server.tooltip ?? { trigger: 'item' },
      series: [
        {
          ...server.series[0],
          type: 'sankey',
          emphasis: { focus: 'adjacency' },
          lineStyle: { color: 'gradient', curveness: 0.5, opacity: 0.45 },
          label: {
            color: '#c9d1d9',
            fontSize: 11,
            formatter: (p: { data?: SankeyNodeDatum; name?: string }) => {
              const raw = p.data?.label;
              if (typeof raw === 'string') return raw;
              return p.name ?? p.data?.name ?? '';
            },
          },
        },
      ],
    };
  }
  return buildSankeyOptionFromSnapshot(snapshot);
}
