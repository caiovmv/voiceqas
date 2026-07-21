#!/usr/bin/env node
/**
 * VoiceQAS native gRPC E2E — direct SpeechToTextService on :9051 (no HTTP / Command Center).
 */
import { spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const REPO = join(__dirname, '..');
const PROTO = join(REPO, 'proto');
const GRPC_ADDR = process.argv[2] || process.env.VOICEQAS_GRPC_ADDR || '127.0.0.1:9051';
const CC = (process.env.VOICEQAS_CC_URL || 'http://127.0.0.1:3000').replace(/\/$/, '');
const PROM = `${CC}/prometheus`;
const TEMPO = `${CC}/tempo`;
const AUDIO_SEC = Number(process.env.VOICEQAS_E2E_AUDIO_SEC || 2.0);
const GRPC_TIMEOUT_MS = Number(process.env.VOICEQAS_GRPC_TIMEOUT_MS || 180_000);
const MODELS = ['parakeet', 'whisper'];
const PROVIDERS = ['cpu', 'cuda'];
const BEYLA_RPC = 'sum(rate({__name__=~"rpc_server_duration_seconds_count|rpc_client_duration_seconds_count",job="beyla",service_name="voiceqas",rpc_method!="",rpc_method!="*",rpc_method!~".*TraceService.*|.*ServerReflection.*"}[5m]))';
const OTEL_GRPC = 'sum(rate(traces_spanmetrics_calls_total{service_name="voiceqas",span_name=~"gRPC .*"}[5m]))';
const results = [];
function log(status, name, detail) { results.push({ status, name, detail, at: new Date().toISOString() }); console.log(`${status.padEnd(4)} ${name} — ${detail}`); }
const pass = (n, d) => log('PASS', n, d);
const fail = (n, d) => log('FAIL', n, d);
const warn = (n, d) => log('WARN', n, d);
const skip = (n, d) => log('SKIP', n, d);
function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }
async function fetchJson(url, init = {}, timeoutMs = 30_000) {
  const ac = new AbortController();
  const t = setTimeout(() => ac.abort(), timeoutMs);
  try {
    const res = await fetch(url, { ...init, signal: ac.signal });
    const text = await res.text();
    let body; try { body = text ? JSON.parse(text) : null; } catch { body = text; }
    if (!res.ok) throw new Error(`${res.status} ${typeof body === 'string' ? body.slice(0, 240) : JSON.stringify(body).slice(0, 240)}`);
    return body;
  } finally { clearTimeout(t); }
}
async function promValue(expr) {
  const body = await fetchJson(`${PROM}/api/v1/query?query=${encodeURIComponent(expr)}`);
  const row = body.data?.result?.[0];
  if (!row) return null;
  return Number(row.value?.[1] ?? row.values?.at(-1)?.[1]);
}
function pcmS16le(seconds = AUDIO_SEC, rate = 16000, freq = 440) {
  const n = Math.floor(rate * seconds);
  const out = Buffer.alloc(n * 2);
  for (let i = 0; i < n; i++) {
    const s = Math.round(5000 * Math.sin((2 * Math.PI * freq * i) / rate)) + Math.round(2500 * Math.sin((2 * Math.PI * (freq * 1.5) * i) / rate));
    out.writeInt16LE(Math.max(-32767, Math.min(32767, s)), i * 2);
  }
  return out;
}
function grpcurlAvailable() {
  const r = spawnSync('grpcurl', ['-version'], { encoding: 'utf8', timeout: 5000 });
  return r.status === 0 || /grpcurl/i.test(r.stdout || r.stderr || '');
}
function dockerGrpcurlAvailable() {
  const r = spawnSync('docker', ['version', '--format', '{{.Server.Version}}'], { encoding: 'utf8', timeout: 8000 });
  return r.status === 0;
}
function dockerGrpcTarget(addr) {
  const port = addr.includes(':') ? addr.split(':').pop() : '9051';
  if (process.platform === 'win32' || process.platform === 'darwin') return `host.docker.internal:${port}`;
  const host = addr.includes(':') ? addr.slice(0, addr.lastIndexOf(':')) : '127.0.0.1';
  if (host === '127.0.0.1' || host === 'localhost') return `host.docker.internal:${port}`;
  return addr;
}
function runGrpcurl(method, jsonBody, metadata = {}) {
  const useLocal = grpcurlAvailable();
  const useDocker = !useLocal && dockerGrpcurlAvailable();
  if (!useLocal && !useDocker) throw new Error('grpcurl not found (install grpcurl or Docker)');
  const target = useLocal ? GRPC_ADDR : dockerGrpcTarget(GRPC_ADDR);
  const headerArgs = Object.entries(metadata).flatMap(([k, v]) => ['-H', `${k}: ${v}`]);
  let workDir = null;
  let dataArgs = [];
  let stdinPayload = null;
  if (jsonBody != null) {
    if (useLocal) {
      workDir = mkdtempSync(join(tmpdir(), 'vq-grpc-'));
      const dataFile = join(workDir, 'request.json');
      writeFileSync(dataFile, JSON.stringify(jsonBody));
      dataArgs = ['-d', `@${dataFile}`];
    } else {
      dataArgs = ['-d', '@'];
      stdinPayload = JSON.stringify(jsonBody);
    }
  }
  const baseArgs = ['-plaintext', '-import-path', PROTO, '-proto', 'stt.proto', '-proto', 'voice_quality.proto', ...headerArgs, ...dataArgs, target, method];
  let r;
  try {
    if (useLocal) {
      r = spawnSync('grpcurl', baseArgs, { encoding: 'utf8', timeout: GRPC_TIMEOUT_MS, maxBuffer: 16 * 1024 * 1024 });
    } else {
      const protoMount = process.platform === 'win32' ? `${PROTO.replace(/\\/g, '/')}:/proto:ro` : `${PROTO}:/proto:ro`;
      const dockerArgs = ['run', '-i', '--rm', '-v', protoMount, 'fullstorydev/grpcurl', '-plaintext', '-import-path', '/proto', '-proto', 'stt.proto', '-proto', 'voice_quality.proto', ...headerArgs, ...dataArgs, target, method];
      r = spawnSync('docker', dockerArgs, { encoding: 'utf8', input: stdinPayload || undefined, timeout: GRPC_TIMEOUT_MS + 15000, maxBuffer: 16 * 1024 * 1024 });
    }
  } finally {
    if (workDir) { try { rmSync(workDir, { recursive: true, force: true }); } catch {} }
  }
  const out = `${r.stdout || ''}${r.stderr || ''}`.trim();
  if (r.error) throw new Error(r.error.message);
  if (r.status !== 0) throw new Error(out.slice(0, 400) || `grpcurl exit ${r.status}`);
  try { return JSON.parse(r.stdout || '{}'); } catch { return { raw: r.stdout, stderr: r.stderr }; }
}
function assertTranscribeResponse(body, label) {
  if (!body || typeof body !== 'object') throw new Error(`${label}: empty response`);
  if (body.error) throw new Error(`${label}: ${body.error}`);
  if (typeof body.processing_ms !== 'number' && typeof body.processingMs !== 'number' && !body.model) throw new Error(`${label}: unexpected payload ${JSON.stringify(body).slice(0, 200)}`);
  return { model: body.model || '?', textLen: (body.text || '').trim().length, processingMs: body.processing_ms ?? body.processingMs ?? null, durationMs: body.duration_ms ?? body.durationMs ?? null };
}
async function grpcReady() {
  const body = runGrpcurl('voiceqas.v1.SpeechToTextService/Ready');
  if (!body.status) throw new Error(`Ready: ${JSON.stringify(body).slice(0, 200)}`);
  return body;
}
async function grpcTranscribe(model, provider, pcm) {
  const session = `e2e-native-grpc-${model}-${provider}-${Date.now()}`;
  const body = runGrpcurl('voiceqas.v1.SpeechToTextService/Transcribe', { payload: pcm.toString('base64'), format: 'PCM_S16LE_16K', sample_rate: 16000, language: 'pt', model }, { 'x-stt-provider': provider, 'x-session-id': session });
  return assertTranscribeResponse(body, 'gRPC native');
}
async function discoverCapabilities() {
  const ready = await fetchJson(`${CC}/api/v1/stt/ready`).catch(() => null);
  const modelsReady = new Set((ready?.models || []).filter((m) => m.ready && MODELS.includes(m.id)).map((m) => m.id));
  if (!modelsReady.size) for (const m of MODELS) modelsReady.add(m);
  const providersAvail = new Set(ready?.providers_available || ['cpu']);
  const cudaCompiled = Boolean(ready?.cuda_compiled);
  return { ready, modelsReady, providersAvail, cudaCompiled };
}
async function runMatrix(caps) {
  const pcm = pcmS16le();
  console.log(`\n=== Native gRPC matrix (${MODELS.length}×${PROVIDERS.length}) @ ${GRPC_ADDR} audio=${AUDIO_SEC}s ===\n`);
  for (const model of MODELS) {
    for (const provider of PROVIDERS) {
      const name = `gRPC native · ${model} · ${provider}`;
      if (!caps.modelsReady.has(model)) { skip(name, `model ${model} not ready`); continue; }
      if (!caps.providersAvail.has(provider)) { skip(name, `provider ${provider} unavailable`); continue; }
      if (provider === 'cuda' && !caps.cudaCompiled) { skip(name, 'cuda_compiled=false'); continue; }
      const t0 = Date.now();
      try {
        const out = await grpcTranscribe(model, provider, pcm);
        pass(name, `model=${out.model} processing_ms=${out.processingMs ?? '—'} textLen=${out.textLen} wall=${Date.now() - t0}ms`);
      } catch (e) { fail(name, e instanceof Error ? e.message : String(e)); }
    }
  }
}
async function runObservabilityChecks() {
  console.log('\n=== Observability (native gRPC traffic) ===\n');
  await sleep(10000);
  try {
    const beyla = await promValue(BEYLA_RPC);
    const otel = await promValue(OTEL_GRPC);
    if ((beyla != null && beyla > 0) && (otel != null && otel > 0)) pass('OBS RED gRPC Beyla+OTel', `beyla_rps=${beyla.toFixed(4)} otel_rps=${otel.toFixed(4)}`);
    else if ((beyla ?? 0) > 0 || (otel ?? 0) > 0) warn('OBS RED gRPC Beyla+OTel', `beyla=${beyla ?? 0} otel=${otel ?? 0} (partial)`);
    else fail('OBS RED gRPC Beyla+OTel', 'sem métricas após tráfego nativo');
  } catch (e) { fail('OBS RED gRPC Beyla+OTel', e.message); }
  try {
    const byMethod = await fetchJson(`${PROM}/api/v1/query?query=${encodeURIComponent('sum by (rpc_method) (rate({__name__=~"rpc_server_duration_seconds_count|rpc_client_duration_seconds_count",job="beyla",service_name="voiceqas",rpc_method=~".*Transcribe.*"}[5m]))')}`);
    const rows = byMethod.data?.result ?? [];
    if (rows.length > 0) pass('OBS Beyla by method', rows.map((r) => `${r.metric.rpc_method}=${Number(r.value[1]).toFixed(4)}`).join('; '));
    else warn('OBS Beyla by method', 'sem série Transcribe');
  } catch (e) { warn('OBS Beyla by method', e.message); }
  try {
    const end = Math.floor(Date.now() / 1000);
    const start = end - 3600;
    const search = await fetchJson(`${TEMPO}/api/search?start=${start}&end=${end}&limit=5&q=${encodeURIComponent('{ name="gRPC SpeechToTextService/Transcribe" }')}`);
    const n = search.traces?.length ?? 0;
    if (n > 0) pass('OBS APM gRPC Transcribe', `traces=${n} sample=${search.traces[0].rootTraceName} ${search.traces[0].durationMs}ms`);
    else fail('OBS APM gRPC Transcribe', 'TraceQL 0 traces');
  } catch (e) { fail('OBS APM gRPC Transcribe', e.message); }
}
async function main() {
  const mode = grpcurlAvailable() ? 'grpcurl local' : dockerGrpcurlAvailable() ? 'docker grpcurl' : 'none';
  console.log(`Native gRPC E2E @ ${GRPC_ADDR} (${mode})`);
  console.log(`Prom/Tempo via ${CC}\n`);
  if (mode === 'none') { fail('Tooling', 'instale grpcurl ou Docker'); finish(1); return; }
  try { const ready = await grpcReady(); pass('gRPC Ready', `status=${ready.status} service=${ready.service} model=${ready.model || '—'}`); }
  catch (e) { fail('gRPC Ready', e.message); finish(1); return; }
  let caps;
  try {
    caps = await discoverCapabilities();
    pass('Capabilities', `models=[${[...caps.modelsReady]}] providers=[${[...caps.providersAvail]}] cuda_compiled=${caps.cudaCompiled}`);
  } catch (e) {
    warn('Capabilities', `STT ready via CC falhou (${e.message}); prosseguindo com defaults`);
    caps = { modelsReady: new Set(MODELS), providersAvail: new Set(['cpu']), cudaCompiled: false };
  }
  await runMatrix(caps);
  await runObservabilityChecks();
  finish();
}
function finish(exitCode) {
  const passN = results.filter((r) => r.status === 'PASS').length;
  const failN = results.filter((r) => r.status === 'FAIL').length;
  const warnN = results.filter((r) => r.status === 'WARN').length;
  const skipN = results.filter((r) => r.status === 'SKIP').length;
  console.log('\n=== SUMMARY ===');
  console.log(`PASS=${passN} FAIL=${failN} WARN=${warnN} SKIP=${skipN} TOTAL=${results.length}`);
  try {
    writeFileSync(join(__dirname, 'cc-grpc-native-e2e-last.json'), JSON.stringify({ at: new Date().toISOString(), grpcAddr: GRPC_ADDR, cc: CC, audioSec: AUDIO_SEC, results }, null, 2));
    console.log('\nReport: scripts/cc-grpc-native-e2e-last.json');
  } catch {}
  process.exit(exitCode ?? (failN > 0 ? 1 : 0));
}
main().catch((e) => { console.error(e); process.exit(1); });
