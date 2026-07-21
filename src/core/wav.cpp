#include "voiceqas/wav.hpp"

#include <cstring>

namespace voiceqas {

namespace {

bool read_u32(std::span<const uint8_t> data, size_t offset, uint32_t& out) {
    if (offset + 4 > data.size()) return false;
    std::memcpy(&out, data.data() + offset, 4);
    return true;
}

bool read_u16(std::span<const uint8_t> data, size_t offset, uint16_t& out) {
    if (offset + 2 > data.size()) return false;
    std::memcpy(&out, data.data() + offset, 2);
    return true;
}

}  // namespace

std::optional<WavInfo> parse_wav(std::span<const uint8_t> data) {
    if (data.size() < 44) {
        return std::nullopt;
    }
    if (std::string(reinterpret_cast<const char*>(data.data()), 4) != "RIFF") {
        return std::nullopt;
    }
    if (std::string(reinterpret_cast<const char*>(data.data() + 8), 4) != "WAVE") {
        return std::nullopt;
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    size_t data_offset = 0;
    uint32_t data_size = 0;

    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const std::string chunk_id(reinterpret_cast<const char*>(data.data() + pos), 4);
        uint32_t chunk_size = 0;
        if (!read_u32(data, pos + 4, chunk_size)) {
            break;
        }
        pos += 8;
        if (chunk_id == "fmt ") {
            read_u16(data, pos, audio_format);
            read_u16(data, pos + 2, channels);
            read_u32(data, pos + 4, sample_rate);
            read_u16(data, pos + 14, bits_per_sample);
        } else if (chunk_id == "data") {
            data_offset = pos;
            data_size = chunk_size;
            break;
        }
        pos += chunk_size;
    }

    if (audio_format != 1 || bits_per_sample != 16 || channels < 1) {
        return std::nullopt;
    }
    if (data_offset + data_size > data.size()) {
        return std::nullopt;
    }

    WavInfo info;
    info.sample_rate = static_cast<int>(sample_rate);
    info.channels = channels;
    const size_t sample_bytes = data_size / channels / 2;
    info.samples.resize(sample_bytes);
    for (size_t i = 0; i < sample_bytes; ++i) {
        int16_t sample = 0;
        std::memcpy(&sample, data.data() + data_offset + i * 2 * channels, 2);
        info.samples[i] = sample;
    }
    return info;
}

std::vector<uint8_t> write_wav_pcm16(std::span<const int16_t> samples, int sample_rate) {
    const int rate = sample_rate > 0 ? sample_rate : 8000;
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_bytes;
    std::vector<uint8_t> out(44 + data_bytes);
    auto write_u16 = [&](size_t off, uint16_t v) {
        out[off] = static_cast<uint8_t>(v & 0xFF);
        out[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };
    auto write_u32 = [&](size_t off, uint32_t v) {
        out[off] = static_cast<uint8_t>(v & 0xFF);
        out[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        out[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        out[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };
    std::memcpy(out.data(), "RIFF", 4);
    write_u32(4, riff_size);
    std::memcpy(out.data() + 8, "WAVE", 4);
    std::memcpy(out.data() + 12, "fmt ", 4);
    write_u32(16, 16);  // PCM fmt chunk size
    write_u16(20, 1);   // PCM
    write_u16(22, 1);   // mono
    write_u32(24, static_cast<uint32_t>(rate));
    write_u32(28, static_cast<uint32_t>(rate * 2));  // byte rate
    write_u16(32, 2);                                // block align
    write_u16(34, 16);                               // bits
    std::memcpy(out.data() + 36, "data", 4);
    write_u32(40, data_bytes);
    if (!samples.empty()) {
        std::memcpy(out.data() + 44, samples.data(), data_bytes);
    }
    return out;
}

}  // namespace voiceqas
