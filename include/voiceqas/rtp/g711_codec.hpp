#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace voiceqas::rtp {

std::vector<uint8_t> encode_g711_pcmu(std::span<const int16_t> pcm);
std::vector<uint8_t> encode_g711_pcma(std::span<const int16_t> pcm);

}  // namespace voiceqas::rtp
