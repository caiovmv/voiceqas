export type Protocol = 'rest' | 'ws' | 'grpc';

const WS_ROUTE_RE = /\/v1\/(?:ops\/stream|stt\/stream|stream)\b/i;
const GRPC_NAME_RE = /grpc\s|voicequalityservice|speechtotextservice|mediarelayservice/i;

export function protocolLabel(p: Protocol): string {
  if (p === 'ws') return 'WS';
  if (p === 'grpc') return 'gRPC';
  return 'REST';
}

/** Detecta protocolo a partir do root trace / span name (Beyla ou OTel SDK). */
export function detectProtocolFromName(name: string): Protocol {
  const trimmed = name.trim();
  const lower = trimmed.toLowerCase();
  if (lower.startsWith('grpc ') || GRPC_NAME_RE.test(trimmed)) return 'grpc';
  if (lower.startsWith('ws ')) return 'ws';
  if (WS_ROUTE_RE.test(trimmed)) return 'ws';
  if (lower.startsWith('get ') && WS_ROUTE_RE.test(trimmed)) return 'ws';
  return 'rest';
}

export function detectProtocolFromSpan(span: {
  name: string;
  serverPort?: number;
  rpcMethod?: string;
  httpStatus?: number;
  httpRoute?: string;
}): Protocol {
  if (span.rpcMethod || span.serverPort === 50051) return 'grpc';
  if (span.serverPort === 8081 || span.httpStatus === 101) return 'ws';
  if (span.httpRoute && WS_ROUTE_RE.test(span.httpRoute)) return 'ws';
  if (span.name.startsWith('pipeline/')) return 'rest';
  return detectProtocolFromName(span.name);
}

/** Herda protocolo do root para spans pipeline internos. */
export function resolveSpanProtocol(
  span: { name: string; serverPort?: number; rpcMethod?: string; httpStatus?: number; httpRoute?: string },
  rootProtocol: Protocol,
): Protocol {
  if (span.name.startsWith('pipeline/')) return rootProtocol;
  return detectProtocolFromSpan(span);
}

export function findTraceRootProtocol(spans: Array<{ name: string; parentSpanId: string; serverPort?: number; rpcMethod?: string; httpStatus?: number; httpRoute?: string }>): Protocol {
  const root = spans.find((s) => !s.parentSpanId) ?? spans[0];
  if (!root) return 'rest';
  return detectProtocolFromSpan(root);
}
