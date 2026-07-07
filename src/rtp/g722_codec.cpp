#include "voiceqas/rtp/g722_codec.hpp"

#include <cstdint>

extern "C" {
#include <stdint.h>
#include "spandsp/telephony.h"
#include "spandsp/g722.h"
}

namespace voiceqas::rtp {

struct G722Decoder::Impl {
    g722_decode_state_t* state = nullptr;

    Impl() { state = g722_decode_init(nullptr, 64000, 0); }
    ~Impl() {
        if (state) {
            g722_decode_free(state);
        }
    }
};

struct G722Encoder::Impl {
    g722_encode_state_t* state = nullptr;

    Impl() { state = g722_encode_init(nullptr, 64000, 0); }
    ~Impl() {
        if (state) {
            g722_encode_free(state);
        }
    }
};

G722Decoder::G722Decoder() : impl_(std::make_unique<Impl>()) {}
G722Decoder::~G722Decoder() = default;

std::vector<int16_t> G722Decoder::decode(std::span<const uint8_t> encoded) {
    if (!impl_ || !impl_->state || encoded.empty()) {
        return {};
    }
    std::vector<int16_t> pcm(encoded.size() * 2);
    const int samples = g722_decode(impl_->state, pcm.data(), encoded.data(), static_cast<int>(encoded.size()));
    pcm.resize(samples > 0 ? static_cast<size_t>(samples) : 0);
    return pcm;
}

G722Encoder::G722Encoder() : impl_(std::make_unique<Impl>()) {}
G722Encoder::~G722Encoder() = default;

std::vector<uint8_t> G722Encoder::encode(std::span<const int16_t> pcm) {
    if (!impl_ || !impl_->state || pcm.empty()) {
        return {};
    }
    std::vector<uint8_t> out(pcm.size() / 2 + 1);
    const int bytes = g722_encode(impl_->state, out.data(), pcm.data(), static_cast<int>(pcm.size()));
    out.resize(bytes > 0 ? static_cast<size_t>(bytes) : 0);
    return out;
}

std::vector<int16_t> decode_g722(std::span<const uint8_t> encoded) {
    G722Decoder decoder;
    return decoder.decode(encoded);
}

std::vector<uint8_t> encode_g722(std::span<const int16_t> pcm) {
    G722Encoder encoder;
    return encoder.encode(pcm);
}

}  // namespace voiceqas::rtp
