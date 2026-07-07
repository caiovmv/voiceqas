#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace voiceqas::rtp {

constexpr int kG729FrameSamples = 80;
constexpr int kG729FrameBytes = 10;

class G729Decoder {
public:
    G729Decoder();
    ~G729Decoder();
    G729Decoder(const G729Decoder&) = delete;
    G729Decoder& operator=(const G729Decoder&) = delete;

    std::vector<int16_t> decode(std::span<const uint8_t> encoded);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class G729Encoder {
public:
    G729Encoder();
    ~G729Encoder();
    G729Encoder(const G729Encoder&) = delete;
    G729Encoder& operator=(const G729Encoder&) = delete;

    std::vector<uint8_t> encode(std::span<const int16_t> pcm);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<int16_t> decode_g729(std::span<const uint8_t> encoded);
std::vector<uint8_t> encode_g729(std::span<const int16_t> pcm);

}  // namespace voiceqas::rtp
