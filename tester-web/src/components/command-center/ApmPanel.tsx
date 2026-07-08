import { useCallback, useEffect, useMemo, useState } from 'react';
import { fetchTraceSpans, searchTraces } from '../../lib/api/tempo';
import {
  flattenSpanTree,
  isPipelineSpan,
  spanDerivedAttributes,
  traceRootStartMs,
  type SpanRow,
  type SpanWithDepth,
  type TraceSummary,
} from '../../lib/domain/trace-model';
import { detectProtocolFromName, protocolLabel, type Protocol } from '../../lib/domain/protocol-model';
import { formatBps, formatBytes } from '../../lib/domain/pipeline-stages';

type ProtocolTab = 'all' | Protocol;

function formatTime(ms: number): string {
  return new Date(ms).toLocaleTimeString();
}

function formatDuration(ms: number): string {
  if (ms < 1) return '<1 ms';
  if (ms < 1000) return `${ms.toFixed(ms < 10 ? 1 : 0)} ms`;
  return `${(ms / 1000).toFixed(2)} s`;
}

function traceProtocol(t: TraceSummary): Protocol {
  return t.protocol ?? detectProtocolFromName(t.rootName);
}

function SpanWaterfall({
  spans,
  selectedSpanId,
  onSelect,
}: {
  spans: SpanWithDepth[];
  selectedSpanId: string | null;
  onSelect: (spanId: string) => void;
}) {
  const rootStart = traceRootStartMs(spans);
  const maxEnd = useMemo(() => {
    if (spans.length === 0) return 1;
    return Math.max(...spans.map((s) => s.startMs + s.durationMs - rootStart), 0.1);
  }, [spans, rootStart]);

  if (spans.length === 0) {
    return <p className="muted">Nenhum span neste trace.</p>;
  }

  return (
    <div className="cc-apm-waterfall">
      {spans.map((span) => {
        const offset = ((span.startMs - rootStart) / maxEnd) * 100;
        const width = Math.max((span.durationMs / maxEnd) * 100, 0.4);
        const label = isPipelineSpan(span)
          ? (span.attributes?.['voiceqas.stage_label'] as string | undefined) ?? span.name.replace(/^pipeline\//, '')
          : span.httpRoute
            ? `${span.httpMethod ?? 'HTTP'} ${span.httpRoute}`
            : span.rpcMethod
              ? `RPC ${span.rpcMethod}`
              : span.name;
        return (
          <div
            key={span.spanId}
            className={`cc-apm-span-row ${selectedSpanId === span.spanId ? 'selected' : ''} ${isPipelineSpan(span) ? 'internal' : ''}`}
            style={{ paddingLeft: `${8 + span.depth * 16}px` }}
            onClick={() => onSelect(span.spanId)}
            onKeyDown={(e) => e.key === 'Enter' && onSelect(span.spanId)}
            role="button"
            tabIndex={0}
            title={span.spanId}
          >
            <div className="cc-apm-span-meta">
              <span className="cc-apm-span-name">
                {isPipelineSpan(span) && <span className="cc-apm-internal-tag">pipeline</span>}
                {label}
              </span>
              <span className="muted">
                {span.service}
                {span.protocol && ` · ${protocolLabel(span.protocol)}`}
                {span.httpStatus !== undefined ? ` · ${span.httpStatus}` : ''}
                {span.clientAddress ? ` · ${span.clientAddress}` : ''}
              </span>
            </div>
            <div className="cc-apm-span-track">
              <div
                className={`cc-apm-span-bar ${span.statusCode === 2 ? 'err' : isPipelineSpan(span) ? 'internal' : 'ok'}`}
                style={{ marginLeft: `${offset}%`, width: `${width}%` }}
              />
            </div>
            <span className="cc-apm-span-dur">{formatDuration(span.durationMs)}</span>
          </div>
        );
      })}
    </div>
  );
}

const ATTR_GROUPS: Array<{ title: string; keys: string[] }> = [
  {
    title: 'Pipeline (OTel SDK)',
    keys: [
      'voiceqas.stage',
      'voiceqas.stage_label',
      'voiceqas.session_id',
      'voiceqas.bytes_in',
      'voiceqas.bytes_out',
      'voiceqas.duration_ms',
      'voiceqas.composite_score',
      'voiceqas.stt_ready',
      'voiceqas.model',
      'voiceqas.error',
    ],
  },
  {
    title: 'HTTP (Beyla)',
    keys: ['http.request.method', 'http.route', 'http.response.status_code', 'url.path', 'server.port', 'client.address'],
  },
  { title: 'gRPC (Beyla)', keys: ['rpc.method', 'rpc.grpc.status_code', 'rpc.service', 'voiceqas.protocol'] },
  { title: 'Throughput', keys: ['bytes_in', 'bytes_out', 'bytes_per_sec', 'packets_in', 'packets_out', 'dropped_bytes'] },
  { title: 'Latência', keys: ['latency_ms_p50', 'latency_ms_p95', 'processing_ms', 'buffer_ms', 'jitter_ms'] },
  { title: 'Qualidade', keys: ['composite_score', 'packet_loss_pct', 'snr_db', 'rms_dbfs', 'stt_ready'] },
];

function formatAttrValue(key: string, value: string | number): string {
  if (key === 'bytes_in' || key === 'bytes_out' || key === 'dropped_bytes') return formatBytes(Number(value));
  if (key === 'bytes_per_sec') return formatBps(Number(value)) || String(value);
  if (key.endsWith('_ms') || key === 'processing_ms' || key === 'buffer_ms') return `${value} ms`;
  if (key === 'composite_score') return `VQA ${value}`;
  if (key === 'stt_ready') return value === 'true' ? 'sim' : 'não';
  return String(value);
}

function attrLabel(key: string): string {
  return key.replace(/^pipeline\./, '').replace(/\./g, ' · ');
}

function SpanAttributes({ span }: { span: SpanRow | null }) {
  if (!span) return <p className="muted">Clique em um span para ver atributos.</p>;

  const attrs = spanDerivedAttributes(span);
  const grouped = new Set<string>();
  const sections = ATTR_GROUPS.map((group) => {
    const rows = group.keys
      .filter((k) => attrs[k] !== undefined)
      .map((k) => {
        grouped.add(k);
        return { key: k, value: formatAttrValue(k, attrs[k]) };
      });
    return { title: group.title, rows };
  }).filter((s) => s.rows.length > 0);

  const other = Object.entries(attrs)
    .filter(([k]) => !grouped.has(k))
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([key, value]) => ({ key, value: formatAttrValue(key, value) }));

  const hasAny = sections.length > 0 || other.length > 0;

  return (
    <div className="cc-apm-attrs">
      <h4>{isPipelineSpan(span) ? `Estágio interno — ${span.attributes?.['voiceqas.stage_label'] ?? span.name}` : 'Atributos do span'}</h4>
      {isPipelineSpan(span) && (
        <p className="muted cc-apm-hint">
          Span OTel exportado pelo SDK voiceqas (OpenTelemetry C++ → Alloy → Tempo).
        </p>
      )}
      {!isPipelineSpan(span) && !span.attributes && (
        <p className="muted cc-apm-hint">Beyla: 1 span por request. Campos derivados do trace quando disponíveis.</p>
      )}
      {!hasAny ? (
        <p className="muted">
          Sem métricas para este estágio — selecione uma sessão no Pipeline ou gere tráfego (STT/VQA/media).
        </p>
      ) : (
        <>
          {sections.map((section) => (
            <div key={section.title} className="cc-apm-attr-group">
              <h5>{section.title}</h5>
              <table className="cc-apm-table">
                <tbody>
                  {section.rows.map((row) => (
                    <tr key={row.key}>
                      <td><code>{attrLabel(row.key)}</code></td>
                      <td>{row.value}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          ))}
          {other.length > 0 && (
            <div className="cc-apm-attr-group">
              <h5>Outros</h5>
              <table className="cc-apm-table">
                <tbody>
                  {other.map((row) => (
                    <tr key={row.key}>
                      <td><code>{attrLabel(row.key)}</code></td>
                      <td>{row.value}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </>
      )}
    </div>
  );
}

export function ApmPanel() {
  const [traces, setTraces] = useState<TraceSummary[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [traceSpans, setTraceSpans] = useState<SpanRow[]>([]);
  const [selectedSpanId, setSelectedSpanId] = useState<string | null>(null);
  const [protocolTab, setProtocolTab] = useState<ProtocolTab>('all');
  const [loading, setLoading] = useState(false);
  const [detailLoading, setDetailLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const list = await searchTraces({ hours: 2, limit: 50 });
      setTraces(list);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Falha ao consultar Tempo');
      setTraces([]);
    } finally {
      setLoading(false);
    }
  }, []);

  const loadTrace = useCallback(async (traceId: string) => {
    setSelectedId(traceId);
    setDetailLoading(true);
    setError(null);
    setSelectedSpanId(null);
    try {
      const rows = await fetchTraceSpans(traceId);
      setTraceSpans(rows);
      const root = rows.find((s) => !s.parentSpanId) ?? rows[0];
      if (root) setSelectedSpanId(root.spanId);
    } catch (e) {
      setTraceSpans([]);
      setError(e instanceof Error ? e.message : 'Falha ao carregar trace');
    } finally {
      setDetailLoading(false);
    }
  }, []);

  const spans = traceSpans;

  useEffect(() => {
    refresh();
    const id = setInterval(refresh, 8000);
    return () => clearInterval(id);
  }, [refresh]);

  const waterfallSpans = useMemo(() => flattenSpanTree(spans), [spans]);
  const selectedSpan = spans.find((s) => s.spanId === selectedSpanId) ?? null;
  const selected = traces.find((t) => t.traceId === selectedId);
  const hasPipeline = spans.some((s) => isPipelineSpan(s));

  const filteredTraces = useMemo(
    () => (protocolTab === 'all' ? traces : traces.filter((t) => traceProtocol(t) === protocolTab)),
    [traces, protocolTab],
  );

  const protocolCounts = useMemo(() => {
    const counts = { rest: 0, ws: 0, grpc: 0 };
    for (const t of traces) counts[traceProtocol(t)] += 1;
    return counts;
  }, [traces]);

  const tabButtons = useMemo(
    () => [
      { id: 'all' as const, label: `Todos (${traces.length})` },
      { id: 'rest' as const, label: `REST (${protocolCounts.rest})` },
      { id: 'ws' as const, label: `WebSocket (${protocolCounts.ws})` },
      { id: 'grpc' as const, label: `gRPC (${protocolCounts.grpc})` },
    ],
    [traces.length, protocolCounts],
  );

  return (
    <section className="panel cc-apm-panel">
      <div className="cc-panel-head">
        <div>
          <h2>APM / Traces</h2>
          <p className="muted cc-pipeline-sub">
            Tempo TraceQL (exclui /health|/metrics). Preferir traces <code>WS</code> / <code>POST …/transcribe</code> —
            GET de polling tem 1 span; filhos <span className="cc-apm-internal-tag">pipeline</span> vivem no STT.
          </p>
        </div>
        <div className="cc-pipeline-meta">
          <button type="button" className="btn" onClick={refresh} disabled={loading}>
            {loading ? 'Atualizando…' : 'Atualizar'}
          </button>
          <span className="badge ok">{traces.length} traces</span>
        </div>
      </div>

      {error && <p className="cc-apm-error">{error}</p>}

      <div className="cc-red-tabs">
        {tabButtons.map((t) => (
          <button key={t.id} type="button" className={`btn ${protocolTab === t.id ? 'primary' : ''}`} onClick={() => setProtocolTab(t.id)}>
            {t.label}
          </button>
        ))}
      </div>

      <div className="cc-apm-layout">
        <div className="cc-apm-list">
          <table className="cc-apm-table">
            <thead>
              <tr>
                <th>Hora</th>
                <th>Proto</th>
                <th>Operação</th>
                <th>Duração</th>
              </tr>
            </thead>
            <tbody>
              {filteredTraces.map((t) => (
                <tr
                  key={t.traceId}
                  className={t.traceId === selectedId ? 'selected' : ''}
                  onClick={() => loadTrace(t.traceId)}
                >
                  <td>{formatTime(t.startMs)}</td>
                  <td>
                    <span className={`cc-proto-badge ${traceProtocol(t)}`}>
                      {protocolLabel(traceProtocol(t))}
                    </span>
                  </td>
                  <td>
                    <span className="cc-apm-route">{t.rootName}</span>
                    <span className="muted cc-apm-tid">{t.traceId.slice(0, 12)}…</span>
                  </td>
                  <td>{formatDuration(t.durationMs)}</td>
                </tr>
              ))}
              {filteredTraces.length === 0 && !loading && (
                <tr>
                  <td colSpan={4} className="muted">
                    Sem traces {protocolTab !== 'all' ? `(${protocolTab.toUpperCase()})` : ''} — use o Tester ou scripts/apm-smoke-test.ps1
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </div>

        <div className="cc-apm-detail">
          <h3>
            {selected
              ? `${selected.rootName} · ${selected.traceId}`
              : 'Selecione um trace na lista'}
          </h3>
          {detailLoading && <p className="muted">Carregando spans…</p>}
          {!detailLoading && selectedId && (
            <>
              {hasPipeline && (
                <p className="muted cc-apm-hint">
                  Barras <span className="cc-apm-internal-tag">pipeline</span> = spans OTel reais (decode, VAD, ASR, encode…).
                </p>
              )}
              <SpanWaterfall spans={waterfallSpans} selectedSpanId={selectedSpanId} onSelect={setSelectedSpanId} />
              <SpanAttributes span={selectedSpan} />
            </>
          )}
        </div>
      </div>
    </section>
  );
}
