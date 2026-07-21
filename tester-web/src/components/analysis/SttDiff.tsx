import { memo } from 'react';
import type { DiffChunk } from '../../lib/domain/stt-diff';

interface Props {
  chunks: DiffChunk[];
  emptyHint?: string;
}

/** Renders word-level diff styles inside Texto antes / Texto depois boxes. */
function DiffTextInner({ chunks, emptyHint }: Props) {
  if (chunks.length === 0) {
    return <p className="stt-text muted">{emptyHint ?? '(vazio)'}</p>;
  }
  return (
    <p className="stt-text stt-diff-inline">
      {chunks.map((c, i) => (
        <span key={i} className={`stt-diff-${c.type}`}>
          {c.text}{' '}
        </span>
      ))}
    </p>
  );
}

export const DiffText = memo(DiffTextInner);
