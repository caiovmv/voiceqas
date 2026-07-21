#include <gtest/gtest.h>

#include "voiceqas/rtp/packetizer.hpp"

namespace voiceqas {
namespace {

TEST(RtpPacketizerTest, PacksPayloadWithIncrementingSeq) {
    rtp::RtpPacketizer packetizer(rtp::PayloadType::Pcmu, 160);
    const std::vector<uint8_t> payload(160, 0x55);
    const auto p1 = packetizer.pack_payload(payload);
    const auto p2 = packetizer.pack_payload(payload);
    ASSERT_GE(p1.size(), 12u);
    ASSERT_GE(p2.size(), 12u);
    EXPECT_NE(p1[2], p2[2]);
}

TEST(RtpPacketizerTest, AdvancesTimestampByStep) {
    rtp::RtpPacketizer packetizer(rtp::PayloadType::Pcmu, 160);
    const std::vector<uint8_t> payload(80, 0);
    const auto p1 = packetizer.pack_payload(payload);
    const auto p2 = packetizer.pack_payload(payload);
    const uint32_t ts1 = (static_cast<uint32_t>(p1[4]) << 24) | (static_cast<uint32_t>(p1[5]) << 16) |
                         (static_cast<uint32_t>(p1[6]) << 8) | static_cast<uint32_t>(p1[7]);
    const uint32_t ts2 = (static_cast<uint32_t>(p2[4]) << 24) | (static_cast<uint32_t>(p2[5]) << 16) |
                         (static_cast<uint32_t>(p2[6]) << 8) | static_cast<uint32_t>(p2[7]);
    EXPECT_EQ(ts2 - ts1, 160u);
}

}  // namespace
}  // namespace voiceqas
