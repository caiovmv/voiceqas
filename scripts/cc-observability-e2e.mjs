#!/usr/bin/env node
/**
 * Command Center E2E — 10 checks over KPIs / charts / APM.
 *
 * Usage:
 *   node scripts/cc-observability-e2e.mjs
 *   node scripts/cc-observability-e2e.mjs http://127.0.0.1:3000
 *
 * Exit 0 if all critical pass; exit 1 if any FAIL (WARN does not fail).
 */
import { writeFileSync } from 'node:fs';

const CC = (process.argv[2] || 'http://127.0.0.1:3000').replace(/\/$/, '');
const API = `${CC}/api`;
const PROM = `${CC}/prometheus`;
const TEMPO = `${CC}/tempo`;
const TOKEN = process.env.VOICEQAS_OPS_TOKEN || 'dev-read';
const WRITE = process.env.VOICEQAS_OPS_WRITE_TOKEN || 'dev-write';

const results = [];

function ok(name, detail) {
  results.push({ name, status: 'PASS', detail });
  console.log(`PASS  ${name} — ${detail}`);
}
function warn(name, detail) {
  results.push({ name, status: 'WARN', detail });
  console.log(`WARN  ${name} — ${detail}`);
}
function fail(name, detail) {
  results.push({ name, status: 'FAIL', detail });
  console.log(`FAIL  ${name} — ${detail}`);
}

async function getJson(url, headers = {}) {
  const res = await fetch(url, { headers });
  const text = await res.text();
  let body;
  try {
    body = text ? JSON.parse(text) : null;
  } catch {
    body = text;
  }
  if (!res.ok) {
    const err = new Error(`${res.status} ${typeof body === 'string' ? body.slice(0, 200) : JSON.stringify(body).slice(0, 200)}`);
    err.status = res.status;
    err.body = body;
    throw err;
  }
  return body;
}

async function promInstant(expr) {
  const url = `${PROM}/api/v1/query?query=${encodeURIComponent(expr)}`;
  const body = await getJson(url);
  if (body.status !== 'success') throw new Error(body.error || 'prom query failed');
  return body.data?.result ?? [];
}

function promValue(result) {
  if (!result?.length) return null;
  const row = result[0];
  if (row.value) return Number(row.value[1]);
  if (row.values?.length) return Number(row.values.at(-1)[1]);
  return null;
}

function pcmS16(seconds = 1.5, rate = 16000, freq = 440) {
  const n = Math.floor(rate * seconds);
  const out = Buffer.alloc(n * 2);
  for (let i = 0; i < n; i++) {
    const s = Math.round(7000 * Math.sin((2 * Math.PI * freq * i) / rate));
    out.writeInt16LE(s, i * 2);
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

async function generateTraffic() {
  const h = { 'X-Ops-Token': TOKEN };
  for (let i = 0; i < 6; i++) {
    await Promise.all([
      fetch(`${API}/health`),
      fetch(`${API}/v1/stt/ready`),
      fetch(`${API}/v1/ops/pipeline/snapshot`, { headers: h }),
      fetch(`${API}/v1/ops/pipeline/sessions`, { headers: h }),
      fetch(`${API}/v1/media/sessions`, { headers: h }),
    ]);
  }

  // REST STT (OTel RequestScope + StageSpan asr/vad when session present)
  const wav = wavFromPcm(pcmS16(1.2));
  const sttRes = await fetch(`${API}/v1/stt/transcribe?model=parakeet`, {
    method: 'POST',
    headers: {
      'Content-Type': 'audio/wav',
      'X-Session-Id': `e2e-cc-${Date.now()}`,
      'X-Ops-Token': WRITE,
    },
    body: wav,
  });
  const sttText = await sttRes.text();
  let sttJson = null;
  try {
    sttJson = JSON.parse(sttText);
  } catch {
    /* ignore */
  }

  // WS STT stream (names "WS /v1/stt/stream")
  let wsOk = false;
  let wsError = null;
  try {
    const wsUrl = CC.replace(/^http/, 'ws') + '/ws/v1/stt/stream';
    wsOk = await new Promise((resolve, reject) => {
      const ws = new WebSocket(wsUrl);
      const t = setTimeout(() => {
        try {
          ws.close();
        } catch {}
        reject(new Error('ws timeout'));
      }, 25_000);
      let phase = 'handshake';
      ws.addEventListener('open', () => {
        ws.send(
          JSON.stringify({
            session_id: `e2e-ws-${Date.now()}`,
            format: 'pcm_s16le_16k',
            sample_rate: 16000,
            model: 'parakeet',
          }),
        );
      });
      ws.addEventListener('message', async (ev) => {
        const raw = typeof ev.data === 'string' ? ev.data : Buffer.from(await ev.data.arrayBuffer()).toString();
        let msg;
        try {
          msg = JSON.parse(raw);
        } catch {
          return;
        }
        if (phase === 'handshake') {
          if (msg.status !== 'ok') {
            clearTimeout(t);
            reject(new Error(`ws handshake ${raw.slice(0, 120)}`));
            return;
          }
          phase = 'audio';
          const pcm = pcmS16(1.0);
          const frame = 640;
          (async () => {
            for (let off = 0; off < pcm.length; off += frame) {
              ws.send(pcm.subarray(off, off + frame));
              await new Promise((r) => setTimeout(r, 15));
            }
            phase = 'flush';
            ws.send(JSON.stringify({ type: 'flush', model: 'parakeet' }));
          })();
          return;
        }
        if (msg.type === 'stt_final' || msg.text !== undefined || msg.ok !== undefined) {
          clearTimeout(t);
          ws.close();
          resolve(true);
        }
      });
      ws.addEventListener('error', () => {
        clearTimeout(t);
        reject(new Error('ws error'));
      });
    });
  } catch (e) {
    wsError = e instanceof Error ? e.message : String(e);
  }

  return {
    sttStatus: sttRes.status,
    sttOk: Boolean(sttJson?.ok ?? sttRes.ok),
    sttProcessingMs: sttJson?.processing_ms,
    wsOk,
    wsError,
  };
}

function coreTraceFilter(name) {
  const n = name || '';
  if (/GET \/(health|ready|metrics)$/i.test(n)) return false;
  if (/\/(health|ready|metrics)$/i.test(n) && !/stt\/ready/i.test(n)) return false;
  return true;
}

async function main() {
  console.log(`Command Center E2E @ ${CC}\n`);

  // --- Traffic ---
  console.log('Generating REST + STT (+ WS) traffic…');
  let traffic;
  try {
    traffic = await generateTraffic();
    console.log(
      `  STT REST ${traffic.sttStatus} ok=${traffic.sttOk} processing_ms=${traffic.sttProcessingMs ?? '—'}; WS=${traffic.wsOk}${traffic.wsError ? ` (${traffic.wsError})` : ''}`,
    );
  } catch (e) {
    traffic = { error: e instanceof Error ? e.message : String(e) };
    console.log(`  traffic error: ${traffic.error}`);
  }
  console.log('Waiting 8s for Prometheus/Tempo ingest…\n');
  await new Promise((r) => setTimeout(r, 8000));

  // 1) Health / up
  try {
    const health = await getJson(`${API}/health`);
    const up = await promInstant('voiceqas_up');
    const v = promValue(up);
    if (health?.status === 'ok' && v === 1) ok('1. Health / voiceqas_up', 'REST ok + gauge=1');
    else fail('1. Health / voiceqas_up', `health=${JSON.stringify(health)} up=${v}`);
  } catch (e) {
    fail('1. Health / voiceqas_up', String(e.message || e));
  }

  // 2) Auth + pipeline snapshot (Sankey source)
  try {
    const snap = await getJson(`${API}/v1/ops/pipeline/snapshot`, { 'X-Ops-Token': TOKEN });
    const links = snap.links?.length ?? 0;
    const nodes = snap.nodes?.length ?? 0;
    const echartsLinks = snap.echarts?.series?.[0]?.links?.length ?? 0;
    if (snap.status === 'ok' && nodes > 0) {
      if (links > 0 || echartsLinks > 0) {
        ok('2. Audio Pipeline Sankey data', `nodes=${nodes} links=${links} echarts.links=${echartsLinks} scope=${snap.scope}`);
      } else {
        warn(
          '2. Audio Pipeline Sankey data',
          `nodes=${nodes} but links=0 — gráfico aparece vazio até haver bytes fluindo (precisa RTP/STT/media session ativa). Snapshot echarts presente=${Boolean(snap.echarts)}`,
        );
      }
    } else fail('2. Audio Pipeline Sankey data', JSON.stringify(snap).slice(0, 200));
  } catch (e) {
    fail('2. Audio Pipeline Sankey data', `${e.message} — UI fica vazia se Auth ops sem token (dev-read/dev-write)`);
  }

  // 3) ASR KPIs (engine ready + SNR gauge)
  try {
    const ready = await getJson(`${API}/v1/stt/ready`);
    const snr = promValue(await promInstant('voiceqas_vqa_snr_db'));
    const engine = promValue(await promInstant('voiceqas_stt_ready'));
    const readySessions = promValue(await promInstant('voiceqas_vqa_stt_ready_sessions'));
    if (ready?.status === 'ready' && engine === 1) {
      ok(
        '3. ASR / STT engine KPIs',
        `engine ready; SNR=${snr ?? '—'} dB; ASR-ready sessions=${readySessions ?? 0} (0 sem janelas VQA ao vivo)`,
      );
    } else fail('3. ASR / STT engine KPIs', `ready=${ready?.status} engine=${engine}`);
  } catch (e) {
    fail('3. ASR / STT engine KPIs', String(e.message || e));
  }

  // 4) ASR success/coverage from ops history (need traffic)
  try {
    const hist = await getJson(`${API}/v1/ops/metrics/history?limit=100`, { 'X-Ops-Token': TOKEN });
    const events = hist.events ?? [];
    const finals = events.filter((e) => e.type === 'stt_final');
    const withText = finals.filter((e) => (e.text || '').trim().length > 0);
    if (finals.length > 0) {
      ok('4. ASR success/coverage (history)', `stt_final=${finals.length} withText=${withText.length} success≈${((withText.length / finals.length) * 100).toFixed(0)}%`);
    } else if (traffic?.sttOk) {
      warn(
        '4. ASR success/coverage (history)',
        'STT REST ok mas history sem stt_final — publish_stt_result pode não persistir batch REST no event store; painel Coverage/Success fica 0 até WS/ops stream',
      );
    } else {
      warn('4. ASR success/coverage (history)', `sem stt_final no JSONL (n=${events.length}). Gere tráfego no Tester STT WS`);
    }
  } catch (e) {
    fail('4. ASR success/coverage (history)', String(e.message || e));
  }

  // 5) RED REST RPS + P95
  try {
    const beylaUp = promValue(await promInstant('up{job="beyla"}'));
    const rpsBeyla = promValue(
      await promInstant(
        'sum(rate(http_server_request_duration_seconds_count{service_name="voiceqas",server_port="8080",http_route!~"/health|/ready|/metrics"}[5m]))',
      ),
    );
    const p95Beyla = promValue(
      await promInstant(
        'histogram_quantile(0.95, sum(rate(http_server_request_duration_seconds_bucket{service_name="voiceqas",server_port="8080",http_route!~"/health|/ready|/metrics",http_response_status_code!~"101"}[5m])) by (le))',
      ),
    );
    const rpsOtel = promValue(
      await promInstant(
        'sum(rate(traces_spanmetrics_calls_total{service_name="voiceqas",span_name!~".*/(health|ready|metrics)|GET /health|GET /metrics|GET /ready",span_name=~"^(GET|POST|PUT|DELETE|PATCH) /v1/.*"}[15m]))',
      ),
    );
    if (rpsBeyla != null && rpsBeyla > 0 && p95Beyla != null && Number.isFinite(p95Beyla)) {
      ok('5. RED REST (RPS + P95)', `rps=${rpsBeyla.toFixed(3)} p95=${(p95Beyla * 1000).toFixed(1)}ms (Beyla :8080)`);
    } else if (rpsOtel != null && rpsOtel > 0) {
      ok(
        '5. RED REST (RPS + P95)',
        `OTel spanmetrics rps=${rpsOtel.toFixed(3)}; Beyla rps=${rpsBeyla ?? 0} (up=${beylaUp ?? 0})`,
      );
    } else if ((beylaUp ?? 0) < 1) {
      fail(
        '5. RED REST (RPS + P95)',
        'Beyla down — rode: docker compose up -d --force-recreate beyla (ou scripts/redeploy-voiceqas.ps1)',
      );
    } else {
      fail('5. RED REST (RPS + P95)', `rps_beyla=${rpsBeyla} p95=${p95Beyla} otel=${rpsOtel}`);
    }
  } catch (e) {
    fail('5. RED REST (RPS + P95)', String(e.message || e));
  }

  // 6) RED WebSocket
  try {
    const wsBeyla = promValue(
      await promInstant(
        'sum(rate(http_server_request_duration_seconds_count{service_name="voiceqas",server_port="8081",http_route!~"/health|/ready|/metrics"}[5m]))',
      ),
    );
    const wsOtel = promValue(
      await promInstant(
        'sum(rate(traces_spanmetrics_calls_total{service_name="voiceqas",span_name=~"WS .*"}[15m]))',
      ),
    );
    if (wsOtel != null && wsOtel > 0) {
      ok(
        '6. RED WebSocket',
        `OTel spanmetrics WS rps=${wsOtel.toFixed(3)}; Beyla:8081=${wsBeyla ?? 0} (upgrade 101 — pode ficar 0 se Beyla não etiquetar :8081)`,
      );
    } else if (wsBeyla != null && wsBeyla > 0) {
      ok('6. RED WebSocket', `Beyla:8081 rps=${wsBeyla}; OTel WS spanmetrics ainda 0`);
    } else {
      warn(
        '6. RED WebSocket',
        `Beyla:8081 e spanmetrics "WS *" vazios. WS e2e=${traffic?.wsOk} err=${traffic?.wsError || '—'} — rode Tester STT WS`,
      );
    }
  } catch (e) {
    fail('6. RED WebSocket', String(e.message || e));
  }

  // 7) RED gRPC
  try {
    const grpcBeyla = promValue(
      await promInstant(
        'sum(rate({__name__=~"rpc_server_duration_seconds_count|rpc_client_duration_seconds_count",job="beyla",service_name="voiceqas",rpc_method!="",rpc_method!="*",rpc_method!~".*TraceService.*|.*ServerReflection.*"}[15m]))',
      ),
    );
    const grpcOtel = promValue(
      await promInstant(
        'sum(rate(traces_spanmetrics_calls_total{service_name="voiceqas",span_name=~"gRPC .*"}[15m]))',
      ),
    );
    if ((grpcBeyla != null && grpcBeyla > 0) || (grpcOtel != null && grpcOtel > 0)) {
      ok('7. RED gRPC', `Beyla rpc=${grpcBeyla ?? 0}; OTel gRPC spans=${grpcOtel ?? 0}`);
    } else {
      warn(
        '7. RED gRPC',
        'Sem tráfego gRPC :9051 — painel gRPC (Beyla/OTel) fica vazio até haver chamadas (Tester gRPC ou grpcurl). Não é fake: é ausência de dados',
      );
    }
  } catch (e) {
    fail('7. RED gRPC', String(e.message || e));
  }

  // 8) APM list — TraceQL (search sem q esconde WS/STT sob polling GET)
  let traces = [];
  let blindSearchCount = 0;
  try {
    const end = Math.floor(Date.now() / 1000);
    const start = end - 7200;
    const blind = await getJson(`${TEMPO}/api/search?start=${start}&end=${end}&limit=50`);
    blindSearchCount = (blind.traces ?? []).filter((t) => coreTraceFilter(t.rootTraceName || '')).length;

    const q = encodeURIComponent(
      '{ resource.service.name="voiceqas" && name !~ ".*/(health|ready|metrics)" && name !~ "GET /(health|ready|metrics)" }',
    );
    const search = await getJson(`${TEMPO}/api/search?start=${start}&end=${end}&limit=50&q=${q}`);
    traces = (search.traces ?? []).filter((t) => coreTraceFilter(t.rootTraceName || ''));
    const rich = traces.filter((t) => /WS |gRPC |POST |pipeline/i.test(t.rootTraceName || ''));
    if (traces.length > 0) {
      const names = [...new Set(traces.map((t) => t.rootTraceName))].slice(0, 8).join(', ');
      const note =
        blindSearchCount > 0 && rich.length === 0
          ? ` (ATENÇÃO: search sem TraceQL devolvia ${blindSearchCount} GETs e escondia WS/STT)`
          : ` (blind search GETs≈${blindSearchCount}; rich=${rich.length})`;
      ok('8. APM Trace list', `${traces.length} traces via TraceQL; amostra: ${names}${note}`);
    } else fail('8. APM Trace list', 'Tempo TraceQL vazio — proxy /tempo ou Alloy/OTLP?');
  } catch (e) {
    fail('8. APM Trace list', `${e.message} — proxy nginx /tempo/ ou Tempo down`);
  }

  // 9) APM waterfall / spans depth — prefer STT/WS rich traces
  try {
    if (!traces.length) {
      fail('9. APM Waterfall / Spans', 'sem traces para detalhar');
    } else {
      let prefer =
        traces.find((t) => /transcribe|WS |gRPC |stt\/stream/i.test(t.rootTraceName || '')) || traces[0];

      // Fallback TraceQL targeting known rich roots
      if (!/transcribe|WS |gRPC /i.test(prefer.rootTraceName || '')) {
        const end = Math.floor(Date.now() / 1000);
        const start = end - 7200;
        for (const tq of [
          '{ name=~"WS.*" }',
          '{ name="POST /v1/stt/transcribe" }',
          '{ name=~"pipeline/.*" }',
        ]) {
          const found = await getJson(
            `${TEMPO}/api/search?start=${start}&end=${end}&limit=5&q=${encodeURIComponent(tq)}`,
          );
          if (found.traces?.length) {
            prefer = found.traces[0];
            break;
          }
        }
      }

      const body = await getJson(`${TEMPO}/api/traces/${prefer.traceID}`);
      const spans = [];
      for (const batch of body.batches ?? []) {
        for (const scope of batch.scopeSpans ?? []) {
          for (const sp of scope.spans ?? []) spans.push(sp);
        }
      }
      const pipelineKids = spans.filter((s) => String(s.name || '').startsWith('pipeline/'));
      const allAttrKeys = new Set();
      for (const sp of spans) {
        for (const a of sp.attributes ?? []) allAttrKeys.add(a.key);
      }
      const hasVoiceqas = [...allAttrKeys].some((k) => k.startsWith('voiceqas.'));
      const spanIdSample = spans[0]?.spanId || '';
      const looksBase64 = /^[A-Za-z0-9+/=]+$/.test(spanIdSample) && !/^[0-9a-f]+$/i.test(spanIdSample);

      const detail = `trace=${prefer.rootTraceName} spans=${spans.length} pipeline_child=${pipelineKids.length} voiceqas_attrs=${hasVoiceqas}`;
      if (spans.length > 1 && pipelineKids.length > 0) {
        ok('9. APM Waterfall / Spans', `${detail} — waterfall rico OK`);
      } else if (spans.length > 1) {
        warn('9. APM Waterfall / Spans', `${detail} — multi-span sem pipeline/*`);
      } else {
        fail(
          '9. APM Waterfall / Spans',
          `${detail}. UI “não funciona” se a lista só mostra GET leaf (1 span). Causa: /api/search sem TraceQL + polling do CC. Fix: searchTraces usa TraceQL; abra um POST/WS`,
        );
      }

      results._apmMeta = {
        prefer,
        spans,
        attrKeys: [...allAttrKeys],
        hasVoiceqas,
        looksBase64,
        spanIdSample,
        pipelineKids: pipelineKids.length,
      };
    }
  } catch (e) {
    fail('9. APM Waterfall / Spans', String(e.message || e));
  }

  // 10) Span details (attributes useful for UI)
  try {
    const meta = results._apmMeta;
    delete results._apmMeta;
    if (!meta?.spans?.length) {
      fail('10. APM Span Details', 'sem span para inspecionar');
    } else {
      const { hasVoiceqas, attrKeys, looksBase64, spanIdSample, prefer, spans, pipelineKids } = meta;
      const child = spans.find((s) => String(s.name || '').startsWith('pipeline/'));
      const childAttrs = (child?.attributes ?? []).map((a) => a.key);
      const problems = [];
      if (!hasVoiceqas) problems.push('sem atributos voiceqas.*');
      if (pipelineKids > 0 && childAttrs.length === 0) {
        problems.push('filho pipeline sem attributes');
      }
      if (spans.length === 1) {
        problems.push('só 1 span — Details não mostram decode/VAD/ASR (escolha POST/WS na lista)');
      }
      if (problems.length) {
        fail('10. APM Span Details', problems.join(' | '));
      } else {
        ok(
          '10. APM Span Details',
          `root=${prefer.rootTraceName}; attrs=${attrKeys.length}; pipeline child attrs=[${childAttrs.slice(0, 6).join(',')}]; spanIdFormat=${looksBase64 ? 'base64' : 'hex'} (${spanIdSample.slice(0, 12)}…)`,
        );
      }
    }
  } catch (e) {
    fail('10. APM Span Details', String(e.message || e));
  }

  // Summary
  console.log('\n=== SUMMARY ===');
  const pass = results.filter((r) => r.status === 'PASS').length;
  const warnN = results.filter((r) => r.status === 'WARN').length;
  const failN = results.filter((r) => r.status === 'FAIL').length;
  console.log(`PASS=${pass} WARN=${warnN} FAIL=${failN}`);

  console.log('\n=== WHY empty / broken (explain) ===');
  console.log(`- APM list: precisa TraceQL (searchTraces agora envia q=). Sem isso: só GET polling, WS/STT sumidos`);
  console.log(
    '- APM waterfall/details: POST /v1/stt/transcribe e WS /v1/stt/stream têm filhos pipeline/*; GET leaf = 1 span (parece “quebrado”)',
  );
  console.log('- RED WS: use spanmetrics OTel (WS *); Beyla :8081 pode ficar 0; gRPC vazio sem tráfego :9051');
  console.log('- Sankey: nodes/links do snapshot; sem bytes recentes o fluxo some visualmente');
  console.log('- ASR: engine ready ≠ ASR-ready sessions (precisa VQA gate); WER/CER n/a');

  const reportPath = 'scripts/cc-observability-e2e-last.json';
  try {
    writeFileSync(
      reportPath,
      JSON.stringify({ at: new Date().toISOString(), cc: CC, traffic, results, traces: traces.slice(0, 10) }, null, 2),
    );
    console.log(`\nReport: ${reportPath}`);
  } catch {
    /* ignore */
  }

  process.exit(failN > 0 ? 1 : 0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
