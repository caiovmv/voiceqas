#!/usr/bin/env python3
"""API e2e: Lab-style before/after STT on fixture WAVs. Fail on stub transcript."""
from __future__ import annotations

import json
import sys
import urllib.request
import wave
import array
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = "http://localhost:9080/v1/stt/transcribe"
STUBS = {"yeah", "yeah.", "e", "e.", "ora", "ora.", "ok", "ok."}

WAVS = [
    ROOT / "chamada_c1f8e735-b04c-4b5b-9dd0-5d380d6be330.wav",
    ROOT / "chamada_nespresso_cliente_testando.wav",
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


def post(pcm: bytes, rate: int, headers: dict, label: str) -> dict:
    req = urllib.request.Request(
        API,
        data=pcm,
        headers={
            "Content-Type": "application/octet-stream",
            "X-Audio-Format": "pcm_s16le_16k",
            "X-Sample-Rate": str(rate),
            "X-Language": "pt",
            "X-STT-Model": "whisper",
            "X-STT-Provider": "cuda",
            "X-Session-Id": f"e2e-{label}",
            **headers,
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=300) as r:
        return json.loads(r.read().decode())


def assert_good(label: str, j: dict, min_chars: int = 80) -> None:
    text = (j.get("text") or "").strip()
    ms = int(j.get("processing_ms") or 0)
    low = text.lower().rstrip(".")
    if low in STUBS or len(text) < min_chars:
        raise AssertionError(f"{label}: stub/short STT text={text!r} ms={ms}")
    if ms < 200:
        raise AssertionError(f"{label}: suspiciously fast STT ms={ms} text={text!r}")
    print(f"OK {label}: chars={len(text)} ms={ms} head={text[:60]!r}")


def main() -> int:
    missing = [p for p in WAVS if not p.exists()]
    if missing:
        print("missing wavs:", missing, file=sys.stderr)
        return 2

    h_before = {
        "X-Audio-AGC": "1",
        "X-Audio-Enhancement": "0",
        "X-STT-Diarization": "0",
        "X-STT-Focus-Primary": "0",
    }
    h_after = {
        # Calibrated DEFAULT_MIX (H5): AGC on, RNNoise off, Silero on
        "X-Audio-AGC": "1",
        "X-Audio-Enhancement": "0",
        "X-STT-Diarization": "1",
        "X-STT-Focus-Primary": "0",
    }

    for wav in WAVS:
        print(f"== {wav.name} ==")
        pcm, rate = load_pcm16(wav)
        before = post(pcm, rate, h_before, f"{wav.stem}-before")
        after = post(pcm, rate, h_after, f"{wav.stem}-after")
        assert_good(f"{wav.name} before", before)
        assert_good(f"{wav.name} after", after)
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())