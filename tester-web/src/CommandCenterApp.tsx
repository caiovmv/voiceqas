import { CommandCenterProvider } from './context/CommandCenterContext';
import { CommandCenterShell } from './components/command-center/CommandCenterShell';

export function CommandCenterApp() {
  return (
    <CommandCenterProvider>
      <CommandCenterShell />
    </CommandCenterProvider>
  );
}
