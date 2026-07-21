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

/** Mono PCM16 LE WAVE (44-byte header + samples). */
std::vector<uint8_t> write_wav_pcm16(std::span<const int16_t> samples, int sample_rate);

}  // namespace voiceqas
