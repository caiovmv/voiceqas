import { useEffect, useMemo, useRef, useState } from 'react';
import * as echarts from 'echarts/core';
import { LineChart as ELineChart, BarChart } from 'echarts/charts';
import { GridComponent, LegendComponent, TooltipComponent } from 'echarts/components';
import { CanvasRenderer } from 'echarts/renderers';
import { fetchOpsHistory } from '../../lib/api';
import { computeAsrMetrics, snrTone, type AsrMetricsSnapshot } from '../../lib/domain/asr-metrics';
import type { OpsEvent, TrackedSession } from '../../lib/types';

echarts.use([ELineChart, BarChart, GridComponent, LegendComponent, TooltipComponent, CanvasRenderer]);

interface AsrMetricsPanelProps {
  sessions: TrackedSession[];
  filterSessionId?: string;
  showChannelColumn?: boolean;
}

function formatPct(v: number): string {
  return `${(v * 100).toFixed(0)}%`;
}

function formatMs(v: number | null): string {
  if (v === null) return '—';
  if (v < 10) return v.toFixed(1);
  return v.toFixed(0);
}

function formatDb(v: number | null): string {
  if (v === null) return '—';
  return v.toFixed(1);
}

function StatCard({
  label,
  value,
  unit,
  tone,
  hint,
}: {
  label: string;
  value: string;
  unit?: string;
  tone?: 'ok' | 'warn' | 'bad';
  hint?: string;
}) {
  return (
    <div className={`cc-red-stat ${tone ?? ''}`} title={hint}>
      <span className="cc-red-stat-label">{label}</span>
      <span className="cc-red-stat-value">
        {value}
        {unit ? <small>{unit}</small> : null}
      </span>
    </div>
  );
}

function AsrCharts({ metrics }: { metrics: AsrMetricsSnapshot }) {
  const hostRef = useRef<HTMLDivElement | null>(null);
  const chartRef = useRef<echarts.EChartsType | null>(null);

  useEffect(() => {
    const el = hostRef.current;
    if (!el) return;
    const chart = echarts.init(el, undefined, { renderer: 'canvas' });
    chartRef.current = chart;
    const onResize = () => chart.resize();
    window.addEventListener('resize', onResize);
    const ro = typeof ResizeObserver !== 'undefined' ? new ResizeObserver(onResize) : null;
    ro?.observe(el);
    return () => {
      window.removeEventListener('resize', onResize);
      ro?.disconnect();
      chart.dispose();
      chartRef.current = null;
    };
  }, []);

  useEffect(() => {
    const chart = chartRef.current;
    if (!chart) return;

    const latency = metrics.latencySeries;
    const snr = metrics.snrSeries;

    chart.setOption(
      {
        backgroundColor: 'transparent',
        tooltip: { trigger: 'axis' },
        legend: {
          data: ['Latência ASR (ms)', 'SNR (dB)'],
          textStyle: { color: '#8b949e', fontSize: 11 },
          top: 0,
        },
        grid: { left: 48, right: 48, top: 36, bottom: 28 },
        xAxis: {
          type: 'time',
          axisLabel: { color: '#8b949e', fontSize: 10 },
          splitLine: { show: false },
        },
        yAxis: [
          {
            type: 'value',
            name: 'ms',
            nameTextStyle: { color: '#8b949e' },
            axisLabel: { color: '#8b949e' },
            splitLine: { lineStyle: { color: 'rgba(255,255,255,0.06)' } },
          },
          {
            type: 'value',
            name: 'dB',
            nameTextStyle: { color: '#8b949e' },
            axisLabel: { color: '#8b949e' },
            splitLine: { show: false },
          },
        ],
        series: [
          {
            name: 'Latência ASR (ms)',
            type: 'line',
            showSymbol: latency.length < 24,
            smooth: true,
            data: latency.map((p) => [p.t, p.v]),
            itemStyle: { color: '#4da3ff' },
            areaStyle: { color: 'rgba(77,163,255,0.12)' },
          },
          {
            name: 'SNR (dB)',
            type: 'line',
            yAxisIndex: 1,
            showSymbol: snr.length < 24,
            smooth: true,
            data: snr.map((p) => [p.t, p.v]),
            itemStyle: { color: '#3ecf8e' },
          },
        ],
      },
      { notMerge: true },
    );
  }, [metrics]);

  return <div ref={hostRef} className="cc-asr-chart" role="img" aria-label="ASR latency and SNR" />;
}

export function AsrMetricsPanel({ sessions, filterSessionId, showChannelColumn }: AsrMetricsPanelProps) {
  const [history, setHistory] = useState<OpsEvent[]>([]);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    const load = () => {
      fetchOpsHistory({ sessionId: filterSessionId || undefined, limit: 200 })
        .then((r) => {
          if (!cancelled) {
            setHistory(r.events ?? []);
            setError(null);
          }
        })
        .catch((e) => {
          if (!cancelled) setError(e instanceof Error ? e.message : String(e));
        });
    };
    load();
    const id = setInterval(load, 12_000);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [filterSessionId]);

  const metrics = useMemo(
    () => computeAsrMetrics(sessions, history),
    [sessions, history],
  );

  return (
    <section className="panel cc-asr-panel">
      <div className="cc-panel-head">
        <div>
          <h2>ASR / STT</h2>
          <p className="muted cc-pipeline-sub">
            Automatic Speech Recognition — qualidade (SNR), latência, sessões ASR-ready, sucesso e cobertura.
            WER/CER exigem ground truth e não são calculados neste serviço.
          </p>
        </div>
        <div className="cc-pipeline-meta">
          <span className="badge ok">{metrics.activeSessions} sessões</span>
          <span className={`badge ${metrics.asrReadySessions > 0 ? 'ok' : ''}`}>
            {metrics.asrReadySessions} ASR-ready
          </span>
        </div>
      </div>

      {error && <p className="cc-apm-error muted">Histórico ops: {error}</p>}

      <div className="cc-red-stats">
        <StatCard
          label="ASR-ready sessions"
          value={String(metrics.asrReadySessions)}
          unit={`/ ${metrics.activeSessions}`}
          tone={metrics.asrReadySessions > 0 ? 'ok' : undefined}
          hint="Sessões com VQA gate stt_ready=true (áudio apto a transcrição)"
        />
        <StatCard
          label="ASR ready rate"
          value={formatPct(metrics.asrReadyRate)}
          tone={metrics.asrReadyRate >= 0.7 ? 'ok' : metrics.asrReadyRate >= 0.4 ? 'warn' : metrics.activeSessions ? 'bad' : undefined}
        />
        <StatCard
          label="Success rate"
          value={metrics.finalsTotal > 0 ? formatPct(metrics.successRate) : '—'}
          tone={metrics.finalsTotal > 0 ? (metrics.successRate >= 0.85 ? 'ok' : metrics.successRate >= 0.5 ? 'warn' : 'bad') : undefined}
          hint={`${metrics.finalsWithText}/${metrics.finalsTotal} finais com texto`}
        />
        <StatCard
          label="Coverage"
          value={metrics.activeSessions > 0 ? formatPct(metrics.coverage) : '—'}
          hint="Sessões ativas com ao menos uma transcrição"
        />
        <StatCard
          label="SNR médio"
          value={formatDb(metrics.snrAvgDb)}
          unit="dB"
          tone={snrTone(metrics.snrAvgDb)}
          hint={`min ${formatDb(metrics.snrMinDb)} · max ${formatDb(metrics.snrMaxDb)} — SNR alto → melhor ASR`}
        />
        <StatCard
          label="Latência ASR P95"
          value={formatMs(metrics.latencyP95Ms)}
          unit="ms"
          hint={`média ${formatMs(metrics.latencyAvgMs)} ms · última ${formatMs(metrics.lastLatencyMs)} ms`}
        />
        <StatCard label="WER" value="n/a" hint="Word Error Rate exige referência humana — não disponível" />
        <StatCard label="CER" value="n/a" hint="Character Error Rate exige referência humana — não disponível" />
      </div>

      <div className="cc-asr-charts-wrap">
        <AsrCharts metrics={metrics} />
      </div>

      <div className="cc-asr-transcripts">
        <h3>Transcrições recentes</h3>
        {metrics.recentTranscripts.length === 0 ? (
          <p className="muted">Sem eventos STT — use o Tester ou tráfego no pipeline.</p>
        ) : (
          <table className="cc-apm-table">
            <thead>
              <tr>
                <th>Sessão</th>
                {showChannelColumn && <th>Canal</th>}
                <th>Tipo</th>
                <th>Latência</th>
                <th>Texto</th>
              </tr>
            </thead>
            <tbody>
              {metrics.recentTranscripts.map((row) => (
                <tr key={`${row.sessionId}-${row.t}-${row.type}-${row.text.slice(0, 12)}`}>
                  <td><code>{row.sessionId.slice(0, 16)}</code></td>
                  {showChannelColumn && (
                    <td>
                      {sessions.find((s) => s.sessionId === row.sessionId)?.mediaMeta?.channel_id ??
                        'default'}
                    </td>
                  )}
                  <td>
                    <span className={`badge ${row.ok ? 'ok' : 'bad'}`}>{row.type.replace('stt_', '')}</span>
                    {row.model ? <span className="muted"> {row.model}</span> : null}
                  </td>
                  <td>{row.processingMs != null ? `${formatMs(row.processingMs)} ms` : '—'}</td>
                  <td className="cc-asr-text">{row.text || '(vazio)'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </section>
  );
}
