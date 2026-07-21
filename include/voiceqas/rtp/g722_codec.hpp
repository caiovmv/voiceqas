#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace voiceqas::rtp {

class G722Decoder {
public:
    G722Decoder();
    ~G722Decoder();
    G722Decoder(const G722Decoder&) = delete;
    G722Decoder& operator=(const G722Decoder&) = delete;

    std::vector<int16_t> decode(std::span<const uint8_t> encoded);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class G722Encoder {
public:
    G722Encoder();
    ~G722Encoder();
    G722Encoder(const G722Encoder&) = delete;
    G722Encoder& operator=(const G722Encoder&) = delete;

    std::vector<uint8_t> encode(std::span<const int16_t> pcm);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<int16_t> decode_g722(std::span<const uint8_t> encoded);
std::vector<uint8_t> encode_g722(std::span<const int16_t> pcm);

}  // namespace voiceqas::rtp
