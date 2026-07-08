import { useMemo } from 'react';
import type { PromTimeSeries } from '../../lib/domain/prom-series';

export interface LineSeries {
  label: string;
  color: string;
  points: { t: number; v: number }[];
}

interface LineChartProps {
  title: string;
  series: LineSeries[];
  height?: number;
  formatValue?: (v: number) => string;
  unit?: string;
  emptyLabel?: string;
}

function chartBounds(series: LineSeries[]) {
  const points = series.flatMap((s) => s.points);
  if (points.length === 0) {
    return { minT: 0, maxT: 1, minV: 0, maxV: 1 };
  }
  const minT = Math.min(...points.map((p) => p.t));
  const maxT = Math.max(...points.map((p) => p.t));
  const minV = Math.min(...points.map((p) => p.v));
  const maxV = Math.max(...points.map((p) => p.v));
  return {
    minT,
    maxT: maxT === minT ? minT + 1 : maxT,
    minV: Math.min(0, minV),
    maxV: maxV <= minV ? minV + 1 : maxV,
  };
}

function toPath(
  points: { t: number; v: number }[],
  width: number,
  height: number,
  minT: number,
  maxT: number,
  minV: number,
  maxV: number,
): string {
  if (points.length === 0) return '';
  const pad = 4;
  const innerW = width - pad * 2;
  const innerH = height - pad * 2;
  return points
    .map((p, i) => {
      const x = pad + ((p.t - minT) / (maxT - minT)) * innerW;
      const y = pad + innerH - ((p.v - minV) / (maxV - minV)) * innerH;
      return `${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(' ');
}

export function LineChart({
  title,
  series,
  height = 160,
  formatValue = (v) => v.toFixed(2),
  unit,
  emptyLabel = 'Sem dados',
}: LineChartProps) {
  const width = 640;
  const active = series.filter((s) => s.points.length > 0);
  const bounds = useMemo(() => chartBounds(active), [active]);
  const last = active.map((s) => ({
    label: s.label,
    color: s.color,
    value: s.points[s.points.length - 1]?.v,
  }));

  return (
    <div className="cc-chart-card">
      <div className="cc-chart-head">
        <h4>{title}</h4>
        <div className="cc-chart-legend">
          {last.map((item) => (
            <span key={item.label} className="cc-chart-legend-item" style={{ color: item.color }}>
              {item.label}: {item.value !== undefined ? formatValue(item.value) : '—'}
              {unit ? ` ${unit}` : ''}
            </span>
          ))}
        </div>
      </div>
      {active.length === 0 ? (
        <p className="muted cc-chart-empty">{emptyLabel}</p>
      ) : (
        <svg className="cc-line-chart" viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none">
          {[0.25, 0.5, 0.75].map((pct) => (
            <line
              key={pct}
              x1={4}
              x2={width - 4}
              y1={4 + (height - 8) * pct}
              y2={4 + (height - 8) * pct}
              className="cc-chart-grid"
            />
          ))}
          {active.map((s) => (
            <path
              key={s.label}
              d={toPath(s.points, width, height, bounds.minT, bounds.maxT, bounds.minV, bounds.maxV)}
              fill="none"
              stroke={s.color}
              strokeWidth={2}
              vectorEffect="non-scaling-stroke"
            />
          ))}
        </svg>
      )}
    </div>
  );
}

export function seriesFromProm(
  rows: PromTimeSeries[],
  label: string,
  color: string,
  scale = 1,
): LineSeries {
  const row = rows[0];
  return {
    label,
    color,
    points: (row?.points ?? []).map((p) => ({ t: p.t, v: p.v * scale })),
  };
}

export function multiSeriesFromProm(
  rows: PromTimeSeries[],
  colors: string[],
  scale = 1,
): LineSeries[] {
  return rows.map((row, i) => ({
    label: row.label,
    color: colors[i % colors.length],
    points: row.points.map((p) => ({ t: p.t, v: p.v * scale })),
  }));
}
