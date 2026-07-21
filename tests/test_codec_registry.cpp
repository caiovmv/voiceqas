#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "voiceqas/audio/codec_registry.hpp"
#include "voiceqas/rtp/g711_codec.hpp"

namespace voiceqas::audio {
namespace {

std::vector<int16_t> make_sine(int n, double amp = 10000.0) {
    std::vector<int16_t> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        out[static_cast<size_t>(i)] =
            static_cast<int16_t>(amp * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / 8000.0));
    }
    return out;
}

}  // namespace

TEST(CodecRegistryTest, ResolvesAliases) {
    auto& registry = default_codec_registry();
    EXPECT_EQ(registry.format_from_string("g722"), AudioFormat::RtpG722);
    EXPECT_EQ(registry.format_from_string("rtp_g722"), AudioFormat::RtpG722);
}

TEST(CodecRegistryTest, G711RoundTripViaRegistryEncoder) {
    auto& registry = default_codec_registry();
    const auto pcm = make_sine(160);
    const auto encoded = registry.encode_pcm(AudioFormat::RtpPcmu, pcm);
    const auto decoded = rtp::decode_g711_pcmu(encoded);
    ASSERT_EQ(decoded.size(), pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        EXPECT_NEAR(decoded[i], pcm[i], 800);
    }
}

TEST(CodecRegistryTest, CustomCodecCanBeRegistered) {
    CodecRegistry registry;
    registry.register_codec(CodecDescriptor{
        .format = AudioFormat::RtpPcmu,
        .name = "fake_pcmu",
        .sample_rate = 8000,
        .encode_pcm = [](std::span<const int16_t> pcm) {
            return std::vector<uint8_t>(pcm.size(), 0x7F);
        },
    });
    const auto encoded = registry.encode_pcm(AudioFormat::RtpPcmu, std::vector<int16_t>{1, 2, 3});
    ASSERT_EQ(encoded.size(), 3u);
    EXPECT_EQ(encoded[0], 0x7F);
}

}  // namespace voiceqas::audio
