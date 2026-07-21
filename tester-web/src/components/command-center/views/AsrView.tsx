import { AsrMetricsPanel } from '../AsrMetricsPanel';
import { VadPanel } from '../VadPanel';
import { SessionSelector } from '../SessionSelector';
import { useCommandCenter } from '../../../context/CommandCenterContext';

export function AsrView() {
  const { sessionList, activeSessionId } = useCommandCenter();
  const filter = activeSessionId.trim() || undefined;

  return (
    <>
      <div className="cc-view-toolbar">
        <SessionSelector />
      </div>
      <AsrMetricsPanel sessions={sessionList} filterSessionId={filter} showChannelColumn />
      <VadPanel />
    </>
  );
}
