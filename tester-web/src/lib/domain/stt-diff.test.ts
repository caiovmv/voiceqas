import { describe, expect, it } from 'vitest';
import { afterViewChunks, beforeViewChunks, diffWords } from './stt-diff';

describe('diffWords', () => {
  it('marks inserts and deletes', () => {
    const chunks = diffWords('ola mundo', 'ola belo mundo');
    expect(chunks.some((c) => c.type === 'insert' && c.text === 'belo')).toBe(true);
    expect(chunks.filter((c) => c.type === 'equal').map((c) => c.text)).toEqual(['ola', 'mundo']);
  });

  it('splits chunks for before / after text boxes', () => {
    const chunks = diffWords('ola mundo', 'ola belo mundo');
    expect(beforeViewChunks(chunks).map((c) => `${c.type}:${c.text}`)).toEqual([
      'equal:ola mundo',
    ]);
    expect(afterViewChunks(chunks).map((c) => `${c.type}:${c.text}`)).toEqual([
      'equal:ola',
      'insert:belo',
      'equal:mundo',
    ]);
  });

  it('merges adjacent equal words', () => {
    const merged = beforeViewChunks([
      { type: 'equal', text: 'a' },
      { type: 'equal', text: 'b' },
      { type: 'delete', text: 'x' },
    ]);
    expect(merged).toEqual([
      { type: 'equal', text: 'a b' },
      { type: 'delete', text: 'x' },
    ]);
  });
});
