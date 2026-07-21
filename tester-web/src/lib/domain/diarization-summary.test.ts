import { describe, expect, it } from 'vitest';
import { summarizeDiarization } from './diarization-summary';

describe('summarizeDiarization', () => {
  it('counts primary and secondary from turns', () => {
    const summary = summarizeDiarization(
      [
        { start_ms: 0, end_ms: 1000, speaker_id: 0, is_primary: true },
        { start_ms: 1200, end_ms: 2000, speaker_id: 1, is_primary: false },
        { start_ms: 2100, end_ms: 3000, speaker_id: 0, is_primary: true },
      ],
      0,
    );
    expect(summary.speakerCount).toBe(2);
    expect(summary.speakers[0].role).toBe('primary');
    expect(summary.speakers[1].role).toBe('secondary');
    expect(summary.speakers[0].duration_ms).toBe(1900);
  });
});
