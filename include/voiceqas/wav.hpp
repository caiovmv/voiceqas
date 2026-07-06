#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace voiceqas {

struct WavInfo {
    int sample_rate = 8000;
    int channels = 1;
    std::vector<int16_t> samples;
};

std::optional<WavInfo> parse_wav(std::span<const uint8_t> data);

}  // namespace voiceqas
