#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/rtp/g722_codec.hpp"
#include "voiceqas/rtp/g729_codec.hpp"

namespace voiceqas::rtp {
namespace {

std::vector<uint8_t> make_rtp_packet(uint16_t seq, uint32_t ts, PayloadType pt, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> packet(12 + payload.size());
    packet[0] = 0x80;
    packet[1] = static_cast<uint8_t>(pt);
    packet[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(seq & 0xFF);
    packet[4] = static_cast<uint8_t>((ts >> 24) & 0xFF);
    packet[5] = static_cast<uint8_t>((ts >> 16) & 0xFF);
    packet[6] = static_cast<uint8_t>((ts >> 8) & 0xFF);
    packet[7] = static_cast<uint8_t>(ts & 0xFF);
    for (size_t i = 0; i < payload.size(); ++i) {
        packet[12 + i] = payload[i];
    }
    return packet;
}

std::vector<int16_t> sine_16k(int samples, double freq_hz = 440.0) {
    std::vector<int16_t> pcm(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / 16000.0;
        pcm[static_cast<size_t>(i)] = static_cast<int16_t>(12000.0 * std::sin(2.0 * M_PI * freq_hz * t));
    }
    return pcm;
}

std::vector<int16_t> sine_8k(int samples, double freq_hz = 440.0) {
    std::vector<int16_t> pcm(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / 8000.0;
        pcm[static_cast<size_t>(i)] = static_cast<int16_t>(12000.0 * std::sin(2.0 * M_PI * freq_hz * t));
    }
    return pcm;
}

}  // namespace

TEST(RtpG722Test, RoundTripAndRtpDecode) {
    const auto pcm = sine_16k(320);
    const auto encoded = encode_g722(pcm);
    ASSERT_GT(encoded.size(), 0u);

    const auto decoded = decode_g722(encoded);
    ASSERT_EQ(decoded.size(), pcm.size());

    RtpDepacketizer depacketizer(8000);
    const auto packet = make_rtp_packet(1, 160, PayloadType::G722, encoded);
    const auto result = depacketizer.decode_packet(packet, PayloadType::G722);
    ASSERT_EQ(result.pcm.size(), pcm.size());
}

TEST(RtpG729Test, RoundTripAndRtpDecode) {
    const auto pcm = sine_8k(160);
    const auto encoded = encode_g729(pcm);
    ASSERT_GE(encoded.size(), 10u);

    const auto decoded = decode_g729(encoded);
    ASSERT_GE(decoded.size(), 80u);

    RtpDepacketizer depacketizer(8000);
    const auto packet = make_rtp_packet(1, 160, PayloadType::G729, encoded);
    const auto result = depacketizer.decode_packet(packet, PayloadType::G729);
    ASSERT_GE(result.pcm.size(), 80u);
}

}  // namespace voiceqas::rtp
