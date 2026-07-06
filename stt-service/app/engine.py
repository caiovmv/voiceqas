from __future__ import annotations

import os
import time
from dataclasses import dataclass
from typing import Any

import numpy as np

_MODEL: Any | None = None
_ASR_VAD: Any | None = None
_MODEL_NAME = os.getenv(
    "STT_MODEL_REPO",
    "alefiury/parakeet-tdt-0.6b-v3-ptBR-TAGARELA-onnx",
)
_MODEL_ARCH = os.getenv("STT_MODEL_ARCH", "nemo-conformer-tdt")
_DEFAULT_LANGUAGE = os.getenv("STT_LANGUAGE", "pt")


@dataclass
class TranscriptSegment:
    start_ms: int
    end_ms: int
    text: str


@dataclass
class TranscriptResult:
    text: str
    model: str
    language: str
    duration_ms: int
    processing_ms: int
    segments: list[TranscriptSegment]


def _pcm16_to_float32(pcm_bytes: bytes) -> np.ndarray:
    samples = np.frombuffer(pcm_bytes, dtype=np.int16)
    if samples.size == 0:
        return np.array([], dtype=np.float32)
    return (samples.astype(np.float32) / 32768.0).clip(-1.0, 1.0)


def _extract_text(result: Any) -> str:
    if isinstance(result, str):
        return result.strip()
    if hasattr(result, "text"):
        return str(result.text).strip()
    return str(result).strip()


def _extract_segments(result: Any) -> list[TranscriptSegment]:
    segments: list[TranscriptSegment] = []
    if hasattr(result, "segments"):
        for seg in result.segments:
            start = int(getattr(seg, "start", 0) * 1000) if hasattr(seg, "start") else int(seg.get("start_ms", 0))
            end = int(getattr(seg, "end", 0) * 1000) if hasattr(seg, "end") else int(seg.get("end_ms", 0))
            text = getattr(seg, "text", None) or seg.get("text", "")
            segments.append(TranscriptSegment(start_ms=start, end_ms=end, text=str(text).strip()))
    return segments


def load_engine() -> None:
    global _MODEL, _ASR_VAD
    if _MODEL is not None:
        return

    import onnx_asr

    local_dir = os.getenv("STT_MODEL_DIR")
    if local_dir and os.path.isdir(local_dir):
        _MODEL = onnx_asr.load_model(_MODEL_ARCH, local_dir)
    else:
        _MODEL = onnx_asr.load_model(_MODEL_ARCH, _MODEL_NAME)

    vad = onnx_asr.load_vad("silero")
    _ASR_VAD = _MODEL.with_vad(vad)


def is_ready() -> bool:
    return _ASR_VAD is not None


def transcribe_pcm16(
    pcm_bytes: bytes,
    sample_rate: int,
    language: str | None = None,
) -> TranscriptResult:
    if _ASR_VAD is None:
        raise RuntimeError("STT engine not loaded")

    lang = language or _DEFAULT_LANGUAGE
    waveform = _pcm16_to_float32(pcm_bytes)
    duration_ms = int(waveform.size * 1000 / sample_rate) if sample_rate > 0 else 0

    started = time.perf_counter()
    if waveform.size == 0:
        return TranscriptResult(
            text="",
            model=_MODEL_NAME,
            language=lang,
            duration_ms=0,
            processing_ms=0,
            segments=[],
        )

    raw = _ASR_VAD.recognize(waveform, sample_rate=sample_rate, language=lang)
    elapsed_ms = int((time.perf_counter() - started) * 1000)

    if isinstance(raw, list):
        text = " ".join(_extract_text(item) for item in raw if _extract_text(item)).strip()
        segments: list[TranscriptSegment] = []
        for item in raw:
            segments.extend(_extract_segments(item))
    else:
        text = _extract_text(raw)
        segments = _extract_segments(raw)

    return TranscriptResult(
        text=text,
        model=_MODEL_NAME,
        language=lang,
        duration_ms=duration_ms,
        processing_ms=elapsed_ms,
        segments=segments,
    )
