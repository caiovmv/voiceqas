import { describe, expect, it } from 'vitest';
import {
  detectProtocolFromName,
  detectProtocolFromSpan,
  findTraceRootProtocol,
  resolveSpanProtocol,
} from './protocol-model';

describe('protocol-model', () => {
  it('detects OTel SDK trace names', () => {
    expect(detectProtocolFromName('WS /v1/stt/stream')).toBe('ws');
    expect(detectProtocolFromName('WS /v1/ops/stream')).toBe('ws');
    expect(detectProtocolFromName('gRPC SpeechToTextService/StreamRecognize')).toBe('grpc');
    expect(detectProtocolFromName('gRPC MediaRelayService/OpenSession')).toBe('grpc');
    expect(detectProtocolFromName('POST /v1/stt/vad/model')).toBe('rest');
    expect(detectProtocolFromName('GET /v1/media/sessions')).toBe('rest');
  });

  it('detects Beyla span attributes', () => {
    expect(detectProtocolFromSpan({ name: 'GET /v1/stt/stream', serverPort: 8081 })).toBe('ws');
    expect(detectProtocolFromSpan({ name: 'rpc', serverPort: 50051, rpcMethod: 'OpenSession' })).toBe('grpc');
    expect(detectProtocolFromSpan({ name: 'GET /v1/media/sessions', serverPort: 8080 })).toBe('rest');
  });

  it('inherits root protocol for pipeline spans', () => {
    const spans = [
      { name: 'WS /v1/stt/stream', parentSpanId: '', serverPort: 8081 },
      { name: 'pipeline/decode_stt', parentSpanId: 'abc', serverPort: undefined },
    ];
    const root = findTraceRootProtocol(spans);
    expect(root).toBe('ws');
    expect(resolveSpanProtocol(spans[1], root)).toBe('ws');
  });
});
