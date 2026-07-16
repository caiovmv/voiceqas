import { ApmPanel } from '../ApmPanel';
import { RedMetricsPanel } from '../RedMetricsPanel';

export function ObservabilityView() {
  return (
    <>
      <RedMetricsPanel />
      <ApmPanel />
    </>
  );
}
