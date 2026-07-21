export function AlertsBar({
  alerts,
}: {
  alerts: Array<{ sessionId: string; message: string; kind: string }>;
}) {
  if (alerts.length === 0) {
    return null;
  }

  return (
    <section className="panel cc-alerts">
      <h2>Alertas ({alerts.length})</h2>
      <ul className="cc-alert-list">
        {alerts.map((a) => (
          <li key={`${a.sessionId}-${a.kind}`} className={`cc-alert ${a.kind}`}>
            <code>{a.sessionId}</code>
            <span>{a.message}</span>
          </li>
        ))}
      </ul>
    </section>
  );
}
