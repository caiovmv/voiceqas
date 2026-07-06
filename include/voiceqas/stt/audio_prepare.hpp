#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "voiceqas/metrics.hpp"

namespace voiceqas::stt {

struct PreparedAudio {
    std::vector<int16_t> pcm;
    int sample_rate = 16000;
};

PreparedAudio prepare_audio_for_stt(
    AudioFormat format,
    std::span<const uint8_t> payload,
    int sample_rate,
    int target_sample_rate = 16000);

std::vector<int16_t> resample_pcm16(
    std::span<const int16_t> input,
    int from_rate,
    int to_rate);

}  // namespace voiceqas::stt
