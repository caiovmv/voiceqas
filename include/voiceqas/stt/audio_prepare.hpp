#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "voiceqas/audio/config.hpp"
#include "voiceqas/metrics.hpp"
#include "voiceqas/rtp/depacketizer.hpp"

namespace voiceqas::stt {

struct PreparedAudio {
    std::vector<int16_t> pcm;
    int sample_rate = 16000;
    double decode_ms = 0.0;
    double agc_ms = 0.0;
    double enhancement_ms = 0.0;
    double resample_ms = 0.0;
    size_t payload_bytes = 0;
};

PreparedAudio prepare_audio_for_stt(
    AudioFormat format,
    std::span<const uint8_t> payload,
    int sample_rate,
    int target_sample_rate,
    const audio::AudioProcessingConfig& audio_config,
    rtp::RtpDepacketizer* persistent_depacketizer = nullptr);

std::vector<int16_t> resample_pcm16(
    std::span<const int16_t> input,
    int from_rate,
    int to_rate);

}  // namespace voiceqas::stt
