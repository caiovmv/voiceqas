import { memo, useMemo } from 'react';
import type { RescoredWindow } from '../../lib/domain/vqa-rescore';

interface Props {
  windows?: RescoredWindow[];
  title?: string;
  variant?: 'before' | 'after';
}

const EMPTY: RescoredWindow[] = [];
const MAX_DOTS = 64;

function ScoreTimelineInner({
  windows = EMPTY,
  title = 'Scores',
  variant = 'after',
}: Props) {
  const { path, dots, maxT } = useMemo(() => {
    if (windows.length === 0) {
      return { path: '', dots: [] as RescoredWindow[], maxT: 1 };
    }
    const width = 640;
    const height = 140;
    const pad = 8;
    const tMax = Math.max(...windows.map((x) => x.window_start_ms), 1);
    const d = windows
      .map((x, i) => {
        const px = pad + (x.window_start_ms / tMax) * (width - pad * 2);
        const py = height - pad - (x.speech_quality_score / 100) * (height - pad * 2);
        return `${i === 0 ? 'M' : 'L'}${px.toFixed(1)},${py.toFixed(1)}`;
      })
      .join(' ');
    const stride = Math.max(1, Math.ceil(windows.length / MAX_DOTS));
    const sampled = windows.filter(
      (_, i) => i % stride === 0 || i === windows.length - 1,
    );
    return { path: d, dots: sampled, maxT: tMax };
  }, [windows]);

  if (windows.length === 0) {
    return (
      <div className="analysis-timeline empty">
        <p className="muted">{title}: sem janelas</p>
      </div>
    );
  }

  const width = 640;
  const height = 140;
  const pad = 8;

  return (
    <div className="analysis-timeline">
      <div className="analysis-timeline-head">{title}</div>
      <svg viewBox={`0 0 ${width} ${height}`} className="analysis-timeline-svg" role="img">
        {[25, 50, 75].map((y) => {
          const py = height - pad - (y / 100) * (height - pad * 2);
          return (
            <line
              key={y}
              x1={pad}
              x2={width - pad}
              y1={py}
              y2={py}
              className="analysis-timeline-grid"
            />
          );
        })}
        <path d={path} className={`analysis-timeline-path ${variant}`} fill="none" />
        {dots.map((x) => {
          const px = pad + (x.window_start_ms / maxT) * (width - pad * 2);
          const py = height - pad - (x.speech_quality_score / 100) * (height - pad * 2);
          return (
            <circle
              key={`${variant}-${x.window_start_ms}`}
              cx={px}
              cy={py}
              r={2.5}
              className={x.stt_ready ? 'analysis-dot ready' : 'analysis-dot'}
            />
          );
        })}
      </svg>
      <div className="analysis-timeline-legend">
        <span className={variant === 'before' ? 'legend-before' : 'legend-after'} /> {title}
        <span className="dot ready" /> ready
      </div>
    </div>
  );
}

export const ScoreTimeline = memo(ScoreTimelineInner);
