#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace voiceqas::test {

inline std::vector<int16_t> make_sine(int sample_rate, double freq, double seconds, double amplitude = 12000.0) {
    const size_t n = static_cast<size_t>(sample_rate * seconds);
    std::vector<int16_t> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sample_rate);
        out[i] = static_cast<int16_t>(amplitude * std::sin(2.0 * M_PI * freq * t));
    }
    return out;
}

inline std::vector<uint8_t> make_rtp_packet(uint8_t pt, std::span<const uint8_t> payload, uint16_t seq = 1) {
    std::vector<uint8_t> packet(12 + payload.size());
    packet[0] = 0x80;
    packet[1] = pt;
    packet[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(seq & 0xFF);
    std::memcpy(packet.data() + 12, payload.data(), payload.size());
    return packet;
}

}  // namespace voiceqas::test
