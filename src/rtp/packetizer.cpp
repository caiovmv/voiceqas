#include "voiceqas/rtp/packetizer.hpp"

#include <cstring>

namespace voiceqas::rtp {

std::vector<uint8_t> build_rtp_packet(
    uint16_t sequence,
    uint32_t timestamp,
    uint8_t payload_type,
    std::span<const uint8_t> payload,
    uint32_t ssrc) {
    std::vector<uint8_t> packet(12 + payload.size());
    packet[0] = 0x80;
    packet[1] = static_cast<uint8_t>(payload_type & 0x7F);
    packet[2] = static_cast<uint8_t>((sequence >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(sequence & 0xFF);
    packet[4] = static_cast<uint8_t>((timestamp >> 24) & 0xFF);
    packet[5] = static_cast<uint8_t>((timestamp >> 16) & 0xFF);
    packet[6] = static_cast<uint8_t>((timestamp >> 8) & 0xFF);
    packet[7] = static_cast<uint8_t>(timestamp & 0xFF);
    packet[8] = static_cast<uint8_t>((ssrc >> 24) & 0xFF);
    packet[9] = static_cast<uint8_t>((ssrc >> 16) & 0xFF);
    packet[10] = static_cast<uint8_t>((ssrc >> 8) & 0xFF);
    packet[11] = static_cast<uint8_t>(ssrc & 0xFF);
    std::memcpy(packet.data() + 12, payload.data(), payload.size());
    return packet;
}

RtpPacketizer::RtpPacketizer(uint8_t payload_type, uint32_t timestamp_step, uint32_t ssrc)
    : payload_type_(payload_type), timestamp_step_(timestamp_step), ssrc_(ssrc) {}

std::vector<uint8_t> RtpPacketizer::pack_payload(std::span<const uint8_t> encoded_payload) {
    auto packet = build_rtp_packet(sequence_++, timestamp_, payload_type_, encoded_payload, ssrc_);
    timestamp_ += timestamp_step_;
    return packet;
}

}  // namespace voiceqas::rtp
