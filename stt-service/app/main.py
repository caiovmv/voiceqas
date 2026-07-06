from __future__ import annotations

import io
import wave
from typing import Any

from fastapi import FastAPI, HTTPException, Request
from pydantic import BaseModel, Field

from .engine import is_ready, load_engine, transcribe_pcm16

app = FastAPI(
    title="voiceqas-stt",
    description="Parakeet TDT 0.6B v3 pt-BR TAGARELA (ONNX) via onnx-asr",
    version="0.1.0",
)


class TranscribeJsonRequest(BaseModel):
    pcm_bytes: list[int] = Field(default_factory=list)
    sample_rate: int = 16000
    language: str = "pt"


class TranscriptSegmentJson(BaseModel):
    start_ms: int
    end_ms: int
    text: str


class TranscribeResponse(BaseModel):
    text: str
    model: str
    language: str
    duration_ms: int
    processing_ms: int
    segments: list[TranscriptSegmentJson]


@app.on_event("startup")
def startup() -> None:
    load_engine()


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/ready")
def ready() -> dict[str, Any]:
    return {
        "status": "ready" if is_ready() else "loading",
        "service": "voiceqas-stt",
        "model": "parakeet-tdt-0.6b-v3-ptBR-TAGARELA",
        "language": "pt",
    }


def _to_response(result: Any) -> TranscribeResponse:
    return TranscribeResponse(
        text=result.text,
        model=result.model,
        language=result.language,
        duration_ms=result.duration_ms,
        processing_ms=result.processing_ms,
        segments=[
            TranscriptSegmentJson(start_ms=s.start_ms, end_ms=s.end_ms, text=s.text)
            for s in result.segments
        ],
    )


def _wav_to_pcm16(data: bytes) -> tuple[bytes, int]:
    with wave.open(io.BytesIO(data), "rb") as wf:
        if wf.getnchannels() != 1:
            raise HTTPException(status_code=400, detail="WAV must be mono")
        if wf.getsampwidth() != 2:
            raise HTTPException(status_code=400, detail="WAV must be 16-bit PCM")
        sample_rate = wf.getframerate()
        return wf.readframes(wf.getnframes()), sample_rate


@app.post("/v1/transcribe", response_model=TranscribeResponse)
async def transcribe(request: Request) -> TranscribeResponse:
    if not is_ready():
        raise HTTPException(status_code=503, detail="STT engine loading")

    content_type = request.headers.get("content-type", "")

    if "audio/wav" in content_type or "audio/x-wav" in content_type:
        body = await request.body()
        pcm_bytes, sample_rate = _wav_to_pcm16(body)
        language = request.headers.get("x-language", "pt")
        return _to_response(transcribe_pcm16(pcm_bytes, sample_rate, language))

    if "application/json" in content_type:
        payload = TranscribeJsonRequest.model_validate(await request.json())
        pcm_bytes = bytes(payload.pcm_bytes)
        return _to_response(
            transcribe_pcm16(pcm_bytes, payload.sample_rate, payload.language)
        )

    if "application/octet-stream" in content_type:
        body = await request.body()
        sample_rate = int(request.headers.get("x-sample-rate", "16000"))
        language = request.headers.get("x-language", "pt")
        return _to_response(transcribe_pcm16(body, sample_rate, language))

    raise HTTPException(
        status_code=415,
        detail="Supported: audio/wav, application/json, application/octet-stream",
    )
