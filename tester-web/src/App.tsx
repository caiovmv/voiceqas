import { useEffect, useState } from 'react';
import { CommandCenterApp } from './CommandCenterApp';
import { TesterApp } from './TesterApp';

type AppMode = 'tester' | 'command-center';

function modeFromHash(): AppMode {
  return location.hash === '#command-center' ? 'command-center' : 'tester';
}

export default function App() {
  const [mode, setMode] = useState<AppMode>(modeFromHash);

  useEffect(() => {
    const onHash = () => setMode(modeFromHash());
    window.addEventListener('hashchange', onHash);
    return () => window.removeEventListener('hashchange', onHash);
  }, []);

  const go = (next: AppMode) => {
    location.hash = next === 'command-center' ? '#command-center' : '#tester';
    setMode(next);
  };

  return (
    <div className={mode === 'command-center' ? 'layout layout-wide' : 'layout'}>
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
          className={`tab ${mode === 'command-center' ? 'active' : ''}`}
          onClick={() => go('command-center')}
        >
          Command Center
        </button>
      </nav>

      {mode === 'tester' ? <TesterApp /> : <CommandCenterApp />}
    </div>
  );
}
