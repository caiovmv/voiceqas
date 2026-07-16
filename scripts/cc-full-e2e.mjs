#!/usr/bin/env node
/**
 * VoiceQAS full E2E — transport × model × provider matrix + observability KPIs.
 *
 * Covers:
 *   REST  /v1/stt/transcribe
 *   WS    /ws/v1/stt/stream
 *   gRPC  /v1/playground/grpc/stt-transcribe (HTTP bridge → in-process gRPC client → :50051)
 *   models: parakeet, whisper
 *   providers: cpu, cuda
 *
 * Usage:
 *   node scripts/cc-full-e2e.mjs
 *   node scripts/cc-full-e2e.mjs http://127.0.0.1:3000
 *   VOICEQAS_E2E_AUDIO_SEC=2.5 node scripts/cc-full-e2e.mjs
 *
 * Exit: 0 if no FAIL (SKIP/WARN allowed); 1 if any FAIL.
 */
import { writeFileSync } from 'node:fs';

const CC = (process.argv[2] || 'http://127.0.0.1:3000').replace(/\/$/, '');
const API = `${CC}/api`;
const WS_BASE = CC.replace(/^http/, 'ws') + '/ws';
const PROM = `${CC}/prometheus`;
const TEMPO = `${CC}/tempo`;
const TOKEN = process.env.VOICEQAS_OPS_TOKEN || 'dev-read';
const WRITE = process.env.VOICEQAS_OPS_WRITE_TOKEN || 'dev-write';
const AUDIO_SEC = Number(process.env.VOICEQAS_E2E_AUDIO_SEC || 2.0);
const WS_TIMEOUT_MS = Number(process.env.VOICEQAS_E2E_WS_TIMEOUT_MS || 120_000);
const REST_TIMEOUT_MS = Number(process.env.VOICEQAS_E2E_REST_TIMEOUT_MS || 180_000);

const MODELS = ['parakeet', 'whisper'];
const PROVIDERS = ['cpu', 'cuda'];
const TRANSPORTS = ['rest', 'ws', 'grpc-http'];

const results = [];

function log(status, name, detail) {
  results.push({ status, name, detail, at: new Date().toISOString() });
  console.log(`${status.padEnd(4)} ${name} — ${detail}`);
}
const pass = (n, d) => log('PASS', n, d);
const fail = (n, d) => log('FAIL', n, d);
const warn = (n, d) => log('WARN', n, d);
const skip = (n, d) => log('SKIP', n, d);

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function fetchJson(url, init = {}, timeoutMs = 30_000) {
  const ac = new AbortController();
  const t = setTimeout(() => ac.abort(), timeoutMs);
  try {
    const res = await fetch(url, { ...init, signal: ac.signal });
    const text = await res.text();
    let body;
    try {
      body = text ? JSON.parse(text) : null;
    } catch {
      body = text;
    }
    if (!res.ok) {
      const err = new Error(
        `${res.status} ${typeof body === 'string' ? body.slice(0, 240) : JSON.stringify(body).slice(0, 240)}`,
      );
      err.status = res.status;
      err.body = body;
      throw err;
    }
    return body;
  } finally {
    clearTimeout(t);
  }
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
    // dual tone avoids total silence VAD drop
    const s =
      Math.round(5000 * Math.sin((2 * Math.PI * freq * i) / rate)) +
      Math.round(2500 * Math.sin((2 * Math.PI * (freq * 1.5) * i) / rate));
    out.writeInt16LE(Math.max(-32767, Math.min(32767, s)), i * 2);
  }
  return out;
}

function wavFromPcm(pcm, rate = 16000) {
  const header = Buffer.alloc(44);
  header.write('RIFF', 0);
  header.writeUInt32LE(36 + pcm.length, 4);
  header.write('WAVE', 8);
  header.write('fmt ', 12);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(1, 22);
  header.writeUInt32LE(rate, 24);
  header.writeUInt32LE(rate * 2, 28);
  header.writeUInt16LE(2, 32);
  header.writeUInt16LE(16, 34);
  header.write('data', 36);
  header.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([header, pcm]);
}

function assertSttResult(body, label) {
  if (!body || typeof body !== 'object') throw new Error(`${label}: empty body`);
  if (body.ok === false) throw new Error(`${label}: ok=false error=${body.error || '?'}`);
  if (body.error && body.ok !== true) throw new Error(`${label}: ${body.error}`);
  // sine wave may yield empty text — require processing happened
  if (typeof body.processing_ms !== 'number' && body.ok !== true && !body.text && !body.model) {
    throw new Error(`${label}: unexpected payload ${JSON.stringify(body).slice(0, 180)}`);
  }
  return {
    ok: body.ok !== false,
    model: body.model || '?',
    textLen: (body.text || '').trim().length,
    processingMs: body.processing_ms ?? null,
    durationMs: body.duration_ms ?? null,
  };
}

async function sttRest(model, provider, pcm) {
  const wav = wavFromPcm(pcm);
  const session = `e2e-rest-${model}-${provider}-${Date.now()}`;
  const body = await fetchJson(
    `${API}/v1/stt/transcribe?model=${encodeURIComponent(model)}`,
    {
      method: 'POST',
      headers: {
        'Content-Type': 'audio/wav',
        'X-STT-Model': model,
        'X-STT-Provider': provider,
        'X-Language': 'pt',
        'X-Session-Id': session,
        'X-Ops-Token': WRITE,
      },
      body: wav,
    },
    REST_TIMEOUT_MS,
  );
  return assertSttResult(body, 'REST');
}

async function sttWs(model, provider, pcm) {
  const session = `e2e-ws-${model}-${provider}-${Date.now()}`;
  const url = `${WS_BASE}/v1/stt/stream`;
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    let phase = 'handshake';
    const timer = setTimeout(() => {
      try {
        ws.close();
      } catch {}
      reject(new Error(`WS timeout ${WS_TIMEOUT_MS}ms`));
    }, WS_TIMEOUT_MS);

    const done = (err, val) => {
      clearTimeout(timer);
      try {
        ws.close();
      } catch {}
      if (err) reject(err);
      else resolve(val);
    };

    ws.addEventListener('error', () => done(new Error('WS error')));
    ws.addEventListener('open', () => {
      ws.send(
        JSON.stringify({
          session_id: session,
          format: 'pcm_s16le_16k',
          sample_rate: 16000,
          model,
          provider,
          language: 'pt',
        }),
      );
    });
    ws.addEventListener('message', async (ev) => {
      const raw =
        typeof ev.data === 'string'
          ? ev.data
          : Buffer.from(await ev.data.arrayBuffer()).toString();
      let msg;
      try {
        msg = JSON.parse(raw);
      } catch {
        return;
      }
      if (phase === 'handshake') {
        if (msg.status !== 'ok' && msg.error) {
          done(new Error(`handshake: ${raw.slice(0, 200)}`));
          return;
        }
        if (msg.status && msg.status !== 'ok') {
          done(new Error(`handshake status=${msg.status}`));
          return;
        }
        phase = 'audio';
        const frame = 640;
        (async () => {
          for (let off = 0; off < pcm.length; off += frame) {
            if (ws.readyState !== WebSocket.OPEN) return;
            ws.send(pcm.subarray(off, off + frame));
            await sleep(10);
          }
          phase = 'flush';
          ws.send(JSON.stringify({ type: 'flush', model, provider, language: 'pt' }));
        })().catch((e) => done(e));
        return;
      }
      if (msg.type === 'error' || msg.error) {
        done(new Error(String(msg.error || raw.slice(0, 200))));
        return;
      }
      if (
        msg.type === 'stt_final' ||
        msg.type === 'final' ||
        msg.ok === true ||
        (msg.text !== undefined && msg.processing_ms !== undefined)
      ) {
        try {
          done(null, assertSttResult(msg, 'WS'));
        } catch (e) {
          done(e);
        }
      }
    });
  });
}

async function sttGrpcHttp(model, provider, pcm) {
  const session = `e2e-grpc-${model}-${provider}-${Date.now()}`;
  const pcm_bytes = [...pcm];
  const body = await fetchJson(
    `${API}/v1/playground/grpc/stt-transcribe`,
    {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'X-Ops-Token': WRITE,
      },
      body: JSON.stringify({
        format: 2, // pcm_s16le_16k (FORMAT_ENUM)
        sample_rate: 16000,
        pcm_bytes,
        model,
        provider,
        language: 'pt',
        session_id: session,
      }),
    },
    REST_TIMEOUT_MS,
  );
  return assertSttResult(body, 'gRPC-HTTP');
}

async function discoverCapabilities() {
  const ready = await fetchJson(`${API}/v1/stt/ready`);
  const health = await fetchJson(`${API}/health`);
  let grpcReady = null;
  try {
    grpcReady = await fetchJson(`${API}/v1/playground/grpc/ready`, { method: 'POST' });
  } catch (e) {
    grpcReady = { error: e.message };
  }

  const modelsReady = new Set(
    (ready.models || []).filter((m) => m.ready && ['parakeet', 'whisper'].includes(m.id)).map((m) => m.id),
  );
  const providersAvail = new Set(ready.providers_available || ['cpu']);
  const cudaCompiled = Boolean(ready.cuda_compiled);

  return { ready, health, grpcReady, modelsReady, providersAvail, cudaCompiled };
}

async function runMatrix(caps) {
  const pcm = pcmS16le();
  console.log(`\n=== STT matrix (${TRANSPORTS.length}×${MODELS.length}×${PROVIDERS.length}) audio=${AUDIO_SEC}s ===\n`);

  for (const transport of TRANSPORTS) {
    for (const model of MODELS) {
      for (const provider of PROVIDERS) {
        const name = `STT ${transport.toUpperCase()} · ${model} · ${provider}`;

        if (!caps.modelsReady.has(model)) {
          skip(name, `model ${model} not ready`);
          continue;
        }
        if (!caps.providersAvail.has(provider)) {
          skip(name, `provider ${provider} not in providers_available`);
          continue;
        }
        if (provider === 'cuda' && !caps.cudaCompiled) {
          skip(name, 'cuda_compiled=false');
          continue;
        }
        if (transport === 'grpc-http' && caps.grpcReady?.error) {
          skip(name, `gRPC HTTP playground unavailable: ${caps.grpcReady.error}`);
          continue;
        }

        const t0 = Date.now();
        try {
          let out;
          if (transport === 'rest') out = await sttRest(model, provider, pcm);
          else if (transport === 'ws') out = await sttWs(model, provider, pcm);
          else out = await sttGrpcHttp(model, provider, pcm);

          const elapsed = Date.now() - t0;
          pass(
            name,
            `model=${out.model} processing_ms=${out.processingMs ?? '—'} textLen=${out.textLen} wall=${elapsed}ms`,
          );
        } catch (e) {
          const msg = e instanceof Error ? e.message : String(e);
          // CUDA runtime failures → FAIL (capability claimed available)
          fail(name, msg);
        }
      }
    }
  }
}

async function runObservabilityChecks() {
  console.log('\n=== Observability KPIs (after matrix traffic) ===\n');
  await sleep(8000);

  // 1 pipeline
  try {
    const snap = await fetchJson(`${API}/v1/ops/pipeline/snapshot`, {
      headers: { 'X-Ops-Token': TOKEN },
    });
    const links = snap.links?.length ?? 0;
    const echarts = snap.echarts?.series?.[0]?.links?.length ?? 0;
    if (snap.status === 'ok' && (links > 0 || echarts > 0)) {
      pass('OBS Pipeline Sankey', `nodes=${snap.nodes?.length} links=${links} echarts.links=${echarts}`);
    } else warn('OBS Pipeline Sankey', `snapshot ok but links empty (nodes=${snap.nodes?.length})`);
  } catch (e) {
    fail('OBS Pipeline Sankey', e.message);
  }

  // 2 REST RED
  try {
    const beylaUp = await promValue('up{job="beyla"}');
    const rpsBeyla = await promValue(
      'sum(rate(http_server_request_duration_seconds_count{service_name="voiceqas",server_port="8080",http_route!~"/health|/ready|/metrics"}[15m]))',
    );
    const rpsOtel = await promValue(
      'sum(rate(traces_spanmetrics_calls_total{service_name="voiceqas",span_name!~".*/(health|ready|metrics)|GET /health|GET /metrics|GET /ready",span_name=~"^(GET|POST|PUT|DELETE|PATCH) /v1/.*"}[15m]))',
    );
    if (rpsBeyla != null && rpsBeyla > 0) {
      pass('OBS RED REST', `rps=${rpsBeyla.toFixed(3)} (Beyla)`);
    } else if (rpsOtel != null && rpsOtel > 0) {
      pass('OBS RED REST', `OTel rps=${rpsOtel.toFixed(3)}; Beyla=${rpsBeyla ?? 0}`);
    } else if ((beylaUp ?? 0) < 1) {
      fail('OBS RED REST', 'Beyla down — scripts/redeploy-voiceqas.ps1');
    } else {
      fail('OBS RED REST', `rps_beyla=${rpsBeyla} otel=${rpsOtel}`);
    }
  } catch (e) {
    fail('OBS RED REST', e.message);
  }

  // 3 WS OTel spanmetrics
  try {
    const ws = await promValue(
      'sum(rate(traces_spanmetrics_calls_total{service_name="voiceqas",span_name=~"WS .*"}[15m]))',
    );
    if (ws != null && ws > 0) pass('OBS RED/APM WebSocket', `spanmetrics WS rps=${ws.toFixed(3)}`);
    else warn('OBS RED/APM WebSocket', `WS spanmetrics empty (ws=${ws})`);
  } catch (e) {
    fail('OBS RED/APM WebSocket', e.message);
  }

  // 4 gRPC OTel / Beyla
  try {
    const grpcOtel = await promValue(
      'sum(rate(traces_spanmetrics_calls_total{service_name="voiceqas",span_name=~"gRPC .*"}[15m]))',
    );
    const grpcBeyla = await promValue(
      'sum(rate({__name__=~"rpc_server_duration_seconds_count|rpc_client_duration_seconds_count",job="beyla",service_name="voiceqas",rpc_method!="",rpc_method!="*",rpc_method!~".*TraceService.*|.*ServerReflection.*"}[15m]))',
    );
    if ((grpcOtel != null && grpcOtel > 0) || (grpcBeyla != null && grpcBeyla > 0)) {
      pass('OBS RED/APM gRPC', `otel=${grpcOtel ?? 0} beyla=${grpcBeyla ?? 0}`);
    } else {
      warn(
        'OBS RED/APM gRPC',
        'sem métricas gRPC após matrix — rode scripts/cc-grpc-native-e2e.mjs para tráfego direto :9051',
      );
    }
  } catch (e) {
    fail('OBS RED/APM gRPC', e.message);
  }

  // 5 Tempo TraceQL rich traces per protocol
  const end = Math.floor(Date.now() / 1000);
  const start = end - 3600;
  for (const [label, tq] of [
    ['OBS APM REST STT', '{ name="POST /v1/stt/transcribe" }'],
    ['OBS APM WebSocket', '{ name="WS /v1/stt/stream" }'],
    ['OBS APM pipeline spans', '{ name=~"pipeline/.*" }'],
  ]) {
    try {
      const search = await fetchJson(
        `${TEMPO}/api/search?start=${start}&end=${end}&limit=5&q=${encodeURIComponent(tq)}`,
      );
      const n = search.traces?.length ?? 0;
      if (n > 0) {
        const tid = search.traces[0].traceID;
        const detail = await fetchJson(`${TEMPO}/api/traces/${tid}`);
        let spans = 0;
        let pipeline = 0;
        for (const b of detail.batches ?? []) {
          for (const s of b.scopeSpans ?? []) {
            for (const sp of s.spans ?? []) {
              spans++;
              if (String(sp.name || '').startsWith('pipeline/')) pipeline++;
            }
          }
        }
        pass(label, `traces=${n} sample spans=${spans} pipeline_child=${pipeline}`);
      } else fail(label, 'TraceQL returned 0 traces');
    } catch (e) {
      fail(label, e.message);
    }
  }

  // ASR history
  try {
    const hist = await fetchJson(`${API}/v1/ops/metrics/history?limit=100`, {
      headers: { 'X-Ops-Token': TOKEN },
    });
    const finals = (hist.events || []).filter((e) => e.type === 'stt_final');
    if (finals.length > 0) pass('OBS ASR history', `stt_final=${finals.length}`);
    else warn('OBS ASR history', 'sem stt_final no JSONL');
  } catch (e) {
    fail('OBS ASR history', e.message);
  }
}

async function main() {
  console.log(`Full E2E @ ${CC}`);
  console.log(`models=${MODELS.join(',')} providers=${PROVIDERS.join(',')} transports=${TRANSPORTS.join(',')}\n`);

  let caps;
  try {
    caps = await discoverCapabilities();
    pass(
      'Capabilities',
      `health=${caps.health?.status} stt=${caps.ready?.status} models=[${[...caps.modelsReady]}] providers=[${[...caps.providersAvail]}] cuda_compiled=${caps.cudaCompiled} grpc=${caps.grpcReady?.error ? 'err' : 'ok'}`,
    );
  } catch (e) {
    fail('Capabilities', e.message);
    finish();
    return;
  }

  await runMatrix(caps);
  await runObservabilityChecks();
  finish();
}

function finish() {
  const passN = results.filter((r) => r.status === 'PASS').length;
  const failN = results.filter((r) => r.status === 'FAIL').length;
  const warnN = results.filter((r) => r.status === 'WARN').length;
  const skipN = results.filter((r) => r.status === 'SKIP').length;

  console.log('\n=== SUMMARY ===');
  console.log(`PASS=${passN} FAIL=${failN} WARN=${warnN} SKIP=${skipN} TOTAL=${results.length}`);

  const matrix = results.filter((r) => r.name.startsWith('STT '));
  console.log('\nMatrix:');
  for (const r of matrix) {
    console.log(`  ${r.status.padEnd(4)} ${r.name}`);
  }

  console.log('\nNotes:');
  console.log('- SKIP = capability missing (não conta como falha)');
  console.log('- Texto vazio em seno sintético é OK se ok/processing_ms presentes');
  console.log('- gRPC-HTTP = playground REST → cliente gRPC interno (:50051)');
  console.log('- gRPC nativo direto: scripts/cc-grpc-native-e2e.mjs (:9051)');
  console.log('- CUDA: se providers_available inclui cuda mas load falhar → FAIL');

  const report = {
    at: new Date().toISOString(),
    cc: CC,
    audioSec: AUDIO_SEC,
    results,
  };
  try {
    writeFileSync('scripts/cc-full-e2e-last.json', JSON.stringify(report, null, 2));
    console.log('\nReport: scripts/cc-full-e2e-last.json');
  } catch {
    /* ignore */
  }

  process.exit(failN > 0 ? 1 : 0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
