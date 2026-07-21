#include "voiceqas/rtp/g711_codec.hpp"

#include <algorithm>
#include <cstdint>

namespace voiceqas::rtp {

namespace {

constexpr int kClip = 32635;
constexpr int kBias = 0x84;

const int kSegEnd[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};

int search_segment(int val) {
    for (int i = 0; i < 8; ++i) {
        if (val <= kSegEnd[i]) {
            return i;
        }
    }
    return 8;
}

uint8_t linear_to_mulaw(int16_t sample) {
    uint8_t sign = 0;
    int val = sample;
    if (val < 0) {
        sign = 0x80;
        val = -val;
    }
    if (val > kClip) {
        val = kClip;
    }
    val += kBias;
    const int seg = search_segment(val);
    if (seg >= 8) {
        return static_cast<uint8_t>(0x7F ^ sign);
    }
    const int mantissa = (val >> (seg + 3)) & 0x0F;
    return static_cast<uint8_t>(~(sign | static_cast<uint8_t>(seg << 4) | static_cast<uint8_t>(mantissa)) & 0xFF);
}

uint8_t linear_to_alaw(int16_t sample) {
    uint8_t sign = 0x55;
    int val = sample;
    if (val < 0) {
        val = -val;
        sign = 0xD5;
    }
    if (val > kClip) {
        val = kClip;
    }
    int seg = 7;
    for (int i = 0; i < 8; ++i) {
        if (val <= kSegEnd[i]) {
            seg = i;
            break;
        }
    }
    int aval;
    if (seg < 2) {
        aval = (val >> 4) & 0x0F;
    } else {
        aval = (val >> (seg + 3)) & 0x0F;
    }
    return static_cast<uint8_t>((aval | (seg << 4)) ^ sign);
}

}  // namespace

std::vector<uint8_t> encode_g711_pcmu(std::span<const int16_t> pcm) {
    std::vector<uint8_t> out(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        out[i] = linear_to_mulaw(pcm[i]);
    }
    return out;
}

std::vector<uint8_t> encode_g711_pcma(std::span<const int16_t> pcm) {
    std::vector<uint8_t> out(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        out[i] = linear_to_alaw(pcm[i]);
    }
    return out;
}

}  // namespace voiceqas::rtp
