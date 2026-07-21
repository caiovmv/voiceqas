import {
  detectProtocolFromName,
  findTraceRootProtocol,
  resolveSpanProtocol,
  type Protocol,
} from './protocol-model';

export interface TraceSummary {
  traceId: string;
  rootService: string;
  rootName: string;
  startMs: number;
  durationMs: number;
  protocol?: Protocol;
}

export interface SpanRow {
  spanId: string;
  parentSpanId: string;
  name: string;
  service: string;
  startMs: number;
  durationMs: number;
  statusCode: number;
  protocol?: Protocol;
  httpRoute?: string;
  httpMethod?: string;
  httpStatus?: number;
  clientAddress?: string;
  serverPort?: number;
  rpcMethod?: string;
  attributes?: Record<string, string | number>;
}

export type SpanWithDepth = SpanRow & { depth: number };

type OtelAttr = { key: string; value: { stringValue?: string; intValue?: string } };

function attrString(attrs: OtelAttr[] | undefined, key: string): string | undefined {
  const hit = attrs?.find((a) => a.key === key);
  if (!hit) return undefined;
  return hit.value.stringValue ?? hit.value.intValue;
}

function attrAll(attrs: OtelAttr[] | undefined): Record<string, string | number> {
  const out: Record<string, string | number> = {};
  for (const a of attrs ?? []) {
    const raw = a.value.stringValue ?? a.value.intValue;
    if (raw === undefined) continue;
    const n = Number(raw);
    out[a.key] = Number.isFinite(n) && a.value.intValue !== undefined ? n : raw;
  }
  return out;
}

export function protocolFromSpan(
  span: Pick<SpanRow, 'serverPort' | 'rpcMethod' | 'httpStatus' | 'name' | 'httpRoute'>,
): Protocol {
  if (span.rpcMethod || span.serverPort === 50051) return 'grpc';
  if (span.serverPort === 8081 || span.httpStatus === 101) return 'ws';
  return detectProtocolFromName(span.name);
}

export function guessProtocolFromTraceName(rootName: string): Protocol {
  return detectProtocolFromName(rootName);
}

function attrNumber(attrs: OtelAttr[] | undefined, key: string): number | undefined {
  const raw = attrString(attrs, key);
  if (raw === undefined) return undefined;
  const n = Number(raw);
  return Number.isFinite(n) ? n : undefined;
}

export function parseTempoSearch(body: {
  traces?: Array<{
    traceID: string;
    rootServiceName?: string;
    rootTraceName?: string;
    startTimeUnixNano?: string;
    durationMs?: number;
  }>;
}): TraceSummary[] {
  return (body.traces ?? []).map((t) => {
    const rootName = t.rootTraceName ?? '?';
    return {
      traceId: t.traceID,
      rootService: t.rootServiceName ?? '?',
      rootName,
      startMs: t.startTimeUnixNano ? Number(t.startTimeUnixNano) / 1_000_000 : 0,
      durationMs: t.durationMs ?? 0,
      protocol: detectProtocolFromName(rootName),
    };
  });
}

export function parseTempoTrace(body: {
  batches?: Array<{
    resource?: { attributes?: OtelAttr[] };
    scopeSpans?: Array<{
      spans?: Array<{
        spanId?: string;
        parentSpanId?: string;
        name?: string;
        startTimeUnixNano?: string;
        endTimeUnixNano?: string;
        status?: { code?: string };
        attributes?: OtelAttr[];
      }>;
    }>;
  }>;
}): SpanRow[] {
  const rows: SpanRow[] = [];
  for (const batch of body.batches ?? []) {
    const service =
      attrString(batch.resource?.attributes, 'service.name') ?? 'unknown';
    for (const scope of batch.scopeSpans ?? []) {
      for (const span of scope.spans ?? []) {
        const startNs = Number(span.startTimeUnixNano ?? 0);
        const endNs = Number(span.endTimeUnixNano ?? startNs);
        const attrs = span.attributes;
        const attributes = attrAll(attrs);
        const serverPort = attrNumber(attrs, 'server.port');
        const rpcMethod = attrString(attrs, 'rpc.method');
        const row: SpanRow = {
          spanId: span.spanId ?? '',
          parentSpanId: span.parentSpanId ?? '',
          name: span.name ?? '?',
          service,
          startMs: startNs / 1_000_000,
          durationMs: Math.max(0, (endNs - startNs) / 1_000_000),
          statusCode: span.status?.code === 'STATUS_CODE_ERROR' ? 2 : 0,
          httpRoute: attrString(attrs, 'http.route') ?? attrString(attrs, 'url.path'),
          httpMethod: attrString(attrs, 'http.request.method'),
          httpStatus: attrNumber(attrs, 'http.response.status_code'),
          clientAddress: attrString(attrs, 'client.address'),
          serverPort,
          rpcMethod,
          attributes,
        };
        rows.push(row);
      }
    }
  }
  const rootProtocol = findTraceRootProtocol(rows);
  for (const row of rows) {
    row.protocol = resolveSpanProtocol(row, rootProtocol);
  }
  return rows.sort((a, b) => a.startMs - b.startMs);
}

export function traceRootStartMs(spans: SpanRow[]): number {
  if (spans.length === 0) return 0;
  return Math.min(...spans.map((s) => s.startMs));
}

export function flattenSpanTree(spans: SpanRow[]): SpanWithDepth[] {
  if (spans.length === 0) return [];
  const byId = new Map(spans.map((s) => [s.spanId, s]));
  const children = new Map<string, SpanRow[]>();
  const roots: SpanRow[] = [];

  for (const span of spans) {
    if (!span.parentSpanId || !byId.has(span.parentSpanId)) {
      roots.push(span);
      continue;
    }
    const list = children.get(span.parentSpanId) ?? [];
    list.push(span);
    children.set(span.parentSpanId, list);
  }

  const out: SpanWithDepth[] = [];
  const walk = (span: SpanRow, depth: number) => {
    out.push({ ...span, depth });
    const kids = (children.get(span.spanId) ?? []).sort((a, b) => a.startMs - b.startMs);
    for (const kid of kids) walk(kid, depth + 1);
  };

  roots.sort((a, b) => a.startMs - b.startMs).forEach((r) => walk(r, 0));
  return out;
}

export function isPipelineSpan(span: Pick<SpanRow, 'name' | 'attributes'>): boolean {
  if (span.name.startsWith('pipeline/')) return true;
  return span.attributes?.['voiceqas.stage'] !== undefined;
}

export function spanDerivedAttributes(span: SpanRow): Record<string, string | number> {
  const out: Record<string, string | number> = { ...(span.attributes ?? {}) };
  if (span.httpMethod && out['http.request.method'] === undefined) out['http.request.method'] = span.httpMethod;
  if (span.httpRoute && out['http.route'] === undefined) out['http.route'] = span.httpRoute;
  if (span.httpStatus !== undefined && out['http.response.status_code'] === undefined) {
    out['http.response.status_code'] = span.httpStatus;
  }
  if (span.serverPort !== undefined && out['server.port'] === undefined) out['server.port'] = span.serverPort;
  if (span.clientAddress && out['client.address'] === undefined) out['client.address'] = span.clientAddress;
  if (span.rpcMethod && out['rpc.method'] === undefined) out['rpc.method'] = span.rpcMethod;
  if (span.protocol && out['voiceqas.protocol'] === undefined) out['voiceqas.protocol'] = span.protocol;
  return out;
}
