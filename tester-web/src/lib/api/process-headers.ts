import type { PipelineMixState } from '../domain/pipeline-mix';
import { processFlagsFromMix, stripJsonForHeader } from '../domain/pipeline-mix';

export function headersFromMix(mix: PipelineMixState): Record<string, string> {
  const f = processFlagsFromMix(mix);
  return {
    'X-Audio-AGC': f.agc ? '1' : '0',
    'X-Audio-Enhancement': f.enhancement ? '1' : '0',
    'X-Audio-Strip': stripJsonForHeader(mix.strip),
    'X-STT-Diarization': f.diarization ? '1' : '0',
    'X-STT-Focus-Primary': f.focusPrimary ? '1' : '0',
  };
}
