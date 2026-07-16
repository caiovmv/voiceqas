#!/usr/bin/env python3
"""E2E quality: DEFAULT mix (from calibration winner) must not regress STT vs LEGACY."""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WAVS = [
    ROOT / "chamada_c1f8e735-b04c-4b5b-9dd0-5d380d6be330.wav",
    ROOT / "chamada_nespresso_cliente_testando.wav",
]
CALIB = ROOT / "data" / "analysis-mix-calibration.json"


def load_calib_mod():
    path = ROOT / "scripts" / "calibrate-analysis-mix.py"
    spec = importlib.util.spec_from_file_location("calibrate_analysis_mix", path)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="whisper")
    ap.add_argument("--provider", default="cuda")
    ap.add_argument("--llm-smoke", action="store_true")
    ap.add_argument("--api", default="http://localhost:9080")
    args = ap.parse_args()

    m = load_calib_mod()
    m.CFG["api"] = args.api.rstrip("/")

    if not CALIB.exists():
        print(
            "missing calibration JSON — run scripts/calibrate-analysis-mix.py first",
            file=sys.stderr,
        )
        return 2
    calib = json.loads(CALIB.read_text(encoding="utf-8"))
    wmix = calib["winner"]["mix"]
    mix = m.Hyp(
        "DEF",
        bool(wmix["agc"]),
        bool(wmix["rnnoise"]),
        bool(wmix["silero"]),
        bool(wmix["focus_primary"]),
        wmix["gate"],
        "winner",
    )
    legacy = m.HYPS[0]
    print("winner mix:", mix.label, "rnnoise=", mix.rnnoise, "silero=", mix.silero)

    missing = [p for p in WAVS if not p.exists()]
    if missing:
        print("missing", missing, file=sys.stderr)
        return 2

    last = None
    for wav in WAVS:
        print(f"== {wav.name} ==", flush=True)
        pcm, _rate = m.load_pcm16(wav)
        leg = m.run_hyp(legacy, wav, pcm, 16000, args.model, args.provider, None)
        aft = m.run_hyp(mix, wav, pcm, 16000, args.model, args.provider, leg["text"])
        if aft["stt"]["stub"] or leg["stt"]["stub"]:
            raise AssertionError(f"{wav.name} stub")
        if aft["stt"]["keyword_recall"] + 1e-9 < leg["stt"]["keyword_recall"] - 0.02:
            raise AssertionError(
                f"{wav.name} KR regression "
                f"{aft['stt']['keyword_recall']:.3f} < {leg['stt']['keyword_recall']:.3f}"
            )
        if aft["vqa"]["ready_ratio"] + 1e-9 < leg["vqa"]["ready_ratio"] - 0.15:
            raise AssertionError(
                f"{wav.name} ready collapse "
                f"{aft['vqa']['ready_ratio']:.3f} << {leg['vqa']['ready_ratio']:.3f}"
            )
        print(
            f"OK kr L={leg['stt']['keyword_recall']:.2f} A={aft['stt']['keyword_recall']:.2f} "
            f"ready L={leg['vqa']['ready_ratio']:.2f} A={aft['vqa']['ready_ratio']:.2f}"
        )
        last = (leg, aft, mix)

    if args.llm_smoke and last:
        leg, aft, mix_h = last
        payload = {
            "mix": {
                "agc_enabled": mix_h.agc,
                "rnnoise_enabled": mix_h.rnnoise,
                "silero_vad_enabled": mix_h.silero,
                "focus_primary": mix_h.focus,
                "gate": mix_h.gate,
            },
            "before": {
                "vqa": leg["vqa"],
                "stt": {
                    "text": leg["text"],
                    "ok": True,
                    "keyword_recall": leg["stt"]["keyword_recall"],
                },
            },
            "after": {
                "vqa": aft["vqa"],
                "stt": {
                    "text": aft["text"],
                    "ok": True,
                    "keyword_recall": aft["stt"]["keyword_recall"],
                },
            },
        }
        body = json.dumps(payload).encode()
        req = urllib.request.Request(
            m.CFG["api"] + "/v1/analysis/llm",
            data=body,
            headers={"Content-Type": "application/json", "X-Ops-Token": "dev-write"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=180) as r:
            j = json.loads(r.read().decode())
        analysis = j.get("analysis") or ""
        if not re.search(r"###\s*1\.\s*Diagn", analysis, re.I):
            raise AssertionError("LLM missing ### 1. Diagnostico")
        if not re.search(r"###\s*2\.\s*Comparativo\s*STT", analysis, re.I):
            raise AssertionError("LLM missing ### 2. Comparativo STT")
        if not re.search(r"###\s*5\.\s*Plano", analysis, re.I):
            raise AssertionError("LLM missing ### 5")
        for word in ("packet_loss", "jitter_ms"):
            if word in analysis.lower():
                raise AssertionError(f"LLM invented {word}")
        if not re.search(r"stt|transcri|legado|antes|depois", analysis, re.I):
            raise AssertionError("LLM did not discuss STT comparison")
        print("OK llm smoke")

    print("ALL PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print("FAIL:", e, file=sys.stderr)
        raise SystemExit(1)
