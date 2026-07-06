#include "voiceqas/rtp/g722_codec.hpp"

#include <cstdint>

extern "C" {
#include <stdint.h>
#include "spandsp/telephony.h"
#include "spandsp/g722.h"
}

namespace voiceqas::rtp {

namespace {

g722_decode_state_t* make_decoder() {
    auto* state = g722_decode_init(nullptr, 64000, 0);
    return state;
}

g722_encode_state_t* make_encoder() {
    auto* state = g722_encode_init(nullptr, 64000, 0);
    return state;
}

}  // namespace

std::vector<int16_t> decode_g722(std::span<const uint8_t> encoded) {
    if (encoded.empty()) {
        return {};
    }
    g722_decode_state_t* state = make_decoder();
    if (!state) {
        return {};
    }
    std::vector<int16_t> pcm(encoded.size() * 2);
    const int samples = g722_decode(state, pcm.data(), encoded.data(), static_cast<int>(encoded.size()));
    pcm.resize(samples > 0 ? static_cast<size_t>(samples) : 0);
    g722_decode_free(state);
    return pcm;
}

std::vector<uint8_t> encode_g722(std::span<const int16_t> pcm) {
    if (pcm.empty()) {
        return {};
    }
    g722_encode_state_t* state = make_encoder();
    if (!state) {
        return {};
    }
    std::vector<uint8_t> out(pcm.size() / 2 + 1);
    const int bytes = g722_encode(state, out.data(), pcm.data(), static_cast<int>(pcm.size()));
    out.resize(bytes > 0 ? static_cast<size_t>(bytes) : 0);
    g722_encode_free(state);
    return out;
}

}  // namespace voiceqas::rtp
