#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "voiceqas/metrics.hpp"
#include "voiceqas/rtp/packetizer.hpp"

namespace voiceqas::audio {

struct EncodeResult {
    std::vector<uint8_t> encoded;
    std::vector<std::vector<uint8_t>> rtp_packets;
    bool ok = false;
    std::string error;
};

EncodeResult encode_from_pcm(
    AudioFormat format,
    std::span<const int16_t> pcm,
    rtp::RtpPacketizer* packetizer,
    int frame_ms = 20);

void apply_peak_limiter_inplace(std::span<int16_t> samples, double ceiling_dbfs = -3.0);

}  // namespace voiceqas::audio
