import { useEffect, useState } from 'react';
import { AnalysisLabApp } from './components/analysis/AnalysisLabApp';
import { CommandCenterApp } from './CommandCenterApp';
import { TesterApp } from './TesterApp';

import { isCommandCenterHash } from './lib/domain/cc-routes';

type AppMode = 'tester' | 'command-center' | 'analysis';

function modeFromHash(): AppMode {
  if (isCommandCenterHash(location.hash)) return 'command-center';
  if (location.hash === '#analysis') return 'analysis';
  return 'tester';
}

function hashFor(mode: AppMode): string {
  if (mode === 'command-center') return '#command-center';
  if (mode === 'analysis') return '#analysis';
  return '#tester';
}

export default function App() {
  const [mode, setMode] = useState<AppMode>(modeFromHash);

  useEffect(() => {
    const onHash = () => setMode(modeFromHash());
    window.addEventListener('hashchange', onHash);
    return () => window.removeEventListener('hashchange', onHash);
  }, []);

  const go = (next: AppMode) => {
    location.hash = hashFor(next);
    setMode(next);
  };

  const wide = mode === 'command-center' || mode === 'analysis';

  return (
    <div className={wide ? 'layout layout-wide' : 'layout'}>
      <nav className="app-nav">
        <button
          type="button"
          className={`tab ${mode === 'tester' ? 'active' : ''}`}
          onClick={() => go('tester')}
        >
          Tester
        </button>
        <button
          type="button"
          className={`tab ${mode === 'analysis' ? 'active' : ''}`}
          onClick={() => go('analysis')}
        >
          Análise
        </button>
        <button
          type="button"
          className={`tab ${mode === 'command-center' ? 'active' : ''}`}
          onClick={() => go('command-center')}
        >
          Command Center
        </button>
      </nav>

      {mode === 'tester' && <TesterApp />}
      {mode === 'analysis' && <AnalysisLabApp />}
      {mode === 'command-center' && <CommandCenterApp />}
    </div>
  );
}
