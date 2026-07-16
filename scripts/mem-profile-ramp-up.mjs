#!/usr/bin/env node
/** Memory profiler during Sankey ramp-up E2E. Exit 2 = leak suspect. */
import { spawn } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { setTimeout as sleep } from 'node:timers/promises';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = join(__dirname, '..');

function parseArgs(argv) {
  let api = 'http://127.0.0.1:9080';
  let iterations = 1000;
  let rampUp = true;
  let container = 'voiceqas';
  let sampleSec = 5;
  let baselineSec = 15;
  let cooldownSec = 120;
  let leakGrowthPct = 15;
  let leakSlopeMiBPerMin = 50;
  const e2eExtra = [];
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--api') api = argv[++i];
    else if (arg === '--iterations' || arg === '-n') iterations = Math.max(1, Number(argv[++i]));
    else if (arg === '--no-ramp-up') rampUp = false;
    else if (arg === '--container') container = argv[++i];
    else if (arg === '--sample-sec') sampleSec = Math.max(1, Number(argv[++i]));
    else if (arg === '--baseline-sec') baselineSec = Math.max(5, Number(argv[++i]));
    else if (arg === '--cooldown-sec') cooldownSec = Math.max(30, Number(argv[++i]));
    else if (arg === '--leak-growth-pct') leakGrowthPct = Number(argv[++i]);
    else if (arg === '--leak-slope-mib-min') leakSlopeMiBPerMin = Number(argv[++i]);
    else if (arg.startsWith('--')) {
      e2eExtra.push(arg);
      if (i + 1 < argv.length && !argv[i + 1].startsWith('--')) e2eExtra.push(argv[++i]);
    } else if (/^https?:\/\//.test(arg)) api = arg.replace(/\/$/, '');
  }
  return { api, iterations, rampUp, container, sampleSec, baselineSec, cooldownSec, leakGrowthPct, leakSlopeMiBPerMin, e2eExtra };
}

function parseMemUsage(raw) {
  const m = String(raw || '').match(/([\d.]+)\s*([KMG]?i?B)\s*\/\s*([\d.]+)\s*([KMG]?i?B)/i);
  if (!m) return null;
  const toBytes = (v, u) => {
    const n = Number(v);
    const unit = (u || 'B').toUpperCase();
    if (unit.startsWith('G')) return n * 1024 ** 3;
    if (unit.startsWith('M')) return n * 1024 ** 2;
    if (unit.startsWith('K')) return n * 1024;
    return n;
  };
  const usage_bytes = toBytes(m[1], m[2]);
  return { usage_bytes, limit_bytes: toBytes(m[3], m[4]), usage_gib: usage_bytes / 1024 ** 3, raw: String(raw).trim() };
}

function execText(cmd, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, { shell: true, windowsHide: true });
    let out = '';
    let err = '';
    child.stdout.on('data', (d) => { out += d; });
    child.stderr.on('data', (d) => { err += d; });
    child.on('error', reject);
    child.on('close', (code) => (code !== 0 ? reject(new Error((err || out || cmd).trim())) : resolve(out.trim())));
  });
}

async function sampleContainer(container) {
  const at = new Date().toISOString();
  let mem = null;
  let cgroup_bytes = null;
  let restart_count = null;
  try {
    mem = parseMemUsage(await execText('docker', ['stats', container, '--no-stream', '--format', '{{.MemUsage}}']));
  } catch (e) {
    mem = { error: e.message };
  }
  try { restart_count = Number(await execText('docker', ['inspect', container, '--format', '{{.RestartCount}}'])); } catch {}
  try {
    const cg = await execText('docker', ['exec', container, 'sh', '-c', 'cat /sys/fs/cgroup/memory.current 2>/dev/null || cat /sys/fs/cgroup/memory/memory.usage_in_bytes 2>/dev/null || echo 0']);
    const n = Number(cg);
    if (Number.isFinite(n) && n > 0) cgroup_bytes = n;
  } catch {}
  return { at, phase: 'unknown', mem, cgroup_bytes, restart_count };
}

async function fetchPipelineSessionCount(api) {
  try {
    const res = await fetch(`${api}/v1/ops/pipeline/sessions`, { headers: { 'X-Ops-Token': 'dev-read' } });
    if (!res.ok) return null;
    const body = await res.json();
    return (body.session_ids ?? body.active_sessions?.map((s) => s.session_id) ?? []).length;
  } catch { return null; }
}

function avg(nums) { return nums.length ? nums.reduce((a, b) => a + b, 0) / nums.length : 0; }

function linearSlopeMiBPerMin(samples) {
  if (samples.length < 3) return 0;
  const t0 = Date.parse(samples[0].at);
  const pts = samples.filter((s) => s.mem?.usage_bytes).map((s) => ({ t: (Date.parse(s.at) - t0) / 60000, y: s.mem.usage_bytes / 1024 ** 2 }));
  if (pts.length < 3) return 0;
  const meanT = avg(pts.map((p) => p.t));
  const meanY = avg(pts.map((p) => p.y));
  let num = 0; let den = 0;
  for (const p of pts) { num += (p.t - meanT) * (p.y - meanY); den += (p.t - meanT) ** 2; }
  return den === 0 ? 0 : num / den;
}

function analyze(samples, opts) {
  const withMem = samples.filter((s) => s.mem?.usage_bytes);
  const baseline = withMem.filter((s) => s.phase === 'baseline');
  const cooldown = withMem.filter((s) => s.phase === 'cooldown');
  const baselineAvg = avg(baseline.map((s) => s.mem.usage_bytes));
  const peak = withMem.reduce((m, s) => Math.max(m, s.mem.usage_bytes), 0);
  const cooldownLast = cooldown.slice(-Math.max(6, Math.floor(cooldown.length / 2)));
  const end = cooldown.length ? cooldown[cooldown.length - 1].mem.usage_bytes : withMem.at(-1)?.mem.usage_bytes ?? 0;
  const growthPct = baselineAvg > 0 ? ((end - baselineAvg) / baselineAvg) * 100 : 0;
  const slope = linearSlopeMiBPerMin(cooldownLast);
  const leakSuspect = growthPct > opts.leakGrowthPct && slope > opts.leakSlopeMiBPerMin;
  return {
    baseline_avg_gib: baselineAvg / 1024 ** 3,
    peak_gib: peak / 1024 ** 3,
    cooldown_end_gib: end / 1024 ** 3,
    growth_pct_vs_baseline: growthPct,
    cooldown_slope_mib_per_min: slope,
    restart_count_delta: (samples.at(-1)?.restart_count ?? 0) - (samples[0]?.restart_count ?? 0),
    sample_count: samples.length,
    verdict: leakSuspect ? 'LEAK_SUSPECT' : 'OK',
    leakSuspect,
  };
}

async function main() {
  const opts = parseArgs(process.argv);
  const samples = [];
  let sampling = true;
  let phase = 'baseline';
  const take = async () => {
    const s = await sampleContainer(opts.container);
    s.phase = phase;
    s.pipeline_sessions = await fetchPipelineSessionCount(opts.api);
    samples.push(s);
    console.log(`[mem ${phase}] ${s.at} rss=${s.mem?.usage_gib?.toFixed(2) ?? '?'} GiB sessions=${s.pipeline_sessions ?? '?'}`);
    return s;
  };
  console.log(`Memory profiler baseline=${opts.baselineSec}s sample=${opts.sampleSec}s cooldown=${opts.cooldownSec}s`);
  for (const end = Date.now() + opts.baselineSec * 1000; Date.now() < end; ) { await take(); await sleep(opts.sampleSec * 1000); }
  phase = 'test';
  const e2eArgs = [join(__dirname, 'cc-sankey-flow-e2e.mjs'), opts.api, '--iterations', String(opts.iterations)];
  if (opts.rampUp) e2eArgs.push('--ramp-up');
  e2eArgs.push(...opts.e2eExtra);
  const e2e = spawn(process.execPath, e2eArgs, { stdio: ['ignore', 'pipe', 'pipe'], cwd: ROOT });
  e2e.stdout.pipe(process.stdout);
  e2e.stderr.pipe(process.stderr);
  const sampler = (async () => { while (sampling) { await take(); await sleep(opts.sampleSec * 1000); } })();
  const e2eCode = await new Promise((r) => e2e.on('close', r));
  sampling = false;
  await sampler.catch(() => {});
  phase = 'cooldown';
  for (const end = Date.now() + opts.cooldownSec * 1000; Date.now() < end; ) { await take(); await sleep(opts.sampleSec * 1000); }
  const analysis = analyze(samples, opts);
  writeFileSync(join(__dirname, 'mem-profile-ramp-up-last.json'), `${JSON.stringify({ at: new Date().toISOString(), options: opts, e2e: { exitCode: e2eCode }, analysis, samples }, null, 2)}\n`);
  console.log('\n=== Memory profile ===');
  console.log(`Baseline avg : ${analysis.baseline_avg_gib.toFixed(2)} GiB`);
  console.log(`Peak         : ${analysis.peak_gib.toFixed(2)} GiB`);
  console.log(`After cooldown: ${analysis.cooldown_end_gib.toFixed(2)} GiB (+${analysis.growth_pct_vs_baseline.toFixed(1)}%)`);
  console.log(`Cooldown slope: ${analysis.cooldown_slope_mib_per_min.toFixed(1)} MiB/min`);
  console.log(`Restarts delta: ${analysis.restart_count_delta}`);
  console.log(`Verdict      : ${analysis.verdict}`);
  if (e2eCode !== 0) process.exit(1);
  if (analysis.leakSuspect) process.exit(2);
}

main().catch((e) => { console.error(e.message); process.exit(1); });
