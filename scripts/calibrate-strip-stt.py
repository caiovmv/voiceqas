#!/usr/bin/env python3
"""Calibrate VoiceChannelStrip knobs so STT after matches LEGACY before.

Primary metric: token F1 vs LEGACY transcript + keyword recall.
Writes data/strip-stt-calibration.json
"""
from __future__ import annotations

import argparse
import copy
import json
import re
import sys
import urllib.error
import urllib.request
import wave
import array
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any
import unicodedata


def fold(s: str) -> str:
    s = s.casefold()
    return "".join(c for c in unicodedata.normalize("NFD", s) if unicodedata.category(c) != "Mn")


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "strip-stt-calibration.json"
CFG = {"api": "http://localhost:9080"}
TOKEN = "dev-write"

DEFAULT_STRIP: dict[str, Any] = {
    "nr": {"enabled": True, "wet_dry": 1.0},
    "hpf": {"enabled": True, "cutoff_hz": 80},
    "eq": {
        "enabled": True,
        "bands": [
            {"freq_hz": 250, "gain_db": -3, "q": 1.2},
            {"freq_hz": 450, "gain_db": -2, "q": 1.5},
            {"freq_hz": 2500, "gain_db": 2, "q": 0.8},
            {"freq_hz": 3500, "gain_db": 2, "q": 0.8},
        ],
    },
    "deesser": {
        "enabled": True,
        "center_hz": 6500,
        "bandwidth_hz": 2000,
        "threshold_db": -25,
        "ratio": 3,
        "attack_ms": 1,
        "release_ms": 40,
    },
    "compressor": {
        "enabled": True,
        "threshold_db": -20,
        "ratio": 3,
        "attack_ms": 5,
        "release_ms": 80,
        "makeup_db": 2,
    },
    "limiter": {"enabled": True, "ceiling_dbfs": -1},
    "agc": {
        "enabled": True,
        "target_rms_dbfs": -18,
        "max_gain_db": 24,
        "attack_ms": 5,
        "release_ms": 100,
    },
}

LEGACY_STRIP: dict[str, Any] = {
    "nr": {"enabled": False, "wet_dry": 1.0},
    "hpf": {"enabled": False, "cutoff_hz": 80},
    "eq": {"enabled": False, "bands": copy.deepcopy(DEFAULT_STRIP["eq"]["bands"])},
    "deesser": {**DEFAULT_STRIP["deesser"], "enabled": False},
    "compressor": {**DEFAULT_STRIP["compressor"], "enabled": False},
    "limiter": {"enabled": True, "ceiling_dbfs": -3},
    "agc": {
        "enabled": True,
        "target_rms_dbfs": -20,
        "max_gain_db": 24,
        "attack_ms": 5,
        "release_ms": 100,
    },
}


def strip_mut(**overrides: Any) -> dict[str, Any]:
    s = copy.deepcopy(DEFAULT_STRIP)
    for k, v in overrides.items():
        if isinstance(v, dict) and isinstance(s.get(k), dict):
            s[k] = {**s[k], **v}
        else:
            s[k] = v
    return s


WAVS = [
    ROOT / "voiceqas-original (1).wav",
    ROOT / "chamada_c1f8e735-b04c-4b5b-9dd0-5d380d6be330.wav",
    ROOT / "chamada_nespresso_cliente_testando.wav",
]

ANCHORS: dict[str, list[list[str]]] = {
    "voiceqas-original (1).wav": [
        ["caio"],
        ["latissima", "latissima"],
        ["red"],
        ["vaporiz", "vaporizacao"],
        ["leite"],
        ["suporte"],
        ["problema"],
    ],
    "chamada_c1f8e735-b04c-4b5b-9dd0-5d380d6be330.wav": [
        ["nespresso"],
        ["bianca"],
        ["latissima", "latissima"],
        ["leite"],
        ["espuma"],
        ["batedor"],
        ["pesquisa"],
        ["satisfacao", "satisfacao"],
    ],
    "chamada_nespresso_cliente_testando.wav": [
        ["nespresso"],
        ["alexandre"],
        ["leite"],
        ["tomada"],
        ["pesquisa"],
        ["satisfacao", "satisfacao"],
        ["cafe", "cafe"],
    ],
}

MUST_KEEP: dict[str, list[list[str]]] = {
    "voiceqas-original (1).wav": [
        ["latissima", "latissima"],
        ["suporte"],
        ["vaporiz"],
    ],
}


@dataclass
class Hyp:
    id: str
    label: str
    strip: dict[str, Any]
    silero: bool = False
    focus: bool = False


def make_hyps() -> list[Hyp]:
    mild_eq = {
        "enabled": True,
        "bands": [
            {"freq_hz": 250, "gain_db": -1.5, "q": 1.0},
            {"freq_hz": 450, "gain_db": -1.0, "q": 1.2},
            {"freq_hz": 2500, "gain_db": 1.0, "q": 0.8},
            {"freq_hz": 3500, "gain_db": 1.0, "q": 0.8},
        ],
    }
    presence_only = {
        "enabled": True,
        "bands": [
            {"freq_hz": 250, "gain_db": 0, "q": 1.0},
            {"freq_hz": 450, "gain_db": 0, "q": 1.0},
            {"freq_hz": 2500, "gain_db": 1.5, "q": 0.8},
            {"freq_hz": 3500, "gain_db": 1.5, "q": 0.8},
        ],
    }
    return [
        Hyp("S0", "legacy", copy.deepcopy(LEGACY_STRIP)),
        Hyp("S1", "default_full_strip", copy.deepcopy(DEFAULT_STRIP)),
        Hyp("S2", "no_nr", strip_mut(nr={"enabled": False})),
        Hyp("S3", "no_eq", strip_mut(eq={"enabled": False})),
        Hyp("S4", "no_deess", strip_mut(deesser={"enabled": False})),
        Hyp("S5", "no_comp", strip_mut(compressor={"enabled": False})),
        Hyp("S6", "no_hpf", strip_mut(hpf={"enabled": False})),
        Hyp("S7", "agc_lim_only", strip_mut(
            nr={"enabled": False},
            hpf={"enabled": False},
            eq={"enabled": False},
            deesser={"enabled": False},
            compressor={"enabled": False},
            limiter={"enabled": True, "ceiling_dbfs": -1},
            agc={"enabled": True, "target_rms_dbfs": -18},
        )),
        Hyp("S8", "legacy_alike_m18", strip_mut(
            nr={"enabled": False},
            hpf={"enabled": False},
            eq={"enabled": False},
            deesser={"enabled": False},
            compressor={"enabled": False},
            limiter={"enabled": True, "ceiling_dbfs": -3},
            agc={"enabled": True, "target_rms_dbfs": -20},
        )),
        Hyp("S9", "nr_soft_03", strip_mut(nr={"enabled": True, "wet_dry": 0.3})),
        Hyp("S10", "nr_soft_05", strip_mut(nr={"enabled": True, "wet_dry": 0.5})),
        Hyp("S11", "nr_off_mild_eq", strip_mut(nr={"enabled": False}, eq=mild_eq)),
        Hyp("S12", "nr_off_presence", strip_mut(
            nr={"enabled": False},
            deesser={"enabled": False},
            compressor={"enabled": False},
            eq=presence_only,
        )),
        Hyp("S13", "dynamics_only", strip_mut(
            nr={"enabled": False},
            hpf={"enabled": True},
            eq={"enabled": False},
            deesser={"enabled": False},
            compressor={"enabled": True},
        )),
        Hyp("S14", "hpf_agc", strip_mut(
            nr={"enabled": False},
            hpf={"enabled": True, "cutoff_hz": 80},
            eq={"enabled": False},
            deesser={"enabled": False},
            compressor={"enabled": False},
        )),
        Hyp("S15", "nr_off_no_deess_mild", strip_mut(
            nr={"enabled": False},
            deesser={"enabled": False},
            eq=mild_eq,
            compressor={"enabled": True, "makeup_db": 0, "ratio": 2},
        )),
        Hyp("S16", "nr_02_hpf_agc", strip_mut(
            nr={"enabled": True, "wet_dry": 0.2},
            hpf={"enabled": True},
            eq={"enabled": False},
            deesser={"enabled": False},
            compressor={"enabled": False},
        )),
        Hyp("S17", "default_no_nr_no_deess", strip_mut(
            nr={"enabled": False},
            deesser={"enabled": False},
        )),
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


def token_f1(ref: str, hyp: str) -> float:
    a = [t for t in re.split(r"\s+", fold(ref)) if t]
    b = [t for t in re.split(r"\s+", fold(hyp)) if t]
    if not a and not b:
        return 1.0
    if not a or not b:
        return 0.0
    ca, cb = Counter(a), Counter(b)
    overlap = sum((ca & cb).values())
    prec = overlap / max(1, sum(cb.values()))
    rec = overlap / max(1, sum(ca.values()))
    if prec + rec == 0:
        return 0.0
    return 2 * prec * rec / (prec + rec)


def keyword_recall(text: str, groups: list[list[str]]) -> float:
    low = fold(text)
    if not groups:
        return 0.0
    hits = sum(1 for alts in groups if any(fold(a) in low for a in alts))
    return hits / len(groups)


def must_keep_ok(text: str, name: str) -> bool:
    groups = MUST_KEEP.get(name, [])
    if not groups:
        return True
    low = fold(text)
    return all(any(fold(a) in low for a in alts) for alts in groups)


def stt(pcm: bytes, h: Hyp, model: str, provider: str, sid: str) -> dict[str, Any]:
    headers = {
        "Content-Type": "application/octet-stream",
        "X-Audio-Format": "pcm_s16le_16k",
        "X-Sample-Rate": "16000",
        "X-Language": "pt",
        "X-STT-Model": model,
        "X-STT-Provider": provider,
        "X-Session-Id": sid,
        "X-Audio-AGC": "1" if h.strip["agc"]["enabled"] else "0",
        "X-Audio-Enhancement": "1" if h.strip["nr"]["enabled"] else "0",
        "X-Audio-Strip": json.dumps(h.strip, separators=(",", ":")),
        "X-STT-Diarization": "1" if h.silero else "0",
        "X-STT-Focus-Primary": "1" if h.focus else "0",
    }
    return http_json("POST", "/v1/stt/transcribe", data=pcm, headers=headers)


def run(args: argparse.Namespace) -> int:
    CFG["api"] = args.api.rstrip("/")
    hyps = make_hyps()
    if args.only:
        want = {x.strip() for x in args.only.split(",")}
        hyps = [h for h in hyps if h.id in want or h.label in want]
    wavs = [Path(p) if Path(p).is_absolute() else ROOT / p for p in args.wav] if args.wav else [
        w for w in WAVS if w.exists()
    ]
    if not wavs:
        print("No WAVs found", file=sys.stderr)
        return 2

    pcm_cache: dict[str, bytes] = {}
    for w in wavs:
        pcm, _ = load_pcm16(w)
        pcm_cache[w.name] = pcm
        print(f"loaded {w.name}: {len(pcm)//2} samples", flush=True)

    per_wav: dict[str, Any] = {}
    legacy_text: dict[str, str] = {}
    ordered = sorted(hyps, key=lambda h: 0 if h.id == "S0" else 1)

    for w in wavs:
        name = w.name
        per_wav[name] = {}
        pcm = pcm_cache[name]
        anchors = ANCHORS.get(name, [])
        for h in ordered:
            sid = f"strip-cal-{h.id}-{name[:20]}"
            print(f"== {name} / {h.id} {h.label} ...", flush=True)
            try:
                resp = stt(pcm, h, args.model, args.provider, sid)
            except Exception as e:
                print(f"  FAIL: {e}", flush=True)
                per_wav[name][h.id] = {"error": str(e), "label": h.label}
                continue
            text = (resp.get("text") or resp.get("transcript") or "").strip()
            if h.id == "S0" or h.label == "legacy":
                legacy_text[name] = text
            ref = legacy_text.get(name, text)
            kr = keyword_recall(text, anchors)
            f1 = token_f1(ref, text)
            mk = must_keep_ok(text, name)
            row = {
                "hyp_id": h.id,
                "label": h.label,
                "strip": h.strip,
                "stt": {
                    "text": text,
                    "text_head": text[:140],
                    "chars": len(text),
                    "processing_ms": resp.get("processing_ms") or resp.get("latency_ms"),
                    "model": resp.get("model") or resp.get("model_id"),
                    "keyword_recall": kr,
                    "token_f1_vs_legacy": f1,
                    "must_keep_ok": mk,
                },
            }
            per_wav[name][h.id] = row
            print(
                f"  F1={f1:.3f} KR={kr:.3f} must={mk} ms={row['stt']['processing_ms']} | {text[:120]}",
                flush=True,
            )

    scores: list[tuple[Any, ...]] = []
    hyp_ids = [h.id for h in hyps]
    for hid in hyp_ids:
        f1s, krs, mks = [], [], []
        for name in per_wav:
            row = per_wav[name].get(hid)
            if not row or "stt" not in row:
                continue
            f1s.append(row["stt"]["token_f1_vs_legacy"])
            krs.append(row["stt"]["keyword_recall"])
            mks.append(1.0 if row["stt"]["must_keep_ok"] else 0.0)
        if not f1s:
            continue
        short = per_wav.get("voiceqas-original (1).wav", {}).get(hid, {}).get("stt", {})
        scores.append(
            (
                short.get("must_keep_ok", False),
                short.get("token_f1_vs_legacy", 0.0),
                short.get("keyword_recall", 0.0),
                sum(mks) / len(mks),
                sum(f1s) / len(f1s),
                sum(krs) / len(krs),
                hid,
            )
        )
    scores.sort(reverse=True)
    ranking = [
        {
            "hyp_id": s[-1],
            "short_must_keep": s[0],
            "short_f1": s[1],
            "short_kr": s[2],
            "avg_must_keep": s[3],
            "avg_f1": s[4],
            "avg_kr": s[5],
            "label": next(h.label for h in hyps if h.id == s[-1]),
        }
        for s in scores
    ]

    winner = None
    for s in scores:
        hid = s[-1]
        if hid == "S0":
            continue
        if s[0] and s[1] >= 0.92:
            winner = hid
            break
    if winner is None and scores:
        for s in scores:
            if s[-1] != "S0" and s[0]:
                winner = s[-1]
                break
    if winner is None and scores:
        winner = scores[0][-1]

    winner_strip = next((h.strip for h in hyps if h.id == winner), None)
    doc = {
        "model": args.model,
        "provider": args.provider,
        "api": CFG["api"],
        "wavs": [w.name for w in wavs],
        "legacy_text": legacy_text,
        "per_wav": per_wav,
        "ranking": ranking,
        "winner": {"hyp_id": winner, "strip": winner_strip},
        "goal": "match-before: STT after ~ LEGACY before; apply yaml+lab",
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(doc, ensure_ascii=False, indent=2), encoding="utf-8")
    print("\n=== RANKING ===", flush=True)
    for r in ranking[:10]:
        print(
            f"{r['hyp_id']:4} {r['label']:22} shortF1={r['short_f1']:.3f} "
            f"shortKR={r['short_kr']:.3f} must={r['short_must_keep']} "
            f"avgF1={r['avg_f1']:.3f} avgKR={r['avg_kr']:.3f}",
            flush=True,
        )
    print(f"\nWINNER: {winner}", flush=True)
    print(f"wrote {OUT}", flush=True)
    if args.apply_check:
        if not winner or winner == "S1":
            print("APPLY-CHECK FAIL: winner still default_full_strip or missing", file=sys.stderr)
            return 1
        short = per_wav.get("voiceqas-original (1).wav", {}).get(winner, {}).get("stt", {})
        if short and (not short.get("must_keep_ok") or short.get("token_f1_vs_legacy", 0) < 0.85):
            print(
                f"APPLY-CHECK FAIL: short F1={short.get('token_f1_vs_legacy')} must={short.get('must_keep_ok')}",
                file=sys.stderr,
            )
            return 1
        print("APPLY-CHECK OK", flush=True)
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--model", default="whisper")
    p.add_argument("--provider", default="cuda")
    p.add_argument("--api", default="http://localhost:9080")
    p.add_argument("--wav", action="append", help="Relative to repo root or absolute")
    p.add_argument("--only", help="Comma hyp ids, e.g. S0,S1,S2")
    p.add_argument("--apply-check", action="store_true")
    return run(p.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
