import { PipelinePanel } from '../PipelinePanel';
import { SessionSelector } from '../SessionSelector';
import { useCommandCenter } from '../../../context/CommandCenterContext';

export function PipelineView() {
  const { snapshot, pipelineLoading, pipelineError, activeSessionId, setActiveSessionId, navigate } =
    useCommandCenter();

  return (
    <>
      <div className="cc-view-toolbar">
        <SessionSelector />
      </div>
      <PipelinePanel
        snapshot={snapshot}
        loading={pipelineLoading}
        error={pipelineError}
        sessionFilter={activeSessionId}
        onSessionFilterChange={setActiveSessionId}
        onChannelLink={() => navigate('config')}
      />
    </>
  );
}
