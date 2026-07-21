export interface DiffChunk {
  type: 'equal' | 'insert' | 'delete';
  text: string;
}

/** Collapse consecutive same-type word chips into fewer DOM nodes (selection perf). */
export function mergeAdjacentChunks(chunks: DiffChunk[]): DiffChunk[] {
  const out: DiffChunk[] = [];
  for (const c of chunks) {
    const last = out[out.length - 1];
    if (last && last.type === c.type) {
      last.text = `${last.text} ${c.text}`;
    } else {
      out.push({ type: c.type, text: c.text });
    }
  }
  return out;
}

/** Chunks to paint inside the "antes" box (equal + deletes). */
export function beforeViewChunks(chunks: DiffChunk[]): DiffChunk[] {
  return mergeAdjacentChunks(chunks.filter((c) => c.type === 'equal' || c.type === 'delete'));
}

/** Chunks to paint inside the "depois" box (equal + inserts). */
export function afterViewChunks(chunks: DiffChunk[]): DiffChunk[] {
  return mergeAdjacentChunks(chunks.filter((c) => c.type === 'equal' || c.type === 'insert'));
}

export function diffWords(before: string, after: string): DiffChunk[] {
  const a = before.trim().split(/\s+/).filter(Boolean);
  const b = after.trim().split(/\s+/).filter(Boolean);
  const n = a.length;
  const m = b.length;
  const dp: number[][] = Array.from({ length: n + 1 }, () => Array(m + 1).fill(0));
  for (let i = n - 1; i >= 0; i--) {
    for (let j = m - 1; j >= 0; j--) {
      dp[i][j] = a[i] === b[j] ? dp[i + 1][j + 1] + 1 : Math.max(dp[i + 1][j], dp[i][j + 1]);
    }
  }
  const out: DiffChunk[] = [];
  let i = 0;
  let j = 0;
  while (i < n && j < m) {
    if (a[i] === b[j]) {
      out.push({ type: 'equal', text: a[i] });
      i++;
      j++;
    } else if (dp[i + 1][j] >= dp[i][j + 1]) {
      out.push({ type: 'delete', text: a[i] });
      i++;
    } else {
      out.push({ type: 'insert', text: b[j] });
      j++;
    }
  }
  while (i < n) out.push({ type: 'delete', text: a[i++] });
  while (j < m) out.push({ type: 'insert', text: b[j++] });
  return out;
}
