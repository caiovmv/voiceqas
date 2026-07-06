#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace voiceqas::rtp {

constexpr int kG729FrameSamples = 80;
constexpr int kG729FrameBytes = 10;

std::vector<int16_t> decode_g729(std::span<const uint8_t> encoded);
std::vector<uint8_t> encode_g729(std::span<const int16_t> pcm);

}  // namespace voiceqas::rtp
