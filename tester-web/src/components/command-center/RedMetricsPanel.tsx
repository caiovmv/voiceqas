import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  fetchRedMetrics,
  type EndpointSeries,
  type GrpcRedMetrics,
  type OtelRedMetrics,
  type ProtocolRedMetrics,
  type RedMetricsSnapshot,
  type WsRedMetrics,
} from '../../lib/api/prometheus';
import { formatEndpointLabel } from '../../lib/domain/observability-filters';
import { lastValue, topSeriesByAvg } from '../../lib/domain/prom-series';
import { LineChart, multiSeriesFromProm, seriesFromProm } from './LineChart';

type ProtocolTab = 'all' | 'rest' | 'ws' | 'grpc';

const ENDPOINT_COLORS = [
  '#4da3ff', '#3ecf8e', '#f5a623', '#c77dff', '#ff7eb6', '#7ee8fa', '#ffd166', '#ef476f',
];

function formatRps(v: number): string {
  if (v < 0.01) return v.toExponential(1);
  if (v < 10) return v.toFixed(2);
  return v.toFixed(1);
}

function formatMs(v: number): string {
  if (v < 1) return v.toFixed(2);
  if (v < 100) return v.toFixed(1);
  return v.toFixed(0);
}

function StatCard({ label, value, unit, tone }: { label: string; value: string; unit?: string; tone?: 'ok' | 'warn' | 'bad' }) {
  return (
    <div className={`cc-red-stat ${tone ?? ''}`}>
      <span className="cc-red-stat-label">{label}</span>
      <span className="cc-red-stat-value">
        {value}
        {unit ? <small>{unit}</small> : null}
      </span>
    </div>
  );
}

function combinedEndpointRows(series: EndpointSeries[]) {
  const total = series.reduce((n, s) => n + (lastValue(s) ?? 0), 0);
  return [...series]
    .map((s) => ({
      protocol: s.protocol,
      source: s.source ?? 'beyla',
      route: s.label,
      label: formatEndpointLabel(s.protocol, s.label),
      rps: lastValue(s) ?? 0,
    }))
    .filter((r) => r.rps > 0.001)
    .sort((a, b) => b.rps - a.rps)
    .map((r) => ({ ...r, share: total > 0 ? (r.rps / total) * 100 : 0 }));
}

function combinedChartSeries(series: EndpointSeries[]) {
  return topSeriesByAvg(
    series.map((s) => ({ ...s, label: formatEndpointLabel(s.protocol, s.label) })),
    12,
  );
}

function endpointRows(series: { label: string; points: { t: number; v: number }[] }[]) {
  const total = series.reduce((n, s) => n + (lastValue(s) ?? 0), 0);
  return [...series]
    .map((s) => ({ route: s.label, rps: lastValue(s) ?? 0 }))
    .filter((r) => r.rps > 0.001)
    .sort((a, b) => b.rps - a.rps)
    .map((r) => ({ ...r, share: total > 0 ? (r.rps / total) * 100 : 0 }));
}

function OtelSection({
  otel,
  title,
}: {
  otel: OtelRedMetrics;
  title: string;
}) {
  const rows = endpointRows(otel.bySpan);
  const topSeries = topSeriesByAvg(otel.bySpan, 8);

  return (
    <div className="cc-red-otel">
      <h4>{title}</h4>
      <div className="cc-red-stats">
        <StatCard label="RPS (OTel)" value={lastValue(otel.rps[0]) !== null ? formatRps(lastValue(otel.rps[0])!) : '—'} unit="ops/s" tone="ok" />
        <StatCard label="P95 (OTel)" value={lastValue(otel.latency.p95[0]) !== null ? formatMs(lastValue(otel.latency.p95[0])! * 1000) : '—'} unit="ms" />
        <StatCard label="Erros (OTel)" value={lastValue(otel.errors[0]) !== null ? formatRps(lastValue(otel.errors[0])!) : '—'} unit="err/s" tone={(lastValue(otel.errors[0]) ?? 0) > 0 ? 'bad' : undefined} />
      </div>
      <div className="cc-red-charts">
        <LineChart
          title="Latência OTel (P50 / P95 / P98)"
          unit="ms"
          formatValue={formatMs}
          series={[
            seriesFromProm(otel.latency.p50, 'P50', '#4da3ff', 1000),
            seriesFromProm(otel.latency.p95, 'P95', '#f5a623', 1000),
            seriesFromProm(otel.latency.p98, 'P98', '#f5555d', 1000),
          ]}
          emptyLabel="Sem spans OTel — gere tráfego STT/WS ou gRPC"
        />
        <LineChart title="Ops rate (OTel spanmetrics)" unit="ops/s" formatValue={formatRps} series={[seriesFromProm(otel.rps, 'RPS', '#3ecf8e')]} />
        <LineChart
          title="RPS por span (top 8)"
          unit="ops/s"
          formatValue={formatRps}
          series={multiSeriesFromProm(topSeries, ENDPOINT_COLORS)}
          emptyLabel="Sem spans OTel"
        />
      </div>
      {rows.length > 0 && (
        <div className="cc-red-endpoints">
          <table className="cc-apm-table">
            <thead>
              <tr>
                <th>Span</th>
                <th>RPS</th>
                <th>Share</th>
              </tr>
            </thead>
            <tbody>
              {rows.map((row) => (
                <tr key={row.route}>
                  <td><span className="cc-apm-route">{row.route}</span></td>
                  <td>{formatRps(row.rps)}</td>
                  <td>{row.share.toFixed(1)}%</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}

function ProtocolSection({
  title,
  metrics,
  endpointLabel,
  loading,
  extraSeries,
  otel,
  otelTitle,
}: {
  title: string;
  metrics: ProtocolRedMetrics;
  endpointLabel: string;
  loading: boolean;
  extraSeries?: { label: string; series: { label: string; points: { t: number; v: number }[] }[]; color: string; unit: string }[];
  otel?: OtelRedMetrics;
  otelTitle?: string;
}) {
  const rows = endpointRows(metrics.byEndpoint);
  const topSeries = topSeriesByAvg(metrics.byEndpoint, 8);

  return (
    <div className="cc-red-protocol">
      <h3>{title}</h3>
      <p className="muted cc-pipeline-sub">Beyla eBPF — latência e erros HTTP/RPC de borda.</p>
      <div className="cc-red-stats">
        <StatCard label="RPS" value={lastValue(metrics.rps[0]) !== null ? formatRps(lastValue(metrics.rps[0])!) : '—'} unit="req/s" tone="ok" />
        <StatCard label="P95" value={lastValue(metrics.latency.p95[0]) !== null ? formatMs(lastValue(metrics.latency.p95[0])! * 1000) : '—'} unit="ms" />
        <StatCard label="4xx" value={lastValue(metrics.errors.err4xx[0]) !== null ? formatRps(lastValue(metrics.errors.err4xx[0])!) : '—'} unit="err/s" tone={(lastValue(metrics.errors.err4xx[0]) ?? 0) > 0 ? 'warn' : undefined} />
        <StatCard label="5xx" value={lastValue(metrics.errors.err5xx[0]) !== null ? formatRps(lastValue(metrics.errors.err5xx[0])!) : '—'} unit="err/s" tone={(lastValue(metrics.errors.err5xx[0]) ?? 0) > 0 ? 'bad' : undefined} />
      </div>

      <div className="cc-red-charts">
        <LineChart
          title="Latência (P50 / P95 / P98)"
          unit="ms"
          formatValue={formatMs}
          series={[
            seriesFromProm(metrics.latency.p50, 'P50', '#4da3ff', 1000),
            seriesFromProm(metrics.latency.p95, 'P95', '#f5a623', 1000),
            seriesFromProm(metrics.latency.p98, 'P98', '#f5555d', 1000),
          ]}
          emptyLabel="Sem dados de latência"
        />
        <LineChart title="Request rate" unit="req/s" formatValue={formatRps} series={[seriesFromProm(metrics.rps, 'RPS', '#3ecf8e')]} />
        {extraSeries?.map((x) => (
          <LineChart key={x.label} title={x.label} unit={x.unit} formatValue={x.unit === 'ms' ? formatMs : formatRps} series={[seriesFromProm(x.series, x.label, x.color, x.unit === 'ms' ? 1000 : 1)]} />
        ))}
        <LineChart
          title="Error rate"
          unit="err/s"
          formatValue={formatRps}
          series={[
            seriesFromProm(metrics.errors.err4xx, '4xx', '#f5a623'),
            seriesFromProm(metrics.errors.err5xx, '5xx', '#f5555d'),
          ]}
        />
        <LineChart
          title={`RPS por ${endpointLabel} (top 8)`}
          unit="req/s"
          formatValue={formatRps}
          series={multiSeriesFromProm(topSeries, ENDPOINT_COLORS)}
          emptyLabel={`Sem tráfego (${endpointLabel})`}
        />
      </div>

      <div className="cc-red-endpoints">
        <table className="cc-apm-table">
          <thead>
            <tr>
              <th>{endpointLabel}</th>
              <th>RPS</th>
              <th>Share</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row) => (
              <tr key={row.route}>
                <td><span className="cc-apm-route">{row.route}</span></td>
                <td>{formatRps(row.rps)}</td>
                <td>{row.share.toFixed(1)}%</td>
              </tr>
            ))}
            {rows.length === 0 && !loading && (
              <tr><td colSpan={3} className="muted">Sem tráfego</td></tr>
            )}
          </tbody>
        </table>
      </div>

      {otel && <OtelSection otel={otel} title={otelTitle ?? 'Sessões OTel (spanmetrics Tempo)'} />}
    </div>
  );
}

function WsSection({ ws, loading }: { ws: WsRedMetrics; loading: boolean }) {
  return (
    <ProtocolSection
      title="WebSocket (HTTP upgrade :8081)"
      metrics={ws}
      endpointLabel="rota WS"
      loading={loading}
      extraSeries={[
        { label: 'Upgrades 101', series: ws.upgrades, color: '#c77dff', unit: 'conn/s' },
        { label: 'P95 upgrade', series: ws.upgradeP95, color: '#7ee8fa', unit: 'ms' },
      ]}
      otel={ws.otel}
      otelTitle="Sessões WS OTel (WS /v1/… spans)"
    />
  );
}

function GrpcSection({ grpc, loading }: { grpc: GrpcRedMetrics; loading: boolean }) {
  return (
    <ProtocolSection
      title="gRPC (:50051)"
      metrics={grpc}
      endpointLabel="método RPC"
      loading={loading}
      otel={grpc.otel}
      otelTitle="Chamadas gRPC OTel (gRPC … spans)"
    />
  );
}

export function RedMetricsPanel() {
  const [data, setData] = useState<RedMetricsSnapshot | null>(null);
  const [tab, setTab] = useState<ProtocolTab>('all');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      setData(await fetchRedMetrics({ windowMinutes: 60, stepSec: 30 }));
    } catch (e) {
      setData(null);
      setError(e instanceof Error ? e.message : 'Falha ao consultar Prometheus');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    refresh();
    const id = setInterval(refresh, 15_000);
    return () => clearInterval(id);
  }, [refresh]);

  const totalRps = lastValue(data?.totalRps[0]);

  const tabButtons = useMemo(
    () => [
      { id: 'all' as const, label: 'Todos (REST+WS+gRPC)' },
      { id: 'rest' as const, label: 'REST :8080' },
      { id: 'ws' as const, label: 'WebSocket :8081' },
      { id: 'grpc' as const, label: 'gRPC :50051' },
    ],
    [],
  );

  const combinedRows = useMemo(
    () => (data ? combinedEndpointRows(data.combinedByEndpoint) : []),
    [data],
  );

  return (
    <section className="panel cc-red-panel">
      <div className="cc-panel-head">
        <div>
          <h2>RED / Latência</h2>
          <p className="muted cc-pipeline-sub">
            Beyla eBPF (borda) + Tempo spanmetrics (OTel SDK). REST :8080, WS :8081, gRPC :50051
            (playground HTTP chama o servidor gRPC real).
          </p>
        </div>
        <div className="cc-pipeline-meta">
          <span className="badge ok">total {totalRps !== null ? formatRps(totalRps) : '—'} req/s</span>
          <button type="button" className="btn" onClick={refresh} disabled={loading}>
            {loading ? 'Atualizando…' : 'Atualizar'}
          </button>
        </div>
      </div>

      {error && <p className="cc-apm-error">{error}</p>}

      <div className="cc-red-tabs">
        {tabButtons.map((t) => (
          <button key={t.id} type="button" className={`btn ${tab === t.id ? 'primary' : ''}`} onClick={() => setTab(t.id)}>
            {t.label}
          </button>
        ))}
      </div>

      {data && tab === 'all' && (
        <div className="cc-red-protocol">
          <h3>Todos os protocolos</h3>
          <div className="cc-red-stats">
            <StatCard label="RPS total" value={totalRps !== null ? formatRps(totalRps) : '—'} unit="req/s" tone="ok" />
            <StatCard label="REST" value={lastValue(data.rest.rps[0]) !== null ? formatRps(lastValue(data.rest.rps[0])!) : '—'} unit="req/s" />
            <StatCard label="WS Beyla" value={lastValue(data.ws.rps[0]) !== null ? formatRps(lastValue(data.ws.rps[0])!) : '—'} unit="req/s" />
            <StatCard label="WS OTel" value={lastValue(data.ws.otel.rps[0]) !== null ? formatRps(lastValue(data.ws.otel.rps[0])!) : '—'} unit="ops/s" />
            <StatCard label="gRPC Beyla" value={lastValue(data.grpc.rps[0]) !== null ? formatRps(lastValue(data.grpc.rps[0])!) : '—'} unit="req/s" />
            <StatCard label="gRPC OTel" value={lastValue(data.grpc.otel.rps[0]) !== null ? formatRps(lastValue(data.grpc.otel.rps[0])!) : '—'} unit="ops/s" />
            <StatCard label="WS upgrades" value={lastValue(data.ws.upgrades[0]) !== null ? formatRps(lastValue(data.ws.upgrades[0])!) : '—'} unit="conn/s" />
          </div>
          <div className="cc-red-charts">
            <LineChart title="RPS total" unit="req/s" formatValue={formatRps} series={[seriesFromProm(data.totalRps, 'Total', '#3ecf8e')]} />
            <LineChart
              title="RPS por endpoint / span (top 12)"
              unit="req/s"
              formatValue={formatRps}
              series={multiSeriesFromProm(combinedChartSeries(data.combinedByEndpoint), ENDPOINT_COLORS)}
            />
            <LineChart
              title="Latência P95 por protocolo"
              unit="ms"
              formatValue={formatMs}
              series={[
                seriesFromProm(data.rest.latency.p95, 'REST Beyla', '#4da3ff', 1000),
                seriesFromProm(data.ws.latency.p95, 'WS Beyla', '#c77dff', 1000),
                seriesFromProm(data.ws.otel.latency.p95, 'WS OTel', '#7ee8fa', 1000),
                seriesFromProm(data.grpc.latency.p95, 'gRPC Beyla', '#f5a623', 1000),
                seriesFromProm(data.grpc.otel.latency.p95, 'gRPC OTel', '#ffd166', 1000),
              ]}
            />
          </div>
          <div className="cc-red-endpoints">
            <table className="cc-apm-table">
              <thead>
                <tr>
                  <th>Proto</th>
                  <th>Fonte</th>
                  <th>Endpoint / span</th>
                  <th>RPS</th>
                  <th>Share</th>
                </tr>
              </thead>
              <tbody>
                {combinedRows.map((row) => (
                  <tr key={`${row.protocol}-${row.source}-${row.route}`}>
                    <td><span className={`cc-proto-badge ${row.protocol}`}>{row.protocol.toUpperCase()}</span></td>
                    <td><span className="muted">{row.source}</span></td>
                    <td><span className="cc-apm-route">{row.route}</span></td>
                    <td>{formatRps(row.rps)}</td>
                    <td>{row.share.toFixed(1)}%</td>
                  </tr>
                ))}
                {combinedRows.length === 0 && !loading && (
                  <tr><td colSpan={5} className="muted">Sem tráfego de negócio</td></tr>
                )}
              </tbody>
            </table>
          </div>
        </div>
      )}
      {data && tab === 'rest' && (
        <ProtocolSection title="REST (HTTP :8080)" metrics={data.rest} endpointLabel="endpoint" loading={loading} />
      )}
      {data && tab === 'ws' && <WsSection ws={data.ws} loading={loading} />}
      {data && tab === 'grpc' && <GrpcSection grpc={data.grpc} loading={loading} />}
    </section>
  );
}
