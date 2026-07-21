export interface PromTimeSeries {
  label: string;
  points: { t: number; v: number }[];
}

type MatrixResult = {
  metric: Record<string, string>;
  values: [number, string][];
};

type VectorResult = {
  metric: Record<string, string>;
  value: [number, string];
};

export function parseMatrix(
  body: { data?: { result?: MatrixResult[] } },
  labelKey?: string,
): PromTimeSeries[] {
  return (body.data?.result ?? []).map((row) => ({
    label: labelKey ? (row.metric[labelKey] ?? row.metric.__name__ ?? '?') : 'value',
    points: row.values.map(([t, v]) => ({ t: t * 1000, v: Number(v) })),
  }));
}

export function parseVector(
  body: { data?: { result?: VectorResult[] } },
  labelKey?: string,
): PromTimeSeries[] {
  const now = Date.now();
  return (body.data?.result ?? []).map((row) => ({
    label: labelKey ? (row.metric[labelKey] ?? '?') : 'value',
    points: [{ t: now, v: Number(row.value[1]) }],
  }));
}

export function lastValue(series: PromTimeSeries | undefined): number | null {
  if (!series || series.points.length === 0) return null;
  return series.points[series.points.length - 1].v;
}

export function topSeriesByAvg(series: PromTimeSeries[], limit: number): PromTimeSeries[] {
  return [...series]
    .map((s) => ({
      s,
      avg: s.points.reduce((n, p) => n + p.v, 0) / Math.max(s.points.length, 1),
    }))
    .sort((a, b) => b.avg - a.avg)
    .slice(0, limit)
    .map((x) => x.s);
}
