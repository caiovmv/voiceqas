#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace voiceqas::rtp {

std::vector<int16_t> decode_g722(std::span<const uint8_t> encoded);
std::vector<uint8_t> encode_g722(std::span<const int16_t> pcm);

}  // namespace voiceqas::rtp
