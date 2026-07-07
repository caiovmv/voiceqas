#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "voiceqas/media/rtp_ingress.hpp"
#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/rtp/g711_codec.hpp"

namespace voiceqas::media {
namespace {

std::vector<uint8_t> make_rtp_packet(uint8_t pt, std::span<const uint8_t> payload, uint16_t seq = 1) {
    std::vector<uint8_t> packet(12 + payload.size());
    packet[0] = 0x80;
    packet[1] = pt;
    packet[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(seq & 0xFF);
    std::memcpy(packet.data() + 12, payload.data(), payload.size());
    return packet;
}

}  // namespace

TEST(RtpIngressTest, DecodesSingleG711Packet) {
    const auto pcm = std::vector<int16_t>{1000, -1000, 500, -500};
    const auto encoded = rtp::encode_g711_pcmu(pcm);
    const auto packet = make_rtp_packet(0, encoded);

    RtpIngressProcessor ingress;
    const auto frame = ingress.decode("sess-a", AudioFormat::RtpPcmu, packet);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->ok);
    ASSERT_EQ(frame->pcm.size(), pcm.size());
    EXPECT_EQ(frame->sample_rate, 8000);
}

TEST(RtpIngressTest, StatefulPerSession) {
    RtpIngressProcessor ingress;
    const auto encoded = rtp::encode_g711_pcmu(std::vector<int16_t>{100, 200});
    const auto p1 = make_rtp_packet(0, encoded, 1);
    const auto p2 = make_rtp_packet(0, encoded, 2);
    ASSERT_TRUE(ingress.decode("s1", AudioFormat::RtpPcmu, p1).has_value());
    ASSERT_TRUE(ingress.decode("s1", AudioFormat::RtpPcmu, p2).has_value());
    ingress.remove_session("s1");
}

}  // namespace voiceqas::media
