/** Rotas de probe/infra — excluir de RPS, traces e dashboards de negócio. */
export const NOISE_HTTP_ROUTES = ['/health', '/ready', '/metrics'] as const;

/** Filtro PromQL para http_route (regex alternation). */
export const PROM_CORE_HTTP_ROUTES = 'http_route!~"/health|/ready|/metrics"';

/** Span metrics Tempo — excluir probes. */
export const PROM_CORE_SPAN_NAMES = 'span_name!~".*/(health|ready|metrics)|GET /health|GET /metrics|GET /ready"';

/** Filtros por protocolo (Tempo spanmetrics / trace names). */
export const PROM_SPAN_WS = 'span_name=~"WS .*"';
export const PROM_SPAN_GRPC = 'span_name=~"gRPC .*"';
export const PROM_SPAN_REST_OTEL = 'span_name=~"^(GET|POST|PUT|DELETE|PATCH) /v1/.*"';

export function isCoreHttpRoute(route: string): boolean {
  const path = route.split('?')[0];
  return !(NOISE_HTTP_ROUTES as readonly string[]).includes(path);
}

export function isCoreTraceName(name: string): boolean {
  const lower = name.toLowerCase();
  if (lower === 'get /health' || lower === 'get /ready' || lower === 'get /metrics') return false;
  const pathMatch = name.match(/\s(\/[^\s]+)/);
  const path = pathMatch?.[1] ?? name;
  return isCoreHttpRoute(path);
}

export function isCoreSpanMetricName(spanName: string): boolean {
  const lower = spanName.toLowerCase();
  if (lower.includes('/health') || lower.includes('/ready') || lower.includes('/metrics')) return false;
  return true;
}

export function formatEndpointLabel(
  protocol: 'rest' | 'ws' | 'grpc',
  endpoint: string,
): string {
  const tag = protocol === 'grpc' ? 'gRPC' : protocol === 'ws' ? 'WS' : 'REST';
  return `${tag} ${endpoint}`;
}

/** TraceQL filter fragment (sem chaves externas). */
export const TEMPO_CORE_NAME_FILTER =
  'name !~ ".*/(health|ready|metrics)" && name !~ "GET /(health|ready|metrics)"';
