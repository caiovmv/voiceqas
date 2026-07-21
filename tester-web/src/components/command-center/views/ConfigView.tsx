import { AuthPanel } from '../AuthPanel';
import { ChannelsPanel } from '../ChannelsPanel';
import { PipelineDefaultPanel } from '../PipelineDefaultPanel';

export function ConfigView() {
  return (
    <>
      <AuthPanel />
      <PipelineDefaultPanel />
      <ChannelsPanel />
    </>
  );
}
