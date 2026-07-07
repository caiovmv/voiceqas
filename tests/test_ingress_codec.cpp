#include <gtest/gtest.h>

#include "voiceqas/audio/ingress_codec.hpp"

TEST(IngressCodecTest, AutodetectG722Preferred) {
    voiceqas::audio::MediaRelayConfig cfg;
    cfg.preferred_ingress_codec = "rtp_g722";
    const auto resolved = voiceqas::audio::resolve_ingress_codec(
        cfg, std::nullopt, voiceqas::AudioFormat::RtpG722);
    EXPECT_EQ(resolved.format, voiceqas::AudioFormat::RtpG722);
    EXPECT_FALSE(resolved.suboptimal);
}

TEST(IngressCodecTest, AutodetectPcmuSuboptimal) {
    voiceqas::audio::MediaRelayConfig cfg;
    cfg.preferred_ingress_codec = "rtp_g722";
    const auto resolved = voiceqas::audio::resolve_ingress_codec(
        cfg, std::nullopt, voiceqas::AudioFormat::RtpPcmu);
    EXPECT_EQ(resolved.format, voiceqas::AudioFormat::RtpPcmu);
    EXPECT_TRUE(resolved.suboptimal);
}

TEST(IngressCodecTest, RegisteredSuboptimal) {
    voiceqas::audio::MediaRelayConfig cfg;
    cfg.preferred_ingress_codec = "rtp_g722";
    const auto resolved = voiceqas::audio::resolve_ingress_codec(
        cfg, voiceqas::AudioFormat::RtpPcmu, voiceqas::AudioFormat::RtpPcmu);
    EXPECT_EQ(resolved.format, voiceqas::AudioFormat::RtpPcmu);
    EXPECT_TRUE(resolved.suboptimal);
}

TEST(IngressCodecTest, RegisteredVsRtpMismatch) {
    voiceqas::audio::MediaRelayConfig cfg;
    const auto resolved = voiceqas::audio::resolve_ingress_codec(
        cfg, voiceqas::AudioFormat::RtpG722, voiceqas::AudioFormat::RtpPcmu);
    EXPECT_EQ(resolved.format, voiceqas::AudioFormat::RtpG722);
    EXPECT_TRUE(resolved.suboptimal);
}
