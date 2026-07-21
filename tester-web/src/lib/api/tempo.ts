import { parseTempoSearch, parseTempoTrace, type SpanRow, type TraceSummary } from '../domain/trace-model';
import { isCoreTraceName, TEMPO_CORE_NAME_FILTER } from '../domain/observability-filters';

const tempoBase = '/tempo';

function tempoUrl(path: string, params?: Record<string, string | number>) {
  const q = params
    ? `?${new URLSearchParams(Object.entries(params).map(([k, v]) => [k, String(v)])).toString()}`
    : '';
  return `${tempoBase}${path}${q}`;
}

/** TraceQL default — sem isso o /api/search fica inundado por GET de polling e esconde WS/STT. */
export const TEMPO_DEFAULT_TRACEQL =
  `{ resource.service.name = "voiceqas" && ${TEMPO_CORE_NAME_FILTER} }`;

export async function searchTraces(options?: {
  hours?: number;
  limit?: number;
  excludeMetrics?: boolean;
  query?: string;
}): Promise<TraceSummary[]> {
  const hours = options?.hours ?? 2;
  const limit = options?.limit ?? 50;
  const end = Math.floor(Date.now() / 1000);
  const start = end - hours * 3600;
  const q = options?.query ?? TEMPO_DEFAULT_TRACEQL;
  const res = await fetch(
    tempoUrl('/api/search', { start, end, limit, q }),
  );
  if (!res.ok) {
    throw new Error(`Tempo search ${res.status}`);
  }
  const body = await res.json();
  let traces = parseTempoSearch(body);
  if (options?.excludeMetrics !== false) {
    traces = traces.filter((t) => isCoreTraceName(t.rootName));
  }
  return traces;
}

export async function fetchTraceSpans(traceId: string): Promise<SpanRow[]> {
  const res = await fetch(tempoUrl(`/api/traces/${traceId}`));
  if (!res.ok) {
    throw new Error(`Tempo trace ${res.status}`);
  }
  const body = await res.json();
  return parseTempoTrace(body);
}
