#!/usr/bin/env python3
"""Grid-calibrate Analysis Lab mix vs LEGACY on Nespresso fixture WAVs.

Hypotheses H0..H5 → STT (whisper) + VQA batch → keyword recall + VQA ready/score.
Writes data/analysis-mix-calibration.json and prints ranking.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
import urllib.error
import urllib.request
import wave
import array
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "analysis-mix-calibration.json"
CFG = {"api": "http://localhost:9080"}
TOKEN = "dev-write"
STUBS = {"yeah", "yeah.", "e", "e.", "ora", "ora.", "ok", "ok."}

WAVS = [
    ROOT / "chamada_c1f8e735-b04c-4b5b-9dd0-5d380d6be330.wav",
    ROOT / "chamada_nespresso_cliente_testando.wav",
]

# Anchor glossary per fixture (case-insensitive substring / alt forms).
ANCHORS: dict[str, list[list[str]]] = {
    "chamada_c1f8e735-b04c-4b5b-9dd0-5d380d6be330.wav": [
        ["nespresso"],
        ["bianca"],
        ["latissima", "latíssima"],
        ["leite"],
        ["espuma"],
        ["batedor"],
        ["pesquisa"],
        ["satisfacao", "satisfação"],
    ],
    "chamada_nespresso_cliente_testando.wav": [
        ["nespresso"],
        ["alexandre"],
        ["inicia", "inícia", "inicia"],
        ["leite"],
        ["tomada"],
        ["pesquisa"],
        ["satisfacao", "satisfação"],
        ["cafe", "café"],
    ],
}

GATE_DEFAULT = {
    "stt_ready_threshold": 65,
    "min_snr_db": 12,
    "max_clipping_ratio": 0.02,
    "max_silence_ratio": 0.6,
    "max_silence_ratio_for_ready": 0.4,
    "hysteresis_ok_windows": 2,
    "hysteresis_bad_windows": 1,
    "split_quality_from_presence": True,
}

GATE_LEGACY = {
    **GATE_DEFAULT,
    "max_silence_ratio_for_ready": 0.6,
    "split_quality_from_presence": False,
}

GATE_SOFT = {
    **GATE_DEFAULT,
    "stt_ready_threshold": 55,
    "max_silence_ratio_for_ready": 0.55,
}


@dataclass
class Hyp:
    id: str
    agc: bool
    rnnoise: bool
    silero: bool
    focus: bool
    gate: dict[str, Any]
    label: str


HYPS = [
    Hyp("H0", True, False, False, False, GATE_LEGACY, "legacy"),
    Hyp("H1", True, True, True, False, GATE_DEFAULT, "default_today"),
    Hyp("H2", True, False, True, False, GATE_DEFAULT, "no_denoise"),
    Hyp("H3", True, False, False, False, GATE_DEFAULT, "agc_split_gate"),
    Hyp("H4", True, True, False, False, GATE_DEFAULT, "denoise_only"),
    Hyp("H5", True, False, True, False, GATE_SOFT, "soft_gate"),
]


def load_pcm16(path: Path) -> tuple[bytes, int]:
    with wave.open(str(path), "rb") as w:
        rate = w.getframerate()
        nch = w.getnchannels()
        raw = w.readframes(w.getnframes())
        sw = w.getsampwidth()
    if sw != 2:
        raise SystemExit(f"{path.name}: expected 16-bit PCM, got sampwidth={sw}")
    samples = array.array("h")
    samples.frombytes(raw)
    if nch == 2:
        samples = array.array(
            "h", ((samples[i] + samples[i + 1]) // 2 for i in range(0, len(samples), 2))
        )
    if rate != 16000:
        ratio = rate / 16000
        out = array.array("h")
        n = int(len(samples) / ratio)
        for i in range(n):
            src = i * ratio
            idx = int(src)
            frac = src - idx
            a = samples[idx]
            b = samples[idx + 1] if idx + 1 < len(samples) else a
            out.append(int(a + (b - a) * frac))
        samples, rate = out, 16000
    return samples.tobytes(), rate


def http_json(
    method: str,
    path: str,
    data: bytes | None = None,
    headers: dict[str, str] | None = None,
    timeout: int = 600,
) -> Any:
    h = {"X-Ops-Token": TOKEN}
    if headers:
        h.update(headers)
    req = urllib.request.Request(CFG["api"] + path, data=data, headers=h, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            body = r.read().decode()
            return json.loads(body) if body else {}
    except urllib.error.HTTPError as e:
        err = e.read().decode(errors="replace")
        raise RuntimeError(f"HTTP {e.code} {path}: {err[:400]}") from e


def stt_headers(h: Hyp, model: str, provider: str, sid: str) -> dict[str, str]:
    return {
        "Content-Type": "application/octet-stream",
        "X-Audio-Format": "pcm_s16le_16k",
        "X-Sample-Rate": "16000",
        "X-Language": "pt",
        "X-STT-Model": model,
        "X-STT-Provider": provider,
        "X-Session-Id": sid,
        "X-Audio-AGC": "1" if h.agc else "0",
        "X-Audio-Enhancement": "1" if h.rnnoise else "0",
        "X-STT-Diarization": "1" if h.silero else "0",
        "X-STT-Focus-Primary": "1" if h.focus else "0",
    }


def vqa_headers(h: Hyp, sid: str) -> dict[str, str]:
    return {
        "Content-Type": "application/octet-stream",
        "X-Audio-Format": "pcm_s16le_16k",
        "X-Sample-Rate": "16000",
        "X-Session-Id": sid,
        "X-Audio-AGC": "1" if h.agc else "0",
        "X-Audio-Enhancement": "1" if h.rnnoise else "0",
    }


def keyword_recall(text: str, groups: list[list[str]]) -> float:
    low = text.casefold()
    if not groups:
        return 0.0
    hits = 0
    for alts in groups:
        if any(a.casefold() in low for a in alts):
            hits += 1
    return hits / len(groups)


def token_f1(ref: str, hyp: str) -> float:
    a = [t for t in re.split(r"\s+", ref.casefold()) if t]
    b = [t for t in re.split(r"\s+", hyp.casefold()) if t]
    if not a and not b:
        return 1.0
    if not a or not b:
        return 0.0
    from collections import Counter

    ca, cb = Counter(a), Counter(b)
    overlap = sum((ca & cb).values())
    prec = overlap / max(1, sum(cb.values()))
    rec = overlap / max(1, sum(ca.values()))
    if prec + rec == 0:
        return 0.0
    return 2 * prec * rec / (prec + rec)


def speech_quality_score(m: dict[str, Any], gate: dict[str, Any]) -> float:
    score = 100.0
    clip = float(m.get("clipping_ratio") or 0)
    sil = float(m.get("silence_ratio") or 0)
    snr = float(m.get("snr_estimate_db") or 0)
    flat = float(m.get("spectral_flatness") or 0)
    pl = float(m.get("packet_loss_pct") or 0)
    jit = float(m.get("jitter_ms") or 0)
    rms = float(m.get("rms_dbfs") or -60)
    score -= min(1.0, clip / max(float(gate["max_clipping_ratio"]), 1e-6)) * 30.0
    if not gate.get("split_quality_from_presence"):
        score -= min(1.0, sil / max(float(gate["max_silence_ratio"]), 1e-6)) * 25.0
    snr_factor = min(1.5, max(0.0, snr / max(float(gate["min_snr_db"]), 1e-6)))
    score = score * (0.5 + 0.5 * snr_factor)
    score -= min(1.0, max(0.0, (flat - 0.3) / 0.7)) * 15.0
    score -= min(1.0, pl / 5.0) * 10.0 + min(1.0, jit / 50.0) * 10.0
    if rms < -50.0:
        score -= min(20.0, (-50.0 - rms) * 0.5)
    return min(100.0, max(0.0, score))


def passes_thresholds(m: dict[str, Any], quality: float, gate: dict[str, Any]) -> bool:
    return (
        quality >= float(gate["stt_ready_threshold"])
        and float(m.get("silence_ratio") or 0) <= float(gate["max_silence_ratio_for_ready"])
        and float(m.get("clipping_ratio") or 0) <= float(gate["max_clipping_ratio"])
        and float(m.get("snr_estimate_db") or 0) >= float(gate["min_snr_db"])
    )


def rescore_batch(batch: dict[str, Any], gate: dict[str, Any]) -> dict[str, float]:
    windows = batch.get("windows") or []
    scored = []
    for w in windows:
        q = speech_quality_score(w, gate)
        scored.append({**w, "speech_quality_score": q})
    # hysteresis
    consecutive_ok = 0
    consecutive_bad = 0
    state = False
    with_ready = []
    for w in scored:
        ok = passes_thresholds(w, w["speech_quality_score"], gate)
        if ok:
            consecutive_ok += 1
            consecutive_bad = 0
            if not state and consecutive_ok >= int(gate["hysteresis_ok_windows"]):
                state = True
        else:
            consecutive_bad += 1
            consecutive_ok = 0
            if state and consecutive_bad >= int(gate["hysteresis_bad_windows"]):
                state = False
        with_ready.append({**w, "stt_ready": state})

    speech = [
        w
        for w in with_ready
        if float(w.get("silence_ratio") or 0) <= float(gate["max_silence_ratio"])
    ]
    if speech:
        sw = 0.0
        ss = 0.0
        for w in speech:
            weight = max(0.0, 1.0 - float(w.get("silence_ratio") or 0))
            weight = weight if weight > 1e-12 else 1.0
            ss += w["speech_quality_score"] * weight
            sw += weight
        speech_agg = ss / sw if sw else 0.0
    else:
        speech_agg = 0.0
    overall = (
        sum(w["speech_quality_score"] for w in with_ready) / len(with_ready) if with_ready else 0.0
    )
    ready_n = sum(1 for w in with_ready if w.get("stt_ready"))
    ready_ratio = ready_n / len(with_ready) if with_ready else 0.0
    composite = speech_agg if gate.get("split_quality_from_presence") and speech else overall
    return {
        "composite_score": composite,
        "ready_ratio": ready_ratio,
        "overall_window_mean": overall,
        "speech_window_count": float(len(speech)),
    }


def is_stub(text: str) -> bool:
    t = text.strip().casefold().rstrip(".")
    return t in STUBS or len(text.strip()) < 80


def run_hyp(
    h: Hyp,
    wav: Path,
    pcm: bytes,
    rate: int,
    model: str,
    provider: str,
    legacy_text: str | None,
) -> dict[str, Any]:
    sid = f"calib-{h.id}-{wav.stem[:20]}"
    print(f"  STT {h.id}…", flush=True)
    stt = http_json(
        "POST",
        "/v1/stt/transcribe",
        data=pcm,
        headers=stt_headers(h, model, provider, sid + "-stt"),
        timeout=600,
    )
    text = (stt.get("text") or "").strip()
    print(f"  VQA {h.id}…", flush=True)
    batch = http_json(
        "POST",
        "/v1/analyze/batch",
        data=pcm,
        headers=vqa_headers(h, sid + "-vqa"),
        timeout=300,
    )
    vqa = rescore_batch(batch, h.gate)
    groups = ANCHORS.get(wav.name, [])
    kr = keyword_recall(text, groups)
    f1 = token_f1(legacy_text, text) if legacy_text is not None else 1.0
    return {
        "hyp_id": h.id,
        "label": h.label,
        "mix": {
            "agc": h.agc,
            "rnnoise": h.rnnoise,
            "silero": h.silero,
            "focus_primary": h.focus,
            "gate": h.gate,
        },
        "stt": {
            "chars": len(text),
            "processing_ms": stt.get("processing_ms"),
            "model": stt.get("model"),
            "stub": is_stub(text),
            "keyword_recall": kr,
            "token_f1_vs_legacy": f1,
            "text_head": text[:120],
        },
        "vqa": vqa,
        "text": text,
    }


def composite_rank_key(row: dict[str, Any], baseline_kr: float) -> tuple:
    """Higher is better for sorting (negate used for max)."""
    stt = row["stt"]
    vqa = row["vqa"]
    if stt["stub"]:
        return (-999, 0, 0, 0)
    kr = stt["keyword_recall"]
    # Prefer no STT regression vs H0, then higher KR, then ready, then composite, then F1
    no_reg = 1 if kr >= baseline_kr - 0.02 else 0
    return (no_reg, kr, vqa["ready_ratio"], vqa["composite_score"], stt["token_f1_vs_legacy"])


def pick_winner(by_hyp: dict[str, list[dict[str, Any]]]) -> dict[str, Any]:
    # Average metrics across WAVs per hyp
    baseline_rows = by_hyp.get("H0", [])
    baseline_kr = (
        sum(r["stt"]["keyword_recall"] for r in baseline_rows) / len(baseline_rows)
        if baseline_rows
        else 0.0
    )
    ranked = []
    for hid, rows in by_hyp.items():
        if not rows:
            continue
        n = len(rows)
        avg = {
            "hyp_id": hid,
            "label": rows[0]["label"],
            "mix": rows[0]["mix"],
            "avg_keyword_recall": sum(r["stt"]["keyword_recall"] for r in rows) / n,
            "avg_ready_ratio": sum(r["vqa"]["ready_ratio"] for r in rows) / n,
            "avg_composite_score": sum(r["vqa"]["composite_score"] for r in rows) / n,
            "avg_token_f1_vs_legacy": sum(r["stt"]["token_f1_vs_legacy"] for r in rows) / n,
            "any_stub": any(r["stt"]["stub"] for r in rows),
            "score_key": None,
        }
        # Fake row for key
        fake = {
            "stt": {
                "stub": avg["any_stub"],
                "keyword_recall": avg["avg_keyword_recall"],
                "token_f1_vs_legacy": avg["avg_token_f1_vs_legacy"],
            },
            "vqa": {
                "ready_ratio": avg["avg_ready_ratio"],
                "composite_score": avg["avg_composite_score"],
            },
        }
        avg["score_key"] = list(composite_rank_key(fake, baseline_kr))
        ranked.append(avg)
    ranked.sort(key=lambda r: tuple(r["score_key"]), reverse=True)
    # Prefer non-H0 if tied-ish improvement on VQA without STT reg
    winner = ranked[0]
    for cand in ranked:
        if cand["hyp_id"] == "H0":
            continue
        if cand["any_stub"]:
            continue
        if cand["avg_keyword_recall"] >= baseline_kr - 0.02:
            # beat H0 if better KR or better ready without KR loss
            if (
                cand["avg_keyword_recall"] > baseline_kr + 1e-9
                or cand["avg_ready_ratio"]
                > next(r["avg_ready_ratio"] for r in ranked if r["hyp_id"] == "H0") + 1e-9
            ):
                winner = cand
                break
    # If still H0, take best non-stub with no STT regression (H2/H3 expected)
    if winner["hyp_id"] == "H0":
        for cand in ranked:
            if cand["hyp_id"] == "H0" or cand["any_stub"]:
                continue
            if cand["avg_keyword_recall"] >= baseline_kr - 0.02:
                winner = cand
                break
    return {"baseline_kr": baseline_kr, "ranking": ranked, "winner": winner}


def default_mix_ts(winner: dict[str, Any]) -> str:
    m = winner["mix"]
    g = m["gate"]
    return (
        f"rnnoise={m['rnnoise']} silero={m['silero']} focus={m['focus_primary']} "
        f"split={g.get('split_quality_from_presence')} "
        f"silence_ready={g.get('max_silence_ratio_for_ready')} thr={g.get('stt_ready_threshold')}"
    )


def apply_check(result: dict[str, Any]) -> int:
    """Validate that committed defaults match last winner expectations."""
    w = result["winner"]
    m = w["mix"]
    # Expected after calib: RNNoise off for these clean fixtures
    ok = True
    if m.get("rnnoise"):
        print("FAIL apply-check: winner still has rnnoise on (regressão esperada nos WAVs limpos)")
        ok = False
    if w.get("any_stub"):
        print("FAIL apply-check: winner has stub STT")
        ok = False
    if w["avg_keyword_recall"] + 1e-9 < result["baseline_kr"] - 0.02:
        print("FAIL apply-check: STT keyword recall regresses vs legacy")
        ok = False
    print(
        "apply-check",
        "PASS" if ok else "FAIL",
        default_mix_ts(w),
        f"kr={w['avg_keyword_recall']:.3f} ready={w['avg_ready_ratio']:.3f}",
    )
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="whisper")
    ap.add_argument("--provider", default="cuda")
    ap.add_argument("--apply-check", action="store_true", help="Only validate last calibration JSON")
    ap.add_argument("--api", default=CFG["api"])
    args = ap.parse_args()
    CFG["api"] = args.api.rstrip("/")

    if args.apply_check:
        if not OUT.exists():
            print("missing", OUT, file=sys.stderr)
            return 2
        return apply_check(json.loads(OUT.read_text(encoding="utf-8")))

    missing = [p for p in WAVS if not p.exists()]
    if missing:
        print("missing wavs:", missing, file=sys.stderr)
        return 2

    by_hyp: dict[str, list[dict[str, Any]]] = {h.id: [] for h in HYPS}
    per_wav: dict[str, Any] = {}

    for wav in WAVS:
        print(f"== {wav.name} ==", flush=True)
        pcm, rate = load_pcm16(wav)
        # Always run H0 first for legacy reference text
        legacy = run_hyp(HYPS[0], wav, pcm, rate, args.model, args.provider, None)
        by_hyp["H0"].append(legacy)
        legacy_text = legacy["text"]
        wav_rows = {"H0": legacy}
        for h in HYPS[1:]:
            row = run_hyp(h, wav, pcm, rate, args.model, args.provider, legacy_text)
            by_hyp[h.id].append(row)
            wav_rows[h.id] = {k: v for k, v in row.items() if k != "text"}
        per_wav[wav.name] = {
            hid: {k: v for k, v in r.items() if k != "text"} for hid, r in wav_rows.items()
        }
        print(
            f"  H0 kr={legacy['stt']['keyword_recall']:.2f} ready={legacy['vqa']['ready_ratio']:.2f}",
            flush=True,
        )

    pick = pick_winner(by_hyp)
    result = {
        "model": args.model,
        "provider": args.provider,
        "wavs": [p.name for p in WAVS],
        "per_wav": per_wav,
        **pick,
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")

    print("\n=== RANKING ===")
    for r in pick["ranking"]:
        print(
            f"{r['hyp_id']:4} {r['label']:16} kr={r['avg_keyword_recall']:.3f} "
            f"ready={r['avg_ready_ratio']:.3f} comp={r['avg_composite_score']:.1f} "
            f"f1={r['avg_token_f1_vs_legacy']:.3f} stub={r['any_stub']}"
        )
    w = pick["winner"]
    print("\nWINNER:", w["hyp_id"], w["label"], default_mix_ts(w))
    print("wrote", OUT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())