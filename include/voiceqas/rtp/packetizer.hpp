#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace voiceqas::rtp {

struct RtpHeader;

std::vector<uint8_t> build_rtp_packet(
    uint16_t sequence,
    uint32_t timestamp,
    uint8_t payload_type,
    std::span<const uint8_t> payload,
    uint32_t ssrc = 0x12345678);

class RtpPacketizer {
public:
    RtpPacketizer() = default;
    explicit RtpPacketizer(uint8_t payload_type, uint32_t timestamp_step, uint32_t ssrc = 0x12345678);

    std::vector<uint8_t> pack_payload(std::span<const uint8_t> encoded_payload);

    uint16_t sequence() const { return sequence_; }
    uint32_t timestamp() const { return timestamp_; }

private:
    uint8_t payload_type_ = 0;
    uint32_t timestamp_step_ = 160;
    uint32_t ssrc_ = 0x12345678;
    uint16_t sequence_ = 1;
    uint32_t timestamp_ = 0;
};

}  // namespace voiceqas::rtp
