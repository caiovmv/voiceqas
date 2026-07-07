#include <gtest/gtest.h>

#include <vector>

#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/rtp/g711_codec.hpp"

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

}  // namespace

TEST(RtpG711Test, DecodePcmuPacket) {
    RtpDepacketizer depacketizer(8000);
    const std::vector<uint8_t> payload = {0xFF, 0x7F, 0x00};
    const auto packet = make_rtp_packet(1, 160, PayloadType::Pcmu, payload);
    const auto result = depacketizer.decode_packet(packet, PayloadType::Pcmu);
    ASSERT_EQ(result.pcm.size(), 3);
    EXPECT_NE(result.pcm[0], 0);
}

TEST(RtpG711Test, DetectPacketLoss) {
    RtpDepacketizer depacketizer(8000);
    const std::vector<uint8_t> payload = {0x55};
    depacketizer.decode_packet(make_rtp_packet(1, 160, PayloadType::Pcmu, payload), PayloadType::Pcmu);
    const auto result = depacketizer.decode_packet(make_rtp_packet(3, 320, PayloadType::Pcmu, payload), PayloadType::Pcmu);
    EXPECT_GT(result.stats.packet_loss_pct, 0.0);
    EXPECT_EQ(result.stats.packets_lost, 1u);
}

TEST(G711Test, PcmuDecodeSilence) {
    const std::vector<uint8_t> encoded = {0xFF};
    const auto pcm = decode_g711_pcmu(encoded);
    ASSERT_EQ(pcm.size(), 1);
    EXPECT_NEAR(pcm[0], 0, 16);
}

TEST(G711Test, PcmuEncodeDecodeRoundTrip) {
    const std::vector<int16_t> original = {0, 1000, -2000, 8000, -12000};
    const auto encoded = encode_g711_pcmu(original);
    const auto decoded = decode_g711_pcmu(encoded);
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_NEAR(decoded[i], original[i], 512);
    }
}

TEST(G711Test, PcmaEncodeDecodeRoundTrip) {
    const std::vector<int16_t> original = {0, 500, -1500, 4000};
    const auto encoded = encode_g711_pcma(original);
    const auto decoded = decode_g711_pcma(encoded);
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_NEAR(decoded[i], original[i], 512);
    }
}

}  // namespace voiceqas::rtp
