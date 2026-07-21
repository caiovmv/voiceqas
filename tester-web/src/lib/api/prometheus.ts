import { parseMatrix, type PromTimeSeries } from '../domain/prom-series';
import {
  PROM_CORE_HTTP_ROUTES,
  PROM_CORE_SPAN_NAMES,
  PROM_SPAN_GRPC,
  PROM_SPAN_WS,
} from '../domain/observability-filters';

const promBase = '/prometheus';

const SERVICE = 'voiceqas';
const HTTP = `service_name="${SERVICE}"`;
const CORE = PROM_CORE_HTTP_ROUTES;
const SPAN = `service_name="${SERVICE}",${PROM_CORE_SPAN_NAMES}`;
const NO_WS_LATENCY = 'http_response_status_code!~"101"';
/**
 * Beyla gRPC (:50051): metrics are rpc_{server|client}_duration_seconds
 * (not rpc.server.call.* — those come from Alloy OTLP noise).
 * Same-process playground often appears only as rpc_client_*; include both.
 * Exclude wildcard method="*" and TraceService exporters.
 */
const RPC_SEL = `job="beyla",service_name="${SERVICE}",rpc_method!="",rpc_method!="*",rpc_method!~".*TraceService.*|.*ServerReflection.*"`;
const RPC_COUNT = `{__name__=~"rpc_server_duration_seconds_count|rpc_client_duration_seconds_count",${RPC_SEL}}`;
const RPC_BUCKET = `{__name__=~"rpc_server_duration_seconds_bucket|rpc_client_duration_seconds_bucket",${RPC_SEL}}`;
const RPC_ERR = `{__name__=~"rpc_server_duration_seconds_count|rpc_client_duration_seconds_count",${RPC_SEL},rpc_grpc_status_code!~"0|OK"}`;

export const promQueries = {
  restRps: `sum(rate(http_server_request_duration_seconds_count{${HTTP},server_port="8080",${CORE}}[1m]))`,
  restP50: `histogram_quantile(0.50, sum(rate(http_server_request_duration_seconds_bucket{${HTTP},server_port="8080",${CORE},${NO_WS_LATENCY}}[5m])) by (le))`,
  restP95: `histogram_quantile(0.95, sum(rate(http_server_request_duration_seconds_bucket{${HTTP},server_port="8080",${CORE},${NO_WS_LATENCY}}[5m])) by (le))`,
  restP98: `histogram_quantile(0.98, sum(rate(http_server_request_duration_seconds_bucket{${HTTP},server_port="8080",${CORE},${NO_WS_LATENCY}}[5m])) by (le))`,
  restErr4xx: `sum(rate(http_server_request_duration_seconds_count{${HTTP},server_port="8080",${CORE},http_response_status_code=~"4.."}[1m]))`,
  restErr5xx: `sum(rate(http_server_request_duration_seconds_count{${HTTP},server_port="8080",${CORE},http_response_status_code=~"5.."}[1m]))`,
  restByRoute: `sum(rate(http_server_request_duration_seconds_count{${HTTP},server_port="8080",${CORE}}[1m])) by (http_route)`,

  wsBeylaRps: `sum(rate(http_server_request_duration_seconds_count{${HTTP},server_port="8081",${CORE}}[1m]))`,
  wsUpgrades: `sum(rate(http_server_request_duration_seconds_count{${HTTP},server_port="8081",http_response_status_code="101",${CORE}}[1m]))`,
  wsUpgradeP95: `histogram_quantile(0.95, sum(rate(http_server_request_duration_seconds_bucket{${HTTP},server_port="8081",http_response_status_code="101",${CORE}}[5m])) by (le))`,
  wsBeylaP50: `histogram_quantile(0.50, sum(rate(http_server_request_duration_seconds_bucket{${HTTP},server_port="8081",${CORE}}[5m])) by (le))`,
  wsBeylaP95: `histogram_quantile(0.95, sum(rate(http_server_request_duration_seconds_bucket{${HTTP},server_port="8081",${CORE}}[5m])) by (le))`,
  wsBeylaP98: `histogram_quantile(0.98, sum(rate(http_server_request_duration_seconds_bucket{${HTTP},server_port="8081",${CORE}}[5m])) by (le))`,
  wsErr4xx: `sum(rate(http_server_request_duration_seconds_count{${HTTP},server_port="8081",${CORE},http_response_status_code=~"4.."}[1m]))`,
  wsErr5xx: `sum(rate(http_server_request_duration_seconds_count{${HTTP},server_port="8081",${CORE},http_response_status_code=~"5.."}[1m]))`,
  wsByRoute: `sum(rate(http_server_request_duration_seconds_count{${HTTP},server_port="8081",${CORE}}[1m])) by (http_route)`,

  wsOtelRps: `sum(rate(traces_spanmetrics_calls_total{${SPAN},${PROM_SPAN_WS}}[1m]))`,
  wsOtelP50: `histogram_quantile(0.50, sum(rate(traces_spanmetrics_latency_bucket{${SPAN},${PROM_SPAN_WS}}[5m])) by (le))`,
  wsOtelP95: `histogram_quantile(0.95, sum(rate(traces_spanmetrics_latency_bucket{${SPAN},${PROM_SPAN_WS}}[5m])) by (le))`,
  wsOtelP98: `histogram_quantile(0.98, sum(rate(traces_spanmetrics_latency_bucket{${SPAN},${PROM_SPAN_WS}}[5m])) by (le))`,
  wsOtelErr: `sum(rate(traces_spanmetrics_calls_total{${SPAN},${PROM_SPAN_WS},status_code="STATUS_CODE_ERROR"}[1m]))`,
  wsOtelBySpan: `sum(rate(traces_spanmetrics_calls_total{${SPAN},${PROM_SPAN_WS}}[1m])) by (span_name)`,

  grpcBeylaRps: `sum(rate(${RPC_COUNT}[1m]))`,
  grpcBeylaP50: `histogram_quantile(0.50, sum(rate(${RPC_BUCKET}[5m])) by (le))`,
  grpcBeylaP95: `histogram_quantile(0.95, sum(rate(${RPC_BUCKET}[5m])) by (le))`,
  grpcBeylaP98: `histogram_quantile(0.98, sum(rate(${RPC_BUCKET}[5m])) by (le))`,
  grpcBeylaErr: `sum(rate(${RPC_ERR}[1m]))`,
  grpcByMethod: `sum(rate(${RPC_COUNT}[1m])) by (rpc_method)`,

  grpcOtelRps: `sum(rate(traces_spanmetrics_calls_total{${SPAN},${PROM_SPAN_GRPC}}[1m]))`,
  grpcOtelP50: `histogram_quantile(0.50, sum(rate(traces_spanmetrics_latency_bucket{${SPAN},${PROM_SPAN_GRPC}}[5m])) by (le))`,
  grpcOtelP95: `histogram_quantile(0.95, sum(rate(traces_spanmetrics_latency_bucket{${SPAN},${PROM_SPAN_GRPC}}[5m])) by (le))`,
  grpcOtelP98: `histogram_quantile(0.98, sum(rate(traces_spanmetrics_latency_bucket{${SPAN},${PROM_SPAN_GRPC}}[5m])) by (le))`,
  grpcOtelErr: `sum(rate(traces_spanmetrics_calls_total{${SPAN},${PROM_SPAN_GRPC},status_code="STATUS_CODE_ERROR"}[1m]))`,
  grpcOtelBySpan: `sum(rate(traces_spanmetrics_calls_total{${SPAN},${PROM_SPAN_GRPC}}[1m])) by (span_name)`,

  totalRps: `sum(rate(http_server_request_duration_seconds_count{${HTTP},${CORE}}[1m])) + sum(rate(${RPC_COUNT}[1m])) + sum(rate(traces_spanmetrics_calls_total{${SPAN},${PROM_SPAN_WS}}[1m])) + sum(rate(traces_spanmetrics_calls_total{${SPAN},${PROM_SPAN_GRPC}}[1m]))`,
};

type PromMatrixBody = { data?: { result?: Parameters<typeof parseMatrix>[0]['data'] extends { result?: infer R } ? R : never } };

async function promFetch(path: string, params: Record<string, string>): Promise<PromMatrixBody & { status?: string; error?: string }> {
  const q = new URLSearchParams(params).toString();
  const res = await fetch(`${promBase}${path}?${q}`);
  if (!res.ok) throw new Error(`Prometheus ${res.status}`);
  const body = (await res.json()) as { status?: string; error?: string };
  if (body.status !== 'success') throw new Error(body.error ?? 'Prometheus query failed');
  return body;
}

export async function queryRange(
  expr: string,
  windowMinutes: number,
  stepSec: number,
  labelKey?: string,
): Promise<PromTimeSeries[]> {
  const end = Math.floor(Date.now() / 1000);
  const start = end - windowMinutes * 60;
  const body = await promFetch('/api/v1/query_range', {
    query: expr,
    start: String(start),
    end: String(end),
    step: String(stepSec),
  });
  return parseMatrix(body as PromMatrixBody, labelKey);
}

export interface ProtocolRedMetrics {
  rps: PromTimeSeries[];
  latency: { p50: PromTimeSeries[]; p95: PromTimeSeries[]; p98: PromTimeSeries[] };
  errors: { err4xx: PromTimeSeries[]; err5xx: PromTimeSeries[] };
  byEndpoint: PromTimeSeries[];
}

export interface OtelRedMetrics {
  rps: PromTimeSeries[];
  latency: { p50: PromTimeSeries[]; p95: PromTimeSeries[]; p98: PromTimeSeries[] };
  errors: PromTimeSeries[];
  bySpan: PromTimeSeries[];
}

export interface WsRedMetrics extends ProtocolRedMetrics {
  upgrades: PromTimeSeries[];
  upgradeP95: PromTimeSeries[];
  otel: OtelRedMetrics;
}

export interface GrpcRedMetrics extends ProtocolRedMetrics {
  otel: OtelRedMetrics;
}

export interface EndpointSeries extends PromTimeSeries {
  protocol: 'rest' | 'ws' | 'grpc';
  source?: 'beyla' | 'otel';
}

export interface RedMetricsSnapshot {
  totalRps: PromTimeSeries[];
  rest: ProtocolRedMetrics;
  ws: WsRedMetrics;
  grpc: GrpcRedMetrics;
  combinedByEndpoint: EndpointSeries[];
}

function tagEndpoints(
  series: PromTimeSeries[],
  protocol: 'rest' | 'ws' | 'grpc',
  source: 'beyla' | 'otel' = 'beyla',
): EndpointSeries[] {
  return series.map((s) => ({ ...s, protocol, source }));
}

async function loadProtocol(
  windowMinutes: number,
  stepSec: number,
  cfg: {
    rps: string;
    p50: string;
    p95: string;
    p98: string;
    err4xx: string;
    err5xx: string;
    byEndpoint: string;
    endpointLabel: string;
  },
): Promise<ProtocolRedMetrics> {
  const [rps, p50, p95, p98, err4xx, err5xx, byEndpoint] = await Promise.all([
    queryRange(cfg.rps, windowMinutes, stepSec),
    queryRange(cfg.p50, windowMinutes, stepSec),
    queryRange(cfg.p95, windowMinutes, stepSec),
    queryRange(cfg.p98, windowMinutes, stepSec),
    queryRange(cfg.err4xx, windowMinutes, stepSec),
    queryRange(cfg.err5xx, windowMinutes, stepSec),
    queryRange(cfg.byEndpoint, windowMinutes, stepSec, cfg.endpointLabel),
  ]);
  return { rps, latency: { p50, p95, p98 }, errors: { err4xx, err5xx }, byEndpoint };
}

async function loadOtel(
  windowMinutes: number,
  stepSec: number,
  cfg: { rps: string; p50: string; p95: string; p98: string; err: string; bySpan: string },
): Promise<OtelRedMetrics> {
  const [rps, p50, p95, p98, errors, bySpan] = await Promise.all([
    queryRange(cfg.rps, windowMinutes, stepSec),
    queryRange(cfg.p50, windowMinutes, stepSec),
    queryRange(cfg.p95, windowMinutes, stepSec),
    queryRange(cfg.p98, windowMinutes, stepSec),
    queryRange(cfg.err, windowMinutes, stepSec),
    queryRange(cfg.bySpan, windowMinutes, stepSec, 'span_name'),
  ]);
  return { rps, latency: { p50, p95, p98 }, errors, bySpan };
}

export async function fetchRedMetrics(options?: {
  windowMinutes?: number;
  stepSec?: number;
}): Promise<RedMetricsSnapshot> {
  const windowMinutes = options?.windowMinutes ?? 60;
  const stepSec = options?.stepSec ?? 30;

  const [totalRps, rest, wsBeyla, wsUpgrades, wsUpgradeP95, wsOtel, grpcBeyla, grpcOtel] = await Promise.all([
    queryRange(promQueries.totalRps, windowMinutes, stepSec),
    loadProtocol(windowMinutes, stepSec, {
      rps: promQueries.restRps,
      p50: promQueries.restP50,
      p95: promQueries.restP95,
      p98: promQueries.restP98,
      err4xx: promQueries.restErr4xx,
      err5xx: promQueries.restErr5xx,
      byEndpoint: promQueries.restByRoute,
      endpointLabel: 'http_route',
    }),
    loadProtocol(windowMinutes, stepSec, {
      rps: promQueries.wsBeylaRps,
      p50: promQueries.wsBeylaP50,
      p95: promQueries.wsBeylaP95,
      p98: promQueries.wsBeylaP98,
      err4xx: promQueries.wsErr4xx,
      err5xx: promQueries.wsErr5xx,
      byEndpoint: promQueries.wsByRoute,
      endpointLabel: 'http_route',
    }),
    queryRange(promQueries.wsUpgrades, windowMinutes, stepSec),
    queryRange(promQueries.wsUpgradeP95, windowMinutes, stepSec),
    loadOtel(windowMinutes, stepSec, {
      rps: promQueries.wsOtelRps,
      p50: promQueries.wsOtelP50,
      p95: promQueries.wsOtelP95,
      p98: promQueries.wsOtelP98,
      err: promQueries.wsOtelErr,
      bySpan: promQueries.wsOtelBySpan,
    }),
    loadProtocol(windowMinutes, stepSec, {
      rps: promQueries.grpcBeylaRps,
      p50: promQueries.grpcBeylaP50,
      p95: promQueries.grpcBeylaP95,
      p98: promQueries.grpcBeylaP98,
      err4xx: promQueries.grpcBeylaErr,
      err5xx: promQueries.grpcBeylaErr,
      byEndpoint: promQueries.grpcByMethod,
      endpointLabel: 'rpc_method',
    }),
    loadOtel(windowMinutes, stepSec, {
      rps: promQueries.grpcOtelRps,
      p50: promQueries.grpcOtelP50,
      p95: promQueries.grpcOtelP95,
      p98: promQueries.grpcOtelP98,
      err: promQueries.grpcOtelErr,
      bySpan: promQueries.grpcOtelBySpan,
    }),
  ]);

  const ws: WsRedMetrics = {
    ...wsBeyla,
    upgrades: wsUpgrades,
    upgradeP95: wsUpgradeP95,
    otel: wsOtel,
  };

  const grpc: GrpcRedMetrics = {
    ...grpcBeyla,
    otel: grpcOtel,
  };

  return {
    totalRps,
    rest,
    ws,
    grpc,
    combinedByEndpoint: [
      ...tagEndpoints(rest.byEndpoint, 'rest', 'beyla'),
      ...tagEndpoints(ws.byEndpoint, 'ws', 'beyla'),
      ...tagEndpoints(ws.otel.bySpan, 'ws', 'otel'),
      ...tagEndpoints(grpc.byEndpoint, 'grpc', 'beyla'),
      ...tagEndpoints(grpc.otel.bySpan, 'grpc', 'otel'),
    ],
  };
}
