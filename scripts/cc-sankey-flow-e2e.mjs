#!/usr/bin/env node

/**

 * Sankey flow E2E - traffic + drawable snapshot check.

 * Usage:

 *   node scripts/cc-sankey-flow-e2e.mjs [api]

 *   node scripts/cc-sankey-flow-e2e.mjs [api] --iterations 1000

 *   node scripts/cc-sankey-flow-e2e.mjs [api] --iterations 1000 --ramp-up --max-parallel 32

 */

import { createSocket } from 'node:dgram';

import { writeFileSync } from 'node:fs';



function parseArgs(argv) {

  let api = 'http://127.0.0.1:9080';

  let iterations = 1;

  let maxParallel = 16;

  let rampUp = false;
  let startFrom = 1;

  for (let i = 2; i < argv.length; i += 1) {

    const arg = argv[i];

    if (arg === '--iterations' || arg === '-n') {

      iterations = Math.max(1, Number(argv[++i] ?? 1));

    } else if (arg.startsWith('--iterations=')) {

      iterations = Math.max(1, Number(arg.slice('--iterations='.length)));

    } else if (arg === '--max-parallel' || arg === '-p') {

      maxParallel = Math.max(1, Number(argv[++i] ?? 16));

    } else if (arg.startsWith('--max-parallel=')) {

      maxParallel = Math.max(1, Number(arg.slice('--max-parallel='.length)));

    } else if (arg === '--start-from') {
      startFrom = Math.max(1, Number(argv[++i] ?? 1));
    } else if (arg.startsWith('--start-from=')) {
      startFrom = Math.max(1, Number(arg.slice('--start-from='.length)));
    } else if (arg === '--ramp-up' || arg === '--ramp') {

      rampUp = true;

    } else if (/^https?:\/\//.test(arg)) {

      api = arg.replace(/\/$/, '');

    } else if (/^\d+$/.test(arg)) {

      iterations = Math.max(1, Number(arg));

    }

  }

  return { api, iterations, maxParallel, rampUp, startFrom };

}



const { api: API, iterations: ITERATIONS, maxParallel: MAX_PARALLEL, rampUp: RAMP_UP, startFrom: START_FROM } = parseArgs(process.argv);

const READ = process.env.VOICEQAS_OPS_TOKEN || 'dev-read';

const WRITE = process.env.VOICEQAS_OPS_WRITE_TOKEN || 'dev-write';

const runStamp = Date.now();

const results = [];

const iterationStats = [];
const summary = { passed: 0, failed: 0, priorPassed: START_FROM > 1 ? START_FROM - 1 : 0 };

let peakInflight = 0;

let lastLoggedConcurrency = 0;



function pass(name, detail) {

  results.push({ status: 'PASS', name, detail, at: new Date().toISOString() });

  console.log('PASS  ' + name + ' - ' + detail);

}

function fail(name, detail) {

  results.push({ status: 'FAIL', name, detail, at: new Date().toISOString() });

  console.log('FAIL  ' + name + ' - ' + detail);

}



function targetConcurrency(started, total, maxParallel) {

  if (total <= 1) return 1;

  const progress = started / total;

  return Math.max(1, Math.min(maxParallel, Math.ceil(maxParallel * progress)));

}



function pcmList(seconds = 1, rate = 16000, freq = 440) {

  const n = rate * seconds;

  const out = [];

  for (let i = 0; i < n; i++) {

    const s = Math.round(8000 * Math.sin((2 * Math.PI * freq * i) / rate));

    out.push(s & 0xff, (s >> 8) & 0xff);

  }

  return out;

}



async function fetchJson(url, init = {}) {

  const res = await fetch(url, init);

  const text = await res.text();

  let body;

  try {

    body = text ? JSON.parse(text) : null;

  } catch {

    body = text;

  }

  if (!res.ok) {

    throw new Error(String(res.status) + ' ' + (typeof body === 'string' ? body.slice(0, 200) : JSON.stringify(body).slice(0, 200)));

  }

  return body;

}



async function getSnapshot() {

  return fetchJson(API + '/v1/ops/pipeline/snapshot', { headers: { 'X-Ops-Token': READ } });

}



function resolveSankeyDrawable(snapshot) {

  if (!snapshot) return { drawable: false, reason: 'snapshot null' };

  const serverLinks = snapshot.echarts?.series?.[0]?.links ?? [];

  if (serverLinks.length > 0) {

    const series = snapshot.echarts.series[0];

    const nodeNames = new Set((series.data ?? []).map((d) => d.name));

    const validLinks = serverLinks.filter((l) => l.value > 0 && nodeNames.has(l.source) && nodeNames.has(l.target));

    if (validLinks.length > 0) {

      return {

        drawable: true,

        source: 'echarts',

        links: validLinks.length,

        nodes: series.data?.length ?? 0,

        sample: validLinks.slice(0, 3),

      };

    }

  }

  const nodes = snapshot.nodes ?? [];

  const nodeByName = new Map(nodes.map((n) => [n.name, n]));

  const links = (snapshot.links ?? []).filter((l) => l.value > 0 && nodeByName.has(l.source) && nodeByName.has(l.target));

  if (links.length === 0) return { drawable: false, reason: 'no links with value > 0', nodes: nodes.length };

  return { drawable: true, source: 'nodes+links', links: links.length, nodes: nodes.length, sample: links.slice(0, 3) };

}



async function sendRtpUdp(pcmBytes, format = 'rtp_g722') {

  const pack = await fetchJson(API + '/v1/tools/pack-rtp', {

    method: 'POST',

    headers: { 'Content-Type': 'application/json' },

    body: JSON.stringify({ format, frame_ms: 20, pcm_bytes: pcmBytes }),

  });

  const frames = pack.frames ?? [];

  if (frames.length < 5) throw new Error('pack-rtp returned ' + frames.length + ' frames');

  const udp = createSocket('udp4');

  await new Promise((resolve, reject) => {

    udp.on('error', reject);

    let i = 0;

    const sendNext = () => {

      if (i >= frames.length) {

        udp.close();

        resolve();

        return;

      }

      const buf = Buffer.from(frames[i++]);

      udp.send(buf, 10000, '127.0.0.1', (err) => {

        if (err) {

          udp.close();

          reject(err);

          return;

        }

        setTimeout(sendNext, 18);

      });

    };

    sendNext();

  });

  return frames.length;

}



async function generatePipelineTraffic(iterIndex) {

  const stamp = runStamp + '-' + iterIndex;

  const sessionRtp = 'sankey-e2e-' + stamp;

  const pcm16 = pcmList(2, 16000, 440 + (iterIndex % 200));

  const pcm8 = pcmList(1, 8000, 523 + (iterIndex % 100));

  await fetchJson(API + '/v1/analyze/segment', {

    method: 'POST',

    headers: { 'Content-Type': 'application/json' },

    body: JSON.stringify({

      format: 'pcm_s16le_16k',

      sample_rate: 16000,

      session_id: 'sankey-vqa-' + stamp,

      pcm_bytes: pcm16,

    }),

  });

  await fetchJson(API + '/v1/stt/transcribe/segment', {

    method: 'POST',

    headers: { 'Content-Type': 'application/json' },

    body: JSON.stringify({

      format: 'pcm_s16le_16k',

      sample_rate: 16000,

      language: 'pt',

      session_id: 'sankey-stt-' + stamp,

      pcm_bytes: pcm16,

    }),

  });

  await fetch(API + '/v1/analyze/batch', {

    method: 'POST',

    headers: {

      'X-Sample-Rate': '8000',

      'X-Audio-Format': 'pcm_s16le_8k',

      'X-Session-Id': 'sankey-batch-' + stamp,

      'Content-Type': 'application/octet-stream',

    },

    body: Buffer.from(pcm8),

  });

  await fetchJson(API + '/v1/media/sessions', {

    method: 'POST',

    headers: { 'Content-Type': 'application/json', 'X-Ops-Token': WRITE },

    body: JSON.stringify({ session_id: sessionRtp, format: 'rtp_g722', sample_rate: 16000 }),

  });

  const rtpFrames = await sendRtpUdp(pcm16, 'rtp_g722');

  await fetch(API + '/v1/media/sessions/' + encodeURIComponent(sessionRtp) + '/agent-audio', {

    method: 'POST',

    headers: {

      'X-Sample-Rate': '16000',

      'X-Ops-Token': WRITE,

      'Content-Type': 'application/octet-stream',

    },

    body: Buffer.from(pcm16),

  });

  return { rtpFrames, sessionRtp };

}



async function waitForDrawableSnapshot(maxAttempts = 8, delayMs = 1500) {

  let last = null;

  for (let i = 0; i < maxAttempts; i += 1) {

    last = await getSnapshot();

    const check = resolveSankeyDrawable(last);

    if (check.drawable) return { snapshot: last, check, attempt: i + 1 };

    await new Promise((r) => setTimeout(r, delayMs));

  }

  return { snapshot: last, check: resolveSankeyDrawable(last), attempt: maxAttempts };

}



async function runOneIteration(iter, inflightAtStart) {

  const iterStarted = Date.now();

  let ok = false;

  let detail = '';

  try {

    const traffic = await generatePipelineTraffic(iter);

    const { snapshot, check, attempt } = await waitForDrawableSnapshot();

    const rtp = snapshot.nodes?.find((n) => n.name === 'rtp_ingress')?.metrics?.bytes_out ?? 0;

    const vqa = snapshot.nodes?.find((n) => n.name === 'vqa')?.metrics?.bytes_out ?? 0;

    const asr = snapshot.nodes?.find((n) => n.name === 'asr')?.metrics?.bytes_out ?? 0;

    if (check.drawable) {

      ok = true;

      detail =

        'links=' + check.links +

        ' rtp=' + rtp +

        ' vqa=' + vqa +

        ' asr=' + asr +

        ' session=' + traffic.sessionRtp +

        ' rtp_frames=' + traffic.rtpFrames +

        ' attempt=' + attempt;

    } else {

      detail =

        (check.reason ?? 'empty') +

        ' after ' + attempt + ' polls rtp=' + rtp + ' vqa=' + vqa + ' asr=' + asr;

    }

  } catch (e) {

    detail = e.message;

  }

  const elapsedMs = Date.now() - iterStarted;

  return { iter, ok, detail, elapsedMs, inflight: inflightAtStart, at: new Date().toISOString() };

}



function logIterationStat(stat, completed, modeLabel) {

  if (stat.ok) summary.passed += 1;
  else summary.failed += 1;
  iterationStats.push(stat);
  if (iterationStats.length > 50) iterationStats.shift();

  const tag = modeLabel + ' iter=' + stat.iter + ' done=' + completed + '/' + ITERATIONS;

  if (ITERATIONS === 1) {

    if (stat.ok) pass('Pipeline traffic + Sankey drawable', stat.detail);

    else fail('Pipeline traffic + Sankey drawable', stat.detail);

    return;

  }

  const line =

    '[' + tag + ' inflight=' + stat.inflight + '] ' +

    (stat.ok ? 'PASS' : 'FAIL') + ' ' + stat.detail + ' (' + stat.elapsedMs + 'ms)';

  if (stat.iter === 1 || completed === ITERATIONS || completed % 50 === 0) {

    console.log(line);

  } else if (!stat.ok) {

    console.log(line);

  }

}



function writeReport(exitCode, shouldExit = true) {
  const passed = summary.priorPassed + summary.passed;
  const failed = summary.failed;
  const out = {
    at: new Date().toISOString(),
    api: API,
    iterations: ITERATIONS,
    startFrom: START_FROM,
    rampUp: RAMP_UP,
    maxParallel: MAX_PARALLEL,
    peakInflight,
    exitCode,
    summary: { passed, failed, total: passed + failed, thisRun: { passed: summary.passed, failed: summary.failed } },
    recentStats: iterationStats,
    results,
  };
  writeFileSync('scripts/cc-sankey-flow-e2e-last.json', JSON.stringify(out, null, 2) + '\n');
  console.log('\nReport: scripts/cc-sankey-flow-e2e-last.json');
  console.log('Summary: ' + passed + '/' + (passed + failed) + ' iterations PASS, ' + failed + ' FAIL, peakInflight=' + peakInflight);
  if (shouldExit) process.exit(exitCode);
}



async function runSequential() {

  for (let iter = 1; iter <= ITERATIONS; iter += 1) {

    const stat = await runOneIteration(iter, 1);

    logIterationStat(stat, iter, 'seq');

  }

}



async function runRampUp() {

  let nextIter = START_FROM;
  let completed = 0;
  let started = 0;
  const inFlight = new Set();
  const remaining = ITERATIONS - START_FROM + 1;

  while (completed < remaining) {
    const target = targetConcurrency(started, remaining, MAX_PARALLEL);

    if (target !== lastLoggedConcurrency) {

      console.log('[ramp] target concurrency ' + lastLoggedConcurrency + ' -> ' + target + ' (started=' + started + ')');

      lastLoggedConcurrency = target;

    }

    while (inFlight.size < target && nextIter <= ITERATIONS) {

      const iter = nextIter++;

      started += 1;

      const inflightNow = inFlight.size + 1;

      peakInflight = Math.max(peakInflight, inflightNow);

      const job = runOneIteration(iter, inflightNow)

        .then((stat) => {

          completed += 1;
          logIterationStat(stat, START_FROM - 1 + completed, 'ramp');
          if (completed % 50 === 0) writeReport(-1, false);

        })

        .finally(() => {

          inFlight.delete(job);

        });

      inFlight.add(job);

    }

    if (inFlight.size === 0) break;

    await Promise.race(inFlight);

  }

  await Promise.all(inFlight);

}



console.log(

  'Sankey flow E2E @ ' + API +

  ' (iterations=' + ITERATIONS +

  ', rampUp=' + RAMP_UP +

  ', maxParallel=' + MAX_PARALLEL + ', startFrom=' + START_FROM + ')\n',

);



try {

  const health = await fetchJson(API + '/health');

  if (health?.status !== 'ok') throw new Error(JSON.stringify(health));

  pass('Health', 'voiceqas ok');

} catch (e) {

  fail('Health', e.message);

  writeReport(1);

}



if (ITERATIONS === 1) {

  try {

    const baseline = await getSnapshot();

    const b = resolveSankeyDrawable(baseline);

    pass(

      'Baseline snapshot',

      'nodes=' + (baseline.nodes?.length ?? 0) + ' links=' + (baseline.links?.length ?? 0) + ' drawable=' + b.drawable,

    );

  } catch (e) {

    fail('Baseline snapshot', e.message);

    writeReport(1);

  }

}



const started = Date.now();

if (RAMP_UP) {

  await runRampUp();

} else {

  await runSequential();

}



const totalSec = ((Date.now() - started) / 1000).toFixed(1);

const passed = summary.priorPassed + summary.passed;
pass(
  'Run complete',
  passed + '/' + ITERATIONS + ' PASS in ' + totalSec + 's avg=' + (Number(totalSec) / Math.max(1, summary.passed + summary.failed)).toFixed(2) + 's/iter peakInflight=' + peakInflight,
);
writeReport(passed === ITERATIONS ? 0 : 1);

