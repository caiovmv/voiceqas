#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "voiceqas/wav.hpp"

namespace voiceqas {
namespace {

std::vector<uint8_t> make_minimal_wav(int sample_rate, const std::vector<int16_t>& samples) {
    const uint32_t data_size = static_cast<uint32_t>(samples.size() * 2);
    const uint32_t riff_size = 36 + data_size;
    std::vector<uint8_t> wav(44 + data_size);
    auto write = [&](size_t offset, const void* ptr, size_t len) {
        std::memcpy(wav.data() + offset, ptr, len);
    };

    write(0, "RIFF", 4);
    write(4, &riff_size, 4);
    write(8, "WAVE", 4);
    write(12, "fmt ", 4);
    const uint32_t fmt_size = 16;
    write(16, &fmt_size, 4);
    const uint16_t audio_format = 1;
    const uint16_t channels = 1;
    const uint32_t byte_rate = static_cast<uint32_t>(sample_rate * 2);
    const uint16_t block_align = 2;
    const uint16_t bits = 16;
    write(20, &audio_format, 2);
    write(22, &channels, 2);
    write(24, &sample_rate, 4);
    write(28, &byte_rate, 4);
    write(32, &block_align, 2);
    write(34, &bits, 2);
    write(36, "data", 4);
    write(40, &data_size, 4);
    write(44, samples.data(), data_size);
    return wav;
}

}  // namespace

TEST(WavTest, ParseMinimalWav) {
    const std::vector<int16_t> samples = {100, -100, 200, -200};
    const auto wav = make_minimal_wav(8000, samples);
    const auto parsed = parse_wav(wav);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->sample_rate, 8000);
    EXPECT_EQ(parsed->samples.size(), 4);
}

}  // namespace voiceqas
