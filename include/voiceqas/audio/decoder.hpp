#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "voiceqas/metrics.hpp"
#include "voiceqas/rtp/depacketizer.hpp"

namespace voiceqas::audio {

struct DecodeResult {
    std::vector<int16_t> pcm;
    rtp::RtpStats rtp_stats{};
    bool ok = false;
    std::string error;
};

bool is_rtp_format(AudioFormat format);
AudioFormat format_from_rtp_payload_type(uint8_t payload_type);
rtp::PayloadType payload_type_for_format(AudioFormat format);
uint32_t rtp_timestamp_step_for_format(AudioFormat format, int frame_ms = 20);

DecodeResult decode_to_pcm(
    AudioFormat format,
    std::span<const uint8_t> payload,
    rtp::RtpDepacketizer* depacketizer);

/** Decode one or more RTP packets concatenated in a single buffer (REST batch). */
DecodeResult decode_rtp_concatenated(
    AudioFormat format,
    std::span<const uint8_t> data,
    rtp::RtpDepacketizer& depacketizer);

}  // namespace voiceqas::audio
