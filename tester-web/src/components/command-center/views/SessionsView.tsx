import { HistoryPanel } from '../HistoryPanel';
import { MediaSessionPanel } from '../MediaSessionPanel';
import { SessionGrid } from '../SessionGrid';
import { SessionSelector } from '../SessionSelector';
import { useCommandCenter } from '../../../context/CommandCenterContext';

export function SessionsView() {
  const {
    sessionList,
    opsStatus,
    activeSessionId,
    onMediaRegistered,
    onMediaClosed,
    refreshMediaSessions,
  } = useCommandCenter();

  return (
    <>
      <div className="cc-view-toolbar">
        <SessionSelector />
      </div>
      <div className="cc-grid">
        <MediaSessionPanel
          onRegistered={onMediaRegistered}
          onClosed={onMediaClosed}
          onRefresh={refreshMediaSessions}
        />
        <section className="panel cc-sessions-panel">
          <div className="cc-panel-head">
            <h2>Sessoes ao vivo ({sessionList.length})</h2>
            <span className="muted">{opsStatus}</span>
          </div>
          <SessionGrid sessions={sessionList} showChannelColumn />
        </section>
      </div>
      <div className="cc-grid cc-grid-bottom">
        <HistoryPanel sessionId={activeSessionId.trim() || undefined} />
      </div>
    </>
  );
}
