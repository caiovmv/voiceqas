import { useEffect, useRef } from 'react';
import * as echarts from 'echarts/core';
import { SankeyChart } from 'echarts/charts';
import { TooltipComponent } from 'echarts/components';
import { CanvasRenderer } from 'echarts/renderers';
import type { SankeyEchartsOption } from '../../lib/domain/pipeline-sankey';

echarts.use([SankeyChart, TooltipComponent, CanvasRenderer]);

interface PipelineSankeyChartProps {
  option: SankeyEchartsOption | null;
  height?: number;
}

export function PipelineSankeyChart({ option, height = 420 }: PipelineSankeyChartProps) {
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
    if (!option) {
      chart.clear();
      return;
    }
    chart.setOption(option, { notMerge: true, lazyUpdate: true });
    chart.resize();
  }, [option]);

  return (
    <div className="cc-sankey-canvas-wrap" style={{ height, position: 'relative' }}>
      {!option && (
        <div
          className="cc-sankey-empty muted"
          style={{ position: 'absolute', inset: 0, zIndex: 1 }}
        >
          Sem fluxo Sankey — aguarde tráfego no pipeline.
        </div>
      )}
      <div
        ref={hostRef}
        className="cc-sankey-host"
        style={{ height, width: '100%' }}
        role="img"
        aria-label="Audio pipeline Sankey"
        aria-hidden={!option}
      />
    </div>
  );
}
