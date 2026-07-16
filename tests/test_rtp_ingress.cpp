#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "voiceqas/audio/config.hpp"
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
    EXPECT_TRUE(frame->normalized);
    EXPECT_GE(frame->agc_ms, 0.0);
}

TEST(RtpIngressTest, SharedAgcBoostsQuietSpeech) {
    audio::AudioProcessingConfig cfg;
    cfg.strip.nr.enabled = false;
    cfg.strip.hpf.enabled = false;
    cfg.strip.eq.enabled = false;
    cfg.strip.deesser.enabled = false;
    cfg.strip.compressor.enabled = false;
    cfg.strip.limiter.enabled = true;
    cfg.strip.limiter.ceiling_dbfs = -1.0;
    cfg.strip.agc.enabled = true;
    cfg.strip.agc.target_rms_dbfs = -20.0;
    cfg.strip.agc.max_gain_db = 24.0;
    cfg.sync_legacy_from_strip();
    RtpIngressProcessor ingress(cfg);

    // Quiet tone (~ few hundred amplitude) should be amplified toward target.
    std::vector<int16_t> quiet(160);
    for (size_t i = 0; i < quiet.size(); ++i) {
        quiet[i] = static_cast<int16_t>(200 * ((i % 2 == 0) ? 1 : -1));
    }
    const auto encoded = rtp::encode_g711_pcmu(quiet);
    const auto packet = make_rtp_packet(0, encoded);
    const auto frame = ingress.decode("quiet", AudioFormat::RtpPcmu, packet);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(frame->normalized);

    int16_t peak_in = 0;
    int16_t peak_out = 0;
    for (auto s : quiet) {
        peak_in = std::max(peak_in, static_cast<int16_t>(std::abs(s)));
    }
    for (auto s : frame->pcm) {
        peak_out = std::max(peak_out, static_cast<int16_t>(std::abs(s)));
    }
    EXPECT_GT(peak_out, peak_in);
}

TEST(RtpIngressTest, AgcCanBeDisabled) {
    audio::AudioProcessingConfig cfg;
    cfg.strip.nr.enabled = false;
    cfg.strip.hpf.enabled = false;
    cfg.strip.eq.enabled = false;
    cfg.strip.deesser.enabled = false;
    cfg.strip.compressor.enabled = false;
    cfg.strip.limiter.enabled = false;
    cfg.strip.agc.enabled = false;
    cfg.sync_legacy_from_strip();
    RtpIngressProcessor ingress(cfg);

    const auto pcm = std::vector<int16_t>{1000, -1000, 500, -500};
    const auto encoded = rtp::encode_g711_pcmu(pcm);
    const auto packet = make_rtp_packet(0, encoded);
    const auto frame = ingress.decode("no-agc", AudioFormat::RtpPcmu, packet);
    ASSERT_TRUE(frame.has_value());
    EXPECT_FALSE(frame->normalized);
    EXPECT_DOUBLE_EQ(frame->agc_ms, 0.0);
    ASSERT_EQ(frame->pcm.size(), pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        EXPECT_NEAR(frame->pcm[i], pcm[i], 64);  // G.711 round-trip tolerance
    }
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
