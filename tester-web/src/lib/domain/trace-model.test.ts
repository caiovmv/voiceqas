import { describe, expect, it } from 'vitest';
import {
  flattenSpanTree,
  guessProtocolFromTraceName,
  isPipelineSpan,
  parseTempoSearch,
  parseTempoTrace,
  protocolFromSpan,
} from './trace-model';

describe('parseTempoSearch', () => {
  it('maps trace list', () => {
    const rows = parseTempoSearch({
      traces: [
        {
          traceID: 'abc',
          rootServiceName: 'voiceqas',
          rootTraceName: 'GET /health',
          startTimeUnixNano: '1000000000',
          durationMs: 2,
        },
      ],
    });
    expect(rows).toHaveLength(1);
    expect(rows[0].traceId).toBe('abc');
    expect(rows[0].rootName).toBe('GET /health');
  });
});

describe('parseTempoTrace', () => {
  it('extracts span attributes', () => {
    const spans = parseTempoTrace({
      batches: [
        {
          resource: {
            attributes: [{ key: 'service.name', value: { stringValue: 'voiceqas' } }],
          },
          scopeSpans: [
            {
              spans: [
                {
                  spanId: 's1',
                  name: 'GET /health',
                  startTimeUnixNano: '1000000000',
                  endTimeUnixNano: '1001000000',
                  attributes: [
                    { key: 'http.route', value: { stringValue: '/health' } },
                    { key: 'http.request.method', value: { stringValue: 'GET' } },
                    { key: 'http.response.status_code', value: { intValue: '200' } },
                  ],
                },
              ],
            },
          ],
        },
      ],
    });
    expect(spans).toHaveLength(1);
    expect(spans[0].httpRoute).toBe('/health');
    expect(spans[0].durationMs).toBe(1);
    expect(spans[0].protocol).toBe('rest');
  });
});

describe('isPipelineSpan', () => {
  it('detects SDK pipeline spans', () => {
    expect(isPipelineSpan({ name: 'pipeline/vad', attributes: {} })).toBe(true);
    expect(isPipelineSpan({ name: 'GET /x', attributes: { 'voiceqas.stage': 'vad' } })).toBe(true);
    expect(isPipelineSpan({ name: 'GET /x', attributes: {} })).toBe(false);
  });
});

describe('span tree', () => {
  it('nests children by parentSpanId', () => {
    const flat = flattenSpanTree([
      { spanId: 'a', parentSpanId: '', name: 'root', service: 'x', startMs: 0, durationMs: 10, statusCode: 0 },
      { spanId: 'b', parentSpanId: 'a', name: 'child', service: 'x', startMs: 1, durationMs: 2, statusCode: 0 },
    ]);
    expect(flat.map((s) => s.depth)).toEqual([0, 1]);
  });

});

describe('protocol', () => {
  it('detects ws and grpc', () => {
    expect(protocolFromSpan({ serverPort: 8081, httpStatus: 101, name: 'x' })).toBe('ws');
    expect(protocolFromSpan({ serverPort: 50051, rpcMethod: 'ListSessions', name: 'x' })).toBe('grpc');
    expect(guessProtocolFromTraceName('GET /v1/stt/stream')).toBe('ws');
  });
});
