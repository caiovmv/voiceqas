import { useCallback, useEffect, useMemo, useState, type ReactNode } from 'react';
import { MonitorView } from './views/MonitorView';
import { PipelineView } from './views/PipelineView';
import { AsrView } from './views/AsrView';
import { SessionsView } from './views/SessionsView';
import { AlertsBar } from './AlertsBar';
import { ApmPanel } from './ApmPanel';
import { RedMetricsPanel } from './RedMetricsPanel';
import { useCommandCenter } from '../../context/CommandCenterContext';

const PIN_KEY = 'cc-noc-pinned-slide';
const AUTOPLAY_MS = 30_000;

interface Slide {
  id: string;
  title: string;
  content: ReactNode;
}

export function NocSlideDeck() {
  const { alerts } = useCommandCenter();
  const slides: Slide[] = useMemo(
    () => [
      { id: 'overview', title: 'Overview', content: <MonitorView /> },
      { id: 'pipeline', title: 'Pipeline', content: <PipelineView /> },
      { id: 'asr', title: 'ASR / STT', content: <AsrView /> },
      { id: 'sessions', title: 'Sessoes', content: <SessionsView /> },
      { id: 'alerts', title: 'Alertas', content: <AlertsBar alerts={alerts} /> },
      { id: 'red', title: 'RED', content: <RedMetricsPanel /> },
      { id: 'apm', title: 'APM', content: <ApmPanel /> },
    ],
    [alerts],
  );

  const [index, setIndex] = useState(0);
  const [pinned, setPinned] = useState(() => localStorage.getItem(PIN_KEY) === '1');
  const [pinnedSlide, setPinnedSlide] = useState<string | null>(() => localStorage.getItem('cc-noc-pinned-id'));

  const activeIndex = useMemo(() => {
    if (pinned && pinnedSlide) {
      const i = slides.findIndex((s) => s.id === pinnedSlide);
      return i >= 0 ? i : index;
    }
    return index;
  }, [pinned, pinnedSlide, slides, index]);

  const go = useCallback(
    (delta: number) => {
      setIndex((i) => (i + delta + slides.length) % slides.length);
    },
    [slides.length],
  );

  const togglePin = useCallback(() => {
    setPinned((p) => {
      const next = !p;
      localStorage.setItem(PIN_KEY, next ? '1' : '0');
      if (next) {
        const id = slides[activeIndex]?.id ?? 'overview';
        setPinnedSlide(id);
        localStorage.setItem('cc-noc-pinned-id', id);
      }
      return next;
    });
  }, [slides, activeIndex]);

  useEffect(() => {
    if (pinned) return undefined;
    const id = window.setInterval(() => go(1), AUTOPLAY_MS);
    return () => window.clearInterval(id);
  }, [pinned, go]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'ArrowRight') go(1);
      if (e.key === 'ArrowLeft') go(-1);
      if (e.key === 'p' || e.key === 'P') togglePin();
      if (e.key === 'f' || e.key === 'F') {
        if (document.fullscreenElement) void document.exitFullscreen();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [go, togglePin]);

  const slide = slides[activeIndex];

  return (
    <div className="cc-noc-deck">
      <div className="cc-noc-deck-head">
        <h2>{slide?.title}</h2>
        <div className="cc-noc-controls">
          <button type="button" className="btn" onClick={() => go(-1)} aria-label="Slide anterior">
            prev
          </button>
          <span className="muted">
            {activeIndex + 1} / {slides.length}
          </span>
          <button type="button" className="btn" onClick={() => go(1)} aria-label="Proximo slide">
            next
          </button>
          <button type="button" className={`btn ${pinned ? 'primary' : ''}`} onClick={togglePin}>
            {pinned ? 'Fixado' : 'Fixar slide'}
          </button>
        </div>
        <div className="cc-noc-dots">
          {slides.map((s, i) => (
            <button
              key={s.id}
              type="button"
              className={`cc-noc-dot ${i === activeIndex ? 'active' : ''}`}
              onClick={() => setIndex(i)}
              aria-label={s.title}
            />
          ))}
        </div>
      </div>
      <div className="cc-noc-slide">{slide?.content}</div>
      <p className="muted cc-noc-hint">Setas navegar, P fixar, ESC sair fullscreen</p>
    </div>
  );
}
