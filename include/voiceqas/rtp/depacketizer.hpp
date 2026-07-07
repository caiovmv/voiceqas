#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "voiceqas/rtp/g722_codec.hpp"
#include "voiceqas/rtp/g729_codec.hpp"

namespace voiceqas::rtp {

enum class PayloadType : uint8_t {
    Pcmu = 0,
    Pcma = 8,
    G722 = 9,
    G729 = 18,
};

int rtp_clock_rate_for_payload(PayloadType pt);

struct RtpHeader {
    uint8_t version = 0;
    uint8_t payload_type = 0;
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
    size_t header_size = 0;
};

struct RtpStats {
    double packet_loss_pct = 0.0;
    double jitter_ms = 0.0;
    uint32_t packets_received = 0;
    uint32_t packets_lost = 0;
};

std::optional<RtpHeader> parse_header(std::span<const uint8_t> packet);
std::span<const uint8_t> payload(std::span<const uint8_t> packet, const RtpHeader& header);

class RtpDepacketizer {
public:
    explicit RtpDepacketizer(int rtp_clock_rate = 8000);

    struct DecodeResult {
        std::vector<int16_t> pcm;
        RtpStats stats;
    };

    DecodeResult decode_packet(std::span<const uint8_t> packet, PayloadType expected);
    void reset();

private:
    int rtp_clock_rate_;
    bool has_sequence_ = false;
    uint16_t last_sequence_ = 0;
    bool has_timestamp_ = false;
    uint32_t last_timestamp_ = 0;
    uint32_t packets_received_ = 0;
    uint32_t packets_lost_ = 0;
    double jitter_ms_ = 0.0;
    std::unique_ptr<G722Decoder> g722_decoder_;
    std::unique_ptr<G729Decoder> g729_decoder_;
};

std::vector<int16_t> decode_g711_pcmu(std::span<const uint8_t> encoded);
std::vector<int16_t> decode_g711_pcma(std::span<const uint8_t> encoded);
std::vector<int16_t> decode_g722_payload(std::span<const uint8_t> encoded);
std::vector<int16_t> decode_g729_payload(std::span<const uint8_t> encoded);

}  // namespace voiceqas::rtp
