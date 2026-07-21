import { describe, expect, it } from 'vitest';
import { lastValue, parseMatrix, parseVector, topSeriesByAvg } from './prom-series';

describe('prom-series', () => {
  it('parses matrix results', () => {
    const rows = parseMatrix(
      {
        data: {
          result: [
            {
              metric: { http_route: '/health' },
              values: [
                [1000, '1.5'],
                [1030, '2'],
              ],
            },
          ],
        },
      },
      'http_route',
    );
    expect(rows).toHaveLength(1);
    expect(rows[0].label).toBe('/health');
    expect(rows[0].points[1].v).toBe(2);
    expect(rows[0].points[1].t).toBe(1030_000);
  });

  it('parses vector and ranks series', () => {
    const rows = parseVector(
      {
        data: {
          result: [
            { metric: { http_route: '/a' }, value: [1, '0.1'] },
            { metric: { http_route: '/b' }, value: [1, '0.9'] },
          ],
        },
      },
      'http_route',
    );
    const top = topSeriesByAvg(rows, 1);
    expect(top[0].label).toBe('/b');
    expect(lastValue(top[0])).toBe(0.9);
  });
});
