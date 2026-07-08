#!/usr/bin/env node
/** Quick STT WebSocket flush test (Node 22+ global WebSocket) */

const wsBase = process.argv[2] || 'ws://127.0.0.1:9081';
const audioSec = Number(process.argv[3] || 2);
const flushTimeoutMs = Number(process.argv[4] || 180_000);

function pcmBytes(seconds = 2, rate = 16000, freq = 440) {
  const n = rate * seconds;
  const out = Buffer.alloc(n * 2);
  for (let i = 0; i < n; i++) {
    const s = Math.round(8000 * Math.sin((2 * Math.PI * freq * i) / rate));
    out.writeInt16LE(s, i * 2);
  }
  return out;
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function main() {
  const uri = `${wsBase}/v1/stt/stream`;
  const ws = new WebSocket(uri);
  let handshakeOk = false;

  const fail = (msg) => {
    console.error('FAIL:', msg);
    try {
      ws.close();
    } catch {}
    process.exit(1);
  };

  ws.addEventListener('error', () => fail('WebSocket error'));
  ws.addEventListener('close', () => {
    if (!handshakeOk) fail('closed before handshake');
  });

  await new Promise((resolve, reject) => {
    ws.addEventListener('open', resolve, { once: true });
    ws.addEventListener('error', reject, { once: true });
  });

  ws.send(
    JSON.stringify({
      session_id: `ws-flush-${Date.now()}`,
      format: 'pcm_s16le_16k',
      sample_rate: 16000,
      model: 'parakeet',
    }),
  );

  const ack = await new Promise((resolve, reject) => {
    const t = setTimeout(() => reject(new Error('handshake timeout')), 10_000);
    ws.addEventListener(
      'message',
      (ev) => {
        clearTimeout(t);
        resolve(ev.data.toString());
      },
      { once: true },
    );
  });

  const ackJson = JSON.parse(ack);
  if (ackJson.status !== 'ok') fail(`handshake: ${ack}`);
  handshakeOk = true;
  console.log('Handshake OK');

  const pcm = pcmBytes(audioSec);
  const frameBytes = 640;
  for (let off = 0; off < pcm.length; off += frameBytes) {
    ws.send(pcm.subarray(off, off + frameBytes));
    await sleep(20);
  }

  console.log('Sending flush...');
  const t0 = Date.now();
  ws.send(JSON.stringify({ type: 'flush', model: 'parakeet' }));

  const finalText = await Promise.race([
    new Promise((resolve) => {
      ws.addEventListener(
        'message',
        (ev) => resolve(ev.data.toString()),
        { once: true },
      );
    }),
    new Promise((_, reject) =>
      setTimeout(() => reject(new Error('STT flush timeout')), flushTimeoutMs),
    ),
  ]);

  const elapsed = Date.now() - t0;
  console.log(`Flush response (${elapsed}ms):`, finalText);
  const parsed = JSON.parse(finalText);
  if (parsed.type === 'final' || parsed.ok === true) {
    console.log('PASS');
    ws.close();
    process.exit(0);
  }
  fail(finalText);
}

main().catch((e) => {
  console.error('FAIL:', e.message);
  process.exit(1);
});
