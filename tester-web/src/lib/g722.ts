/**
 * ITU-T G.722 encode/decode (64 kbps, 16 kHz wideband).
 * Ported from SpanDSP g722.c (LGPL-2.1) — see third_party/spandsp.
 */

const INT16_MAX = 0x7fff;
const INT16_MIN = -0x8000;

const qmfCoeffsFwd = Int16Array.from([
  3, -11, 12, 32, -210, 951, 3876, -805, 362, -156, 53, -11,
]);
const qmfCoeffsRev = Int16Array.from([
  -11, 53, -156, 362, -805, 3876, 951, -210, 32, 12, -11, 3,
]);
const qm2 = Int16Array.from([-7408, -1616, 7408, 1616]);
const qm4 = Int16Array.from([
  0, -20456, -12896, -8968, -6288, -4240, -2584, -1200, 20456, 12896, 8968, 6288, 4240,
  2584, 1200, 0,
]);
const qm5 = Int16Array.from([
  -280, -280, -23352, -17560, -14120, -11664, -9752, -8184, -6864, -5712, -4696, -3784, -2960,
  -2208, -1520, -880, 23352, 17560, 14120, 11664, 9752, 8184, 6864, 5712, 4696, 3784, 2960,
  2208, 1520, 880, 280, -280,
]);
const qm6 = Int16Array.from([
  -136, -136, -136, -136, -24808, -21904, -19008, -16704, -14984, -13512, -12280, -11192, -10232,
  -9360, -8576, -7856, -7192, -6576, -6000, -5456, -4944, -4464, -4008, -3576, -3168, -2776, -2400,
  -2032, -1688, -1360, -1040, -728, 24808, 21904, 19008, 16704, 14984, 13512, 12280, 11192, 10232,
  9360, 8576, 7856, 7192, 6576, 6000, 5456, 4944, 4464, 4008, 3576, 3168, 2776, 2400, 2032, 1688,
  1360, 1040, 728, 432, 136, -432, -136,
]);
const q6 = Int16Array.from([
  0, 35, 72, 110, 150, 190, 233, 276, 323, 370, 422, 473, 530, 587, 650, 714, 786, 858, 940, 1023,
  1121, 1219, 1339, 1458, 1612, 1765, 1980, 2195, 2557, 2919, 0, 0,
]);
const ilb = Int16Array.from([
  2048, 2093, 2139, 2186, 2233, 2282, 2332, 2383, 2435, 2489, 2543, 2599, 2656, 2714, 2774, 2834,
  2896, 2960, 3025, 3091, 3158, 3228, 3298, 3371, 3444, 3520, 3597, 3676, 3756, 3838, 3922, 4008,
]);
const iln = Int16Array.from([
  0, 63, 62, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10,
  9, 8, 7, 6, 5, 4, 0,
]);
const ilp = Int16Array.from([
  0, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38,
  37, 36, 35, 34, 33, 32, 0,
]);
const ihn = Int16Array.from([0, 1, 0]);
const ihp = Int16Array.from([0, 3, 2]);
const wl = Int16Array.from([-60, -30, 58, 172, 334, 538, 1198, 3042]);
const rl42 = Int16Array.from([0, 7, 6, 5, 4, 3, 2, 1, 7, 6, 5, 4, 3, 2, 1, 0]);
const wh = Int16Array.from([0, -214, 798]);
const rh2 = Int16Array.from([2, 1, 2, 1]);

function saturate16(amp: number): number {
  if (amp > INT16_MAX) return INT16_MAX;
  if (amp < INT16_MIN) return INT16_MIN;
  return amp | 0;
}

function saturate15(amp: number): number {
  if (amp > 16383) return 16383;
  if (amp < -16384) return -16384;
  return amp | 0;
}

function satAdd16(x: number, y: number): number {
  return saturate16(x + y);
}

function satSub16(x: number, y: number): number {
  return saturate16(x - y);
}

function vecDotProdi16(x: Int16Array, y: Int16Array, n: number, xOff = 0, yOff = 0): number {
  let z = 0;
  for (let i = 0; i < n; i++) {
    z += x[xOff + i] * y[yOff + i];
  }
  return z;
}

function vecCircularDotProdi16(x: Int16Array, y: Int16Array, n: number, pos: number): number {
  return vecDotProdi16(x, y, n - pos, pos, 0) + vecDotProdi16(x, y, pos, 0, n - pos);
}

interface G722Band {
  nb: number;
  det: number;
  s: number;
  sz: number;
  r: number;
  p: [number, number];
  a: [number, number];
  b: [number, number, number, number, number, number];
  d: [number, number, number, number, number, number, number];
}

function newBand(): G722Band {
  return {
    nb: 0,
    det: 0,
    s: 0,
    sz: 0,
    r: 0,
    p: [0, 0],
    a: [0, 0],
    b: [0, 0, 0, 0, 0, 0],
    d: [0, 0, 0, 0, 0, 0, 0],
  };
}

function block4(s: G722Band, dx: number): void {
  const r = satAdd16(s.s, dx);
  const p = satAdd16(s.sz, dx);

  let wd1 = saturate16(s.a[0] << 2);
  let wd32 = (p ^ s.p[0]) & 0x8000 ? wd1 : -wd1;
  if (wd32 > 32767) wd32 = 32767;
  let wd3 =
    ((p ^ s.p[1]) & 0x8000 ? -128 : 128) + (wd32 >> 7) + ((s.a[1] * 32512) >> 15);
  if (Math.abs(wd3) > 12288) wd3 = wd3 < 0 ? -12288 : 12288;
  const ap1 = wd3;

  wd1 = (p ^ s.p[0]) & 0x8000 ? -192 : 192;
  let wd2 = (s.a[0] * 32640) >> 15;
  let ap0 = satAdd16(wd1, wd2);

  wd3 = satSub16(15360, ap1);
  if (Math.abs(ap0) > wd3) ap0 = ap0 < 0 ? -wd3 : wd3;

  wd1 = satAdd16(r, r);
  const spWd1 = (ap0 * wd1) >> 15;
  wd2 = satAdd16(s.r, s.r);
  const spWd2 = (ap1 * wd2) >> 15;
  const sp = satAdd16(spWd1, spWd2);

  s.r = r;
  s.a[1] = ap1;
  s.a[0] = ap0;
  s.p[1] = s.p[0];
  s.p[0] = p;

  wd1 = dx === 0 ? 0 : 128;
  s.d[0] = dx;
  let sz = 0;
  for (let i = 5; i >= 0; i--) {
    const wd2b = (s.d[i + 1] ^ dx) & 0x8000 ? -wd1 : wd1;
    const wd3b = (s.b[i] * 32640) >> 15;
    s.b[i] = satAdd16(wd2b, wd3b);
    const wd3c = satAdd16(s.d[i], s.d[i]);
    sz += (s.b[i] * wd3c) >> 15;
    s.d[i + 1] = s.d[i];
  }
  s.sz = saturate16(sz);
  s.s = satAdd16(sp, s.sz);
}

class G722Decoder {
  readonly bitsPerSample = 8;
  readonly eightK = false;
  readonly packed = false;
  readonly ituTestMode = false;
  readonly x = new Int16Array(12);
  readonly y = new Int16Array(12);
  ptr = 0;
  inBuffer = 0;
  inBits = 0;
  readonly band: [G722Band, G722Band] = [newBand(), newBand()];

  constructor() {
    this.band[0].det = 32;
    this.band[1].det = 8;
  }

  decode(encoded: Uint8Array): Int16Array {
    const amp: number[] = [];
    let rhigh = 0;

    for (let j = 0; j < encoded.length; ) {
      let code: number;
      if (this.packed) {
        if (this.inBits < this.bitsPerSample) {
          this.inBuffer |= encoded[j++] << this.inBits;
          this.inBits += 8;
        }
        code = this.inBuffer & ((1 << this.bitsPerSample) - 1);
        this.inBuffer >>= this.bitsPerSample;
        this.inBits -= this.bitsPerSample;
      } else {
        code = encoded[j++];
      }

      let wd1: number;
      let ihigh: number;
      let wd2: number;
      if (this.bitsPerSample === 8) {
        wd1 = code & 0x3f;
        ihigh = (code >> 6) & 0x03;
        wd2 = qm6[wd1];
        wd1 >>= 2;
      } else if (this.bitsPerSample === 7) {
        wd1 = code & 0x1f;
        ihigh = (code >> 5) & 0x03;
        wd2 = qm5[wd1];
        wd1 >>= 1;
      } else {
        wd1 = code & 0x0f;
        ihigh = (code >> 4) & 0x03;
        wd2 = qm4[wd1];
      }

      wd2 = (this.band[0].det * wd2) >> 15;
      const rlow = saturate15(this.band[0].s + wd2);

      wd2 = qm4[wd1];
      const dlow = (this.band[0].det * wd2) >> 15;

      let wd2b = rl42[wd1];
      wd1 = (this.band[0].nb * 127) >> 7;
      wd1 += wl[wd2b];
      if (wd1 < 0) wd1 = 0;
      else if (wd1 > 18432) wd1 = 18432;
      this.band[0].nb = wd1;

      wd1 = (this.band[0].nb >> 6) & 31;
      const wd2s = 8 - (this.band[0].nb >> 11);
      const wd3s = wd2s < 0 ? ilb[wd1] << -wd2s : ilb[wd1] >> wd2s;
      this.band[0].det = wd3s << 2;

      block4(this.band[0], dlow);

      if (!this.eightK) {
        wd2 = qm2[ihigh];
        const dhigh = (this.band[1].det * wd2) >> 15;
        rhigh = saturate15(dhigh + this.band[1].s);

        wd2 = rh2[ihigh];
        wd1 = (this.band[1].nb * 127) >> 7;
        wd1 += wh[wd2];
        if (wd1 < 0) wd1 = 0;
        else if (wd1 > 22528) wd1 = 22528;
        this.band[1].nb = wd1;

        wd1 = (this.band[1].nb >> 6) & 31;
        const wd2h = 10 - (this.band[1].nb >> 11);
        const wd3h = wd2h < 0 ? ilb[wd1] << -wd2h : ilb[wd1] >> wd2h;
        this.band[1].det = wd3h << 2;

        block4(this.band[1], dhigh);
      }

      if (this.ituTestMode) {
        amp.push(rlow << 1, rhigh << 1);
      } else if (this.eightK) {
        amp.push(rlow << 1);
      } else {
        this.x[this.ptr] = rlow + rhigh;
        this.y[this.ptr] = rlow - rhigh;
        this.ptr = (this.ptr + 1) % 12;
        amp.push(
          saturate16(vecCircularDotProdi16(this.y, qmfCoeffsRev, 12, this.ptr) >> 11),
          saturate16(vecCircularDotProdi16(this.x, qmfCoeffsFwd, 12, this.ptr) >> 11),
        );
      }
    }

    return Int16Array.from(amp);
  }
}

class G722Encoder {
  readonly bitsPerSample = 8;
  readonly eightK = false;
  readonly packed = false;
  readonly ituTestMode = false;
  readonly x = new Int16Array(12);
  readonly y = new Int16Array(12);
  ptr = 0;
  outBuffer = 0;
  outBits = 0;
  readonly band: [G722Band, G722Band] = [newBand(), newBand()];

  constructor() {
    this.band[0].det = 32;
    this.band[1].det = 8;
  }

  encode(samples: Int16Array): Uint8Array {
    const out: number[] = [];
    let xhigh = 0;

    for (let j = 0; j < samples.length; ) {
      let xlow: number;
      if (this.ituTestMode) {
        xlow = (xhigh = samples[j++] >> 1);
      } else if (this.eightK) {
        xlow = samples[j++] >> 1;
      } else {
        this.x[this.ptr] = samples[j++];
        this.y[this.ptr] = samples[j++];
        this.ptr = (this.ptr + 1) % 12;
        const sumodd = vecCircularDotProdi16(this.x, qmfCoeffsFwd, 12, this.ptr);
        const sumeven = vecCircularDotProdi16(this.y, qmfCoeffsRev, 12, this.ptr);
        xlow = (sumeven + sumodd) >> 14;
        xhigh = (sumeven - sumodd) >> 14;
      }

      const el = satSub16(xlow, this.band[0].s);
      let wd = el >= 0 ? el : ~el;
      let i = 1;
      for (; i < 30; i++) {
        const wd1 = (q6[i] * this.band[0].det) >> 12;
        if (wd < wd1) break;
      }
      const ilow = el < 0 ? iln[i] : ilp[i];

      const ril = ilow >> 2;
      let wd2 = qm4[ril];
      const dlow = (this.band[0].det * wd2) >> 15;

      const il4 = rl42[ril];
      wd = (this.band[0].nb * 127) >> 7;
      this.band[0].nb = wd + wl[il4];
      if (this.band[0].nb < 0) this.band[0].nb = 0;
      else if (this.band[0].nb > 18432) this.band[0].nb = 18432;

      let wd1 = (this.band[0].nb >> 6) & 31;
      let wd2s = 8 - (this.band[0].nb >> 11);
      let wd3 = wd2s < 0 ? ilb[wd1] << -wd2s : ilb[wd1] >> wd2s;
      this.band[0].det = wd3 << 2;

      block4(this.band[0], dlow);

      let code: number;
      if (this.eightK) {
        code = (0xc0 | ilow) >> (8 - this.bitsPerSample);
      } else {
        const eh = satSub16(xhigh, this.band[1].s);
        wd = eh >= 0 ? eh : ~eh;
        wd1 = (564 * this.band[1].det) >> 12;
        const mih = wd >= wd1 ? 2 : 1;
        const ihigh = eh < 0 ? ihn[mih] : ihp[mih];

        wd2 = qm2[ihigh];
        const dhigh = (this.band[1].det * wd2) >> 15;

        const ih2 = rh2[ihigh];
        wd = (this.band[1].nb * 127) >> 7;
        this.band[1].nb = wd + wh[ih2];
        if (this.band[1].nb < 0) this.band[1].nb = 0;
        else if (this.band[1].nb > 22528) this.band[1].nb = 22528;

        wd1 = (this.band[1].nb >> 6) & 31;
        wd2s = 10 - (this.band[1].nb >> 11);
        wd3 = wd2s < 0 ? ilb[wd1] << -wd2s : ilb[wd1] >> wd2s;
        this.band[1].det = wd3 << 2;

        block4(this.band[1], dhigh);
        code = ((ihigh << 6) | ilow) >> (8 - this.bitsPerSample);
      }

      if (this.packed) {
        this.outBuffer |= code << this.outBits;
        this.outBits += this.bitsPerSample;
        if (this.outBits >= 8) {
          out.push(this.outBuffer & 0xff);
          this.outBits -= 8;
          this.outBuffer >>= 8;
        }
      } else {
        out.push(code & 0xff);
      }
    }

    return Uint8Array.from(out);
  }
}

/** QMF pipeline delay (16 kHz samples) — alinhar decode ao PCM de entrada. */
export const G722_DECODER_DELAY_SAMPLES = 22;

export function encodeG722(samples: Int16Array): Uint8Array {
  if (!samples.length) return new Uint8Array(0);
  return new G722Encoder().encode(samples);
}

export function decodeG722(encoded: Uint8Array): Int16Array {
  if (!encoded.length) return new Int16Array(0);
  const raw = new G722Decoder().decode(encoded);
  const delay = G722_DECODER_DELAY_SAMPLES;
  if (raw.length <= delay) return raw;
  const out = new Int16Array(raw.length);
  out.set(raw.subarray(delay));
  return out;
}
