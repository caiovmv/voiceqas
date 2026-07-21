import { apiBase } from './client';
import { headersFromMix } from './process-headers';
import type { PipelineMixState } from '../domain/pipeline-mix';

export interface ProcessedAudioWav {
  blob: Blob;
  sampleRate: number;
  url: string;
  pcm: Int16Array;
}

function wavBlobToPcm16(buf: ArrayBuffer): { pcm: Int16Array; sampleRate: number } {
  const u8 = new Uint8Array(buf);
  if (u8.length < 44 || u8[0] !== 0x52) {
    throw new Error('invalid wav');
  }
  const view = new DataView(buf);
  let pos = 12;
  let sampleRate = 16000;
  let dataOffset = 44;
  let dataSize = 0;
  while (pos + 8 <= u8.length) {
    const id = String.fromCharCode(u8[pos], u8[pos + 1], u8[pos + 2], u8[pos + 3]);
    const size = view.getUint32(pos + 4, true);
    pos += 8;
    if (id === 'fmt ') {
      sampleRate = view.getUint32(pos + 4, true);
    } else if (id === 'data') {
      dataOffset = pos;
      dataSize = size;
      break;
    }
    pos += size;
  }
  const samples = dataSize >> 1;
  const pcm = new Int16Array(samples);
  for (let i = 0; i < samples; i++) {
    pcm[i] = view.getInt16(dataOffset + i * 2, true);
  }
  return { pcm, sampleRate };
}

/** Server VoiceChannelStrip -> WAV (full timeline, no Silero mask). */
export async function processAudioStripWav(
  pcm: Int16Array,
  sampleRate: number,
  mix: PipelineMixState,
): Promise<ProcessedAudioWav> {
  const format = sampleRate >= 16000 ? 'pcm_s16le_16k' : 'pcm_s16le_8k';
  const body = new Uint8Array(pcm.buffer, pcm.byteOffset, pcm.byteLength);
  const res = await fetch(`${apiBase}/v1/tools/process-audio`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/octet-stream',
      'X-Audio-Format': format,
      'X-Sample-Rate': String(sampleRate),
      'X-Session-Id': `lab-strip-${Date.now()}`,
      ...headersFromMix(mix),
    },
    body,
  });
  if (!res.ok) {
    throw new Error(await res.text());
  }
  const blob = await res.blob();
  const ab = await blob.arrayBuffer();
  const decoded = wavBlobToPcm16(ab);
  const srHeader = res.headers.get('X-Sample-Rate');
  const outRate = srHeader ? Number(srHeader) : decoded.sampleRate;
  return {
    blob,
    sampleRate: outRate,
    url: URL.createObjectURL(blob),
    pcm: decoded.pcm,
  };
}
