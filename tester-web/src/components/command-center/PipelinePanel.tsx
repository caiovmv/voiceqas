import { useEffect, useMemo, useState } from 'react';
import { fetchPipelineSessions } from '../../lib/api';
import { resolveSankeyOption } from '../../lib/domain/pipeline-sankey';
import {
  resolveTransportSankeyOption,
  transportLinkCount,
  transportSankeyFromSnapshot,
} from '../../lib/domain/transport-sankey';
import {
  INBOUND_STAGES,
  OUTBOUND_STAGES,
  activeLinkCount,
  formatBytes,
  maxBytesOut,
  nodeByName,
  scoreClass,
  stageActivity,
  stageLiveMetrics,
} from '../../lib/domain/pipeline-stages';
import type { PipelineNode, PipelineSnapshot } from '../../lib/types';
import { PipelineSankeyChart } from './PipelineSankeyChart';

interface PipelinePanelProps {
  snapshot: PipelineSnapshot | null;
  loading: boolean;
  error: string | null;
  sessionFilter: string;
  onSessionFilterChange: (id: string) => void;
}

function StageCard({
  node,
  scale,
}: {
  node: PipelineNode;
  scale: number;
}) {
  const m = node.metrics;
  const activity = stageActivity(node);
  const score = scoreClass(m.composite_score);
  const barPct = scale > 0 ? Math.min(100, (m.bytes_out / scale) * 100) : 0;
  const live = stageLiveMetrics(node);

  return (
    <div
      className={`cc-pipe-stage ${activity} ${node.direction}`}
      title={`${node.label}\nbytes out: ${formatBytes(m.bytes_out)}\n${live.jitter} jitter · ${live.latency} lat · ${live.totalTime} · ${live.kbps}`}
    >
      <span className="cc-pipe-stage-label">{node.label}</span>
      <div className="cc-pipe-bar-track">
        <div
          className={`cc-pipe-bar ${score}`}
          style={{ width: `${barPct}%` }}
        />
      </div>
      <div className="cc-pipe-metrics">
        <div className="cc-pipe-metric">
          <span className="cc-pipe-metric-k">jit</span>
          <span className={`cc-pipe-metric-v ${activity === 'live' ? 'live' : ''}`}>{live.jitter}</span>
        </div>
        <div className="cc-pipe-metric">
          <span className="cc-pipe-metric-k">lat</span>
          <span className={`cc-pipe-metric-v ${activity === 'live' ? 'live' : ''}`}>{live.latency}</span>
        </div>
        <div className="cc-pipe-metric">
          <span className="cc-pipe-metric-k">time</span>
          <span className={`cc-pipe-metric-v ${activity === 'live' ? 'live' : ''}`}>{live.totalTime}</span>
        </div>
        <div className="cc-pipe-metric">
          <span className="cc-pipe-metric-k">thr</span>
          <span className={`cc-pipe-metric-v ${activity === 'live' ? 'live' : ''}`}>{live.kbps}</span>
        </div>
      </div>
      {(m.composite_score > 0 || m.dropped_bytes > 0) && (
        <div className="cc-pipe-chips">
          {m.composite_score > 0 && (
            <span className={`cc-pipe-chip score ${score}`}>
              VQA {Math.round(m.composite_score)}
            </span>
          )}
          {m.dropped_bytes > 0 && (
            <span className="cc-pipe-chip drop">drop {formatBytes(m.dropped_bytes)}</span>
          )}
        </div>
      )}
    </div>
  );
}

function StageRow({
  title,
  stageIds,
  snapshot,
}: {
  title: string;
  stageIds: readonly string[];
  snapshot: PipelineSnapshot | null;
}) {
  const nodes = useMemo(() => {
    const list: PipelineNode[] = [];
    for (const id of stageIds) {
      const n = nodeByName(snapshot, id);
      if (n) list.push(n);
    }
    return list;
  }, [snapshot, stageIds]);

  const scale = maxBytesOut(nodes);
  const anyActive = nodes.some((n) => stageActivity(n) !== 'idle');

  return (
    <div className={`cc-pipe-row ${anyActive ? 'active' : ''}`}>
      <div className="cc-pipe-row-title">{title}</div>
      <div className="cc-pipe-stages">
        {nodes.map((node, i) => (
          <div key={node.name} className="cc-pipe-stage-wrap">
            {i > 0 && <span className="cc-pipe-arrow" aria-hidden>›</span>}
            <StageCard node={node} scale={scale} />
          </div>
        ))}
      </div>
    </div>
  );
}

export function PipelinePanel({
  snapshot,
  loading,
  error,
  sessionFilter,
  onSessionFilterChange,
}: PipelinePanelProps) {
  const [sessionIds, setSessionIds] = useState<string[]>([]);

  useEffect(() => {
    fetchPipelineSessions()
      .then((res) => {
        const ids = res.session_ids ?? res.active_sessions?.map((s) => s.session_id) ?? [];
        setSessionIds(ids);
      })
      .catch(() => setSessionIds([]));
  }, [snapshot?.ts_ms]);

  const links = activeLinkCount(snapshot);
  const transportPair = transportSankeyFromSnapshot(snapshot);
  const transportLinks = transportLinkCount(transportPair);
  const vqa = nodeByName(snapshot, 'vqa');
  const rtp = nodeByName(snapshot, 'rtp_ingress');
  const sip = nodeByName(snapshot, 'sip_in');
  const sankeyOption = useMemo(() => resolveSankeyOption(snapshot), [snapshot]);
  const inboundTransportOption = useMemo(
    () => resolveTransportSankeyOption(transportPair?.inbound),
    [transportPair?.inbound],
  );
  const outboundTransportOption = useMemo(
    () => resolveTransportSankeyOption(transportPair?.outbound),
    [transportPair?.outbound],
  );

  return (
    <section className="panel cc-pipeline">
      <div className="cc-panel-head">
        <div>
          <h2>Audio Pipeline</h2>
          <p className="muted cc-pipeline-sub">
            Sankeys de transporte (SIP/REST/WS/gRPC → Media Pipeline → Ollama) + pipeline DSP detalhado.
          </p>
        </div>
        <div className="cc-pipeline-meta">
          <span className={`badge ${transportLinks > 0 ? 'ok' : links > 0 ? 'ok' : ''}`}>
            {transportLinks > 0 ? `${transportLinks} transport links` : `${links} links ativos`}
          </span>
          <span className="badge">{snapshot?.scope ?? '—'}</span>
          {snapshot?.codec && <span className="badge">{snapshot.codec}</span>}
          {loading && <span className="muted">atualizando…</span>}
        </div>
      </div>

      <div className="cc-pipeline-toolbar">
        <label>
          Visão
          <select
            value={sessionFilter}
            onChange={(e) => onSessionFilterChange(e.target.value)}
          >
            <option value="">Fleet (todas)</option>
            {sessionIds.map((id) => (
              <option key={id} value={id}>
                {id}
              </option>
            ))}
          </select>
        </label>
        <div className="cc-pipeline-summary">
          <span>SIP {formatBytes(sip?.metrics.bytes_out ?? 0)}</span>
          <span>RTP {formatBytes(rtp?.metrics.bytes_out ?? 0)}</span>
          <span>VQA {vqa?.metrics.composite_score ? Math.round(vqa.metrics.composite_score) : '—'}</span>
        </div>
      </div>

      {error && <p className="error">{error}</p>}

      {!error && !loading && links === 0 && transportLinks === 0 && (
        <p className="cc-empty muted">
          Sem tráfego no pipeline. Use o <a href="#tester">Tester</a> ou rode{' '}
          <code>scripts/sankey-traffic-test.ps1</code>.
        </p>
      )}

      <div className="cc-transport-sankey-grid">
        <div className="cc-sankey-wrap">
          <h3 className="cc-sankey-title">Inbound — Transport → Media Pipeline → AI Externa</h3>
          <PipelineSankeyChart option={inboundTransportOption} height={320} />
        </div>
        <div className="cc-sankey-wrap">
          <h3 className="cc-sankey-title">Outbound — AI Externa → Media Pipeline → Transport</h3>
          <PipelineSankeyChart option={outboundTransportOption} height={320} />
        </div>
      </div>

      <div className="cc-sankey-wrap">
        <h3 className="cc-sankey-title">Pipeline DSP (detalhe interno)</h3>
        <PipelineSankeyChart option={sankeyOption} height={440} />
      </div>

      <StageRow title="Inbound" stageIds={INBOUND_STAGES} snapshot={snapshot} />
      <StageRow title="Outbound" stageIds={OUTBOUND_STAGES} snapshot={snapshot} />
    </section>
  );
}
