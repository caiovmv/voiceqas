import { beforeEach, describe, expect, it } from 'vitest';
import { getOpsToken, setOpsToken } from '../auth';
import { concatFrames, opsWsHandshake, parseWsEventData, parseWsJson } from './util';

describe('api util', () => {
  beforeEach(() => {
    setOpsToken('');
  });

  it('strips null padding from websocket JSON', () => {
    const parsed = parseWsJson('{"status":"ok"}\0\0') as { status: string };
    expect(parsed.status).toBe('ok');
  });

  it('parses websocket event data from ArrayBuffer', async () => {
    const buf = new TextEncoder().encode('{"status":"ok"}').buffer;
    const parsed = (await parseWsEventData(buf)) as { status: string };
    expect(parsed.status).toBe('ok');
  });

  it('concatenates encoded frames', () => {
    const out = concatFrames([new Uint8Array([1, 2]), new Uint8Array([3])]);
    expect(Array.from(out)).toEqual([1, 2, 3]);
  });

  it('builds ops websocket handshake with token', () => {
    setOpsToken('dev-read');
    const hs = opsWsHandshake({ filterSessionId: 'call-1', subscribeStt: false });
    expect(hs.filter_session_id).toBe('call-1');
    expect(hs.subscribe_stt).toBe(false);
    expect(hs.token).toBe('dev-read');
    expect(getOpsToken()).toBe('dev-read');
  });
});
