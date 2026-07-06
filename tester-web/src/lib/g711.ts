const BIAS = 0x84;
const CLIP = 32635;

function searchSegment(val: number, table: number[]): number {
  for (let i = 0; i < table.length; i++) {
    if (val <= table[i]) return i;
  }
  return table.length;
}

const SEG_END = [
  0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF,
];

export function linearToMulaw(sample: number): number {
  const sign = sample < 0 ? 0x80 : 0;
  if (sample < 0) sample = -sample;
  if (sample > CLIP) sample = CLIP;
  sample += BIAS;
  const seg = searchSegment(sample, SEG_END);
  if (seg >= 8) return 0x7F ^ sign;
  const mantissa = (sample >> (seg + 3)) & 0x0F;
  return ~(sign | (seg << 4) | mantissa) & 0xFF;
}

export function linearToAlaw(sample: number): number {
  let sign = 0x55;
  if (sample < 0) {
    sample = -sample;
    sign = 0xD5;
  }
  if (sample > 32635) sample = 32635;
  let seg = 7;
  for (let i = 0; i < 8; i++) {
    if (sample <= SEG_END[i]) {
      seg = i;
      break;
    }
  }
  let aval: number;
  if (seg < 2) {
    aval = (sample >> 4) & 0x0F;
  } else {
    aval = (sample >> (seg + 3)) & 0x0F;
  }
  return (aval | (seg << 4)) ^ sign;
}

export function encodePcmu(samples: Int16Array): Uint8Array {
  const out = new Uint8Array(samples.length);
  for (let i = 0; i < samples.length; i++) {
    out[i] = linearToMulaw(samples[i]);
  }
  return out;
}

export function encodePcma(samples: Int16Array): Uint8Array {
  const out = new Uint8Array(samples.length);
  for (let i = 0; i < samples.length; i++) {
    out[i] = linearToAlaw(samples[i]);
  }
  return out;
}

const ULAW_TABLE = (() => {
  const t = new Int16Array(256);
  for (let i = 0; i < 256; i++) {
    const u = ~i & 0xff;
    const sign = u & 0x80 ? -1 : 1;
    const seg = (u >> 4) & 0x07;
    const mant = u & 0x0f;
    let val = ((mant << 3) + 0x84) << seg;
    val -= 0x84;
    t[i] = sign * val;
  }
  return t;
})();

export function decodePcmu(encoded: Uint8Array): Int16Array {
  const out = new Int16Array(encoded.length);
  for (let i = 0; i < encoded.length; i++) {
    out[i] = ULAW_TABLE[encoded[i]];
  }
  return out;
}

export function decodePcma(encoded: Uint8Array): Int16Array {
  const out = new Int16Array(encoded.length);
  for (let i = 0; i < encoded.length; i++) {
    let a = encoded[i] ^ 0x55;
    const sign = a & 0x80 ? -1 : 1;
    const seg = (a >> 4) & 0x07;
    const mant = a & 0x0f;
    let val = seg < 2 ? (mant << 4) + 8 : ((mant << 4) + 0x108) << (seg - 1);
    out[i] = sign * val;
  }
  return out;
}
