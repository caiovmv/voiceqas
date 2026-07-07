#pragma once

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace voiceqas::audio {

// Band-limited sinc resampler (windowed) for telephony 8/16 kHz paths.
std::vector<int16_t> resample_sinc_pcm16(
    std::span<const int16_t> input,
    int from_rate,
    int to_rate);

}  // namespace voiceqas::audio
