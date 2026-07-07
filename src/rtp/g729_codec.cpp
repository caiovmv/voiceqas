#include "voiceqas/rtp/g729_codec.hpp"

#include <algorithm>
#include <array>

extern "C" {
#include <bcg729/decoder.h>
#include <bcg729/encoder.h>
}

namespace voiceqas::rtp {

struct G729Decoder::Impl {
    bcg729DecoderChannelContextStruct* ctx = nullptr;

    Impl() { ctx = initBcg729DecoderChannel(); }
    ~Impl() {
        if (ctx) {
            closeBcg729DecoderChannel(ctx);
        }
    }
};

struct G729Encoder::Impl {
    bcg729EncoderChannelContextStruct* ctx = nullptr;

    Impl() { ctx = initBcg729EncoderChannel(0); }
    ~Impl() {
        if (ctx) {
            closeBcg729EncoderChannel(ctx);
        }
    }
};

G729Decoder::G729Decoder() : impl_(std::make_unique<Impl>()) {}
G729Decoder::~G729Decoder() = default;

std::vector<int16_t> G729Decoder::decode(std::span<const uint8_t> encoded) {
    if (!impl_ || !impl_->ctx || encoded.empty()) {
        return {};
    }

    std::vector<int16_t> pcm;
    pcm.reserve((encoded.size() / kG729FrameBytes) * kG729FrameSamples + kG729FrameSamples);

    for (size_t offset = 0; offset < encoded.size(); offset += kG729FrameBytes) {
        const size_t chunk = std::min<size_t>(kG729FrameBytes, encoded.size() - offset);
        std::array<int16_t, kG729FrameSamples> frame{};
        const uint8_t sid = chunk == 2 ? 1 : 0;
        bcg729Decoder(
            impl_->ctx,
            encoded.data() + offset,
            static_cast<uint8_t>(chunk),
            0,
            sid,
            0,
            frame.data());
        pcm.insert(pcm.end(), frame.begin(), frame.end());
    }
    return pcm;
}

G729Encoder::G729Encoder() : impl_(std::make_unique<Impl>()) {}
G729Encoder::~G729Encoder() = default;

std::vector<uint8_t> G729Encoder::encode(std::span<const int16_t> pcm) {
    if (!impl_ || !impl_->ctx || pcm.empty()) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve((pcm.size() / kG729FrameSamples) * kG729FrameBytes + kG729FrameBytes);

    for (size_t offset = 0; offset + kG729FrameSamples <= pcm.size(); offset += kG729FrameSamples) {
        std::array<uint8_t, kG729FrameBytes> frame{};
        uint8_t length = 0;
        bcg729Encoder(impl_->ctx, pcm.data() + offset, frame.data(), &length);
        if (length == 0) {
            continue;
        }
        out.insert(out.end(), frame.begin(), frame.begin() + length);
    }
    return out;
}

std::vector<int16_t> decode_g729(std::span<const uint8_t> encoded) {
    G729Decoder decoder;
    return decoder.decode(encoded);
}

std::vector<uint8_t> encode_g729(std::span<const int16_t> pcm) {
    G729Encoder encoder;
    return encoder.encode(pcm);
}

}  // namespace voiceqas::rtp
