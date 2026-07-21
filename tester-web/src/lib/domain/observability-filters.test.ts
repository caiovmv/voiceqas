import { describe, expect, it } from 'vitest';
import {
  formatEndpointLabel,
  isCoreHttpRoute,
  isCoreSpanMetricName,
  isCoreTraceName,
} from './observability-filters';

describe('observability-filters', () => {
  it('excludes probe routes', () => {
    expect(isCoreHttpRoute('/health')).toBe(false);
    expect(isCoreHttpRoute('/ready')).toBe(false);
    expect(isCoreHttpRoute('/metrics')).toBe(false);
    expect(isCoreHttpRoute('/v1/stt/stream')).toBe(true);
  });

  it('filters trace names', () => {
    expect(isCoreTraceName('GET /health')).toBe(false);
    expect(isCoreTraceName('GET /v1/stt/ready')).toBe(true);
  });

  it('labels endpoints by protocol', () => {
    expect(formatEndpointLabel('ws', '/v1/stt/stream')).toBe('WS /v1/stt/stream');
    expect(formatEndpointLabel('grpc', 'OpenSession')).toBe('gRPC OpenSession');
  });

  it('filters span metric names', () => {
    expect(isCoreSpanMetricName('WS /v1/stt/stream')).toBe(true);
    expect(isCoreSpanMetricName('GET /health')).toBe(false);
    expect(isCoreSpanMetricName('gRPC SpeechToTextService/Stream')).toBe(true);
  });
});
