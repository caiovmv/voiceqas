#include "voiceqas/rtp/g729_codec.hpp"

#include <algorithm>
#include <array>

extern "C" {
#include <bcg729/decoder.h>
#include <bcg729/encoder.h>
}

namespace voiceqas::rtp {

std::vector<int16_t> decode_g729(std::span<const uint8_t> encoded) {
    if (encoded.empty()) {
        return {};
    }
    bcg729DecoderChannelContextStruct* ctx = initBcg729DecoderChannel();
    if (!ctx) {
        return {};
    }

    std::vector<int16_t> pcm;
    pcm.reserve((encoded.size() / kG729FrameBytes) * kG729FrameSamples + kG729FrameSamples);

    for (size_t offset = 0; offset < encoded.size(); offset += kG729FrameBytes) {
        const size_t chunk = std::min<size_t>(kG729FrameBytes, encoded.size() - offset);
        std::array<int16_t, kG729FrameSamples> frame{};
        const uint8_t sid = chunk == 2 ? 1 : 0;
        bcg729Decoder(
            ctx,
            encoded.data() + offset,
            static_cast<uint8_t>(chunk),
            0,
            sid,
            0,
            frame.data());
        pcm.insert(pcm.end(), frame.begin(), frame.end());
    }

    closeBcg729DecoderChannel(ctx);
    return pcm;
}

std::vector<uint8_t> encode_g729(std::span<const int16_t> pcm) {
    if (pcm.empty()) {
        return {};
    }
    bcg729EncoderChannelContextStruct* ctx = initBcg729EncoderChannel(0);
    if (!ctx) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve((pcm.size() / kG729FrameSamples) * kG729FrameBytes + kG729FrameBytes);

    for (size_t offset = 0; offset + kG729FrameSamples <= pcm.size(); offset += kG729FrameSamples) {
        std::array<uint8_t, kG729FrameBytes> frame{};
        uint8_t length = 0;
        bcg729Encoder(ctx, pcm.data() + offset, frame.data(), &length);
        if (length == 0) {
            continue;
        }
        out.insert(out.end(), frame.begin(), frame.begin() + length);
    }

    closeBcg729EncoderChannel(ctx);
    return out;
}

}  // namespace voiceqas::rtp
