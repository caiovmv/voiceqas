const VoiceQasUI = (() => {
  const params = new URLSearchParams(location.search);
  const apiBase = params.get('api') || location.origin;
  const wsBase = params.get('ws') || `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.hostname}:8081`;

  function $(id) { return document.getElementById(id); }

  function log(el, msg, clear = false) {
    if (!el) return;
    if (clear) el.textContent = '';
    const ts = new Date().toISOString().slice(11, 19);
    el.textContent += `[${ts}] ${msg}\n`;
    el.scrollTop = el.scrollHeight;
  }

  async function fetchJson(path, opts = {}) {
    const res = await fetch(`${apiBase}${path}`, opts);
    const text = await res.text();
    let data;
    try { data = JSON.parse(text); } catch { data = { raw: text }; }
    if (!res.ok) throw new Error(data.error || res.statusText || text);
    return data;
  }

  function pcmFromSine(freq, sampleRate, seconds, amplitude = 8000) {
    const n = Math.floor(sampleRate * seconds);
    const bytes = new Uint8Array(n * 2);
    const view = new DataView(bytes.buffer);
    for (let i = 0; i < n; i++) {
      const t = i / sampleRate;
      const sample = Math.round(amplitude * Math.sin(2 * Math.PI * freq * t));
      view.setInt16(i * 2, sample, true);
    }
    return bytes;
  }

  function int16BytesToArray(buf) {
    return Array.from(buf);
  }

  return { apiBase, wsBase, $, log, fetchJson, pcmFromSine, int16BytesToArray };
})();
