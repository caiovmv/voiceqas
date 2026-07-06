export function buildRtpPacket(
  sequence: number,
  timestamp: number,
  payload: Uint8Array,
  payloadType: 0 | 8 | 9 | 18,
  ssrc = 0x12345678,
): Uint8Array {
  const packet = new Uint8Array(12 + payload.length);
  packet[0] = 0x80;
  packet[1] = payloadType & 0x7f;
  packet[2] = (sequence >> 8) & 0xff;
  packet[3] = sequence & 0xff;
  packet[4] = (timestamp >> 24) & 0xff;
  packet[5] = (timestamp >> 16) & 0xff;
  packet[6] = (timestamp >> 8) & 0xff;
  packet[7] = timestamp & 0xff;
  packet[8] = (ssrc >> 24) & 0xff;
  packet[9] = (ssrc >> 16) & 0xff;
  packet[10] = (ssrc >> 8) & 0xff;
  packet[11] = ssrc & 0xff;
  packet.set(payload, 12);
  return packet;
}

export function extractRtpPayload(packet: Uint8Array): Uint8Array {
  if (packet.length < 12) return packet;
  const cc = packet[0] & 0x0f;
  const headerSize = 12 + cc * 4;
  return packet.subarray(headerSize);
}

export function chunkRtpFrames(
  payloads: Uint8Array[],
  payloadType: 0 | 8,
  samplesPerFrame: number,
): Uint8Array[] {
  const packets: Uint8Array[] = [];
  let seq = 1;
  let ts = 0;
  for (const payload of payloads) {
    packets.push(buildRtpPacket(seq++, ts, payload, payloadType));
    ts += samplesPerFrame;
  }
  return packets;
}
