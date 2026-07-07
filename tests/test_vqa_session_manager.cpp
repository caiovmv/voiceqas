#include <gtest/gtest.h>

#include "mocks/mock_ports.hpp"
#include "test_helpers.hpp"
#include "voiceqas/analyzer.hpp"
#include "voiceqas/rtp/g711_codec.hpp"

namespace voiceqas {

TEST(VqaSessionManagerTest, PushFrameDecodesRtp) {
    auto metrics = std::make_shared<test::MockMetricsPublisher>();
    auto telemetry = std::make_shared<test::MockPipelineTelemetry>();

    AnalyzerConfig cfg;
    cfg.window_ms = 500;
    cfg.frame_ms = 20;
    cfg.sample_rate = 8000;
    VqaSessionManager manager(cfg, {}, metrics, telemetry);

    const auto pcm = std::vector<int16_t>{1000, -1000, 500, -500};
    const auto encoded = rtp::encode_g711_pcmu(pcm);
    const auto packet = test::make_rtp_packet(0, encoded);

    const auto report = manager.push_frame("rtp-1", AudioFormat::RtpPcmu, packet, 0);
    EXPECT_GE(telemetry->rtp_ingress_calls, 1u);
    if (report) {
        EXPECT_GE(metrics->vqa_sessions.size(), 1u);
    }
}

TEST(VqaSessionManagerTest, RemoveSessionClearsState) {
    VqaSessionManager manager(AnalyzerConfig{});
    const auto speech = test::make_sine(8000, 300.0, 0.5);
    ASSERT_TRUE(manager.push_pcm("x", speech, 0, 8000).has_value());
    manager.remove_session("x");
    EXPECT_TRUE(manager.push_pcm("x", speech, 0, 8000).has_value());
}

}  // namespace voiceqas
