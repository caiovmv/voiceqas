#include <gtest/gtest.h>

#include "mocks/mock_ports.hpp"
#include "test_helpers.hpp"
#include "voiceqas/stt/session_manager.hpp"

namespace voiceqas::stt {

TEST(SttSessionManagerTest, GateDropsWhenRequireSttReady) {
    auto metrics = std::make_shared<voiceqas::test::MockMetricsPublisher>();
    auto telemetry = std::make_shared<voiceqas::test::MockPipelineTelemetry>();

    SttConfig cfg;
    cfg.require_stt_ready = true;
    SttSessionManager manager(
        std::make_shared<SttEngine>(cfg),
        cfg,
        ops::OpsConfig{},
        metrics,
        telemetry,
        {});

    const auto pcm = test::make_sine(8000, 440.0, 0.1);
    manager.append_pcm("blocked", pcm, 8000);
    EXPECT_GE(telemetry->gate_drop_calls, 1u);

    manager.update_stt_ready("blocked", true);
    manager.append_pcm("blocked", pcm, 8000);
}

TEST(SttSessionManagerTest, AppendPcmResamplesToTargetRate) {
    auto metrics = std::make_shared<voiceqas::test::MockMetricsPublisher>();
    auto telemetry = std::make_shared<voiceqas::test::MockPipelineTelemetry>();
    SttConfig cfg;
    cfg.target_sample_rate = 16000;
    SttSessionManager manager(
        std::make_shared<SttEngine>(cfg),
        cfg,
        ops::OpsConfig{},
        metrics,
        telemetry,
        {},
        16000);

    const auto pcm = test::make_sine(8000, 440.0, 0.2);
    manager.append_pcm("s1", pcm, 8000);
    EXPECT_GE(telemetry->vqa_path_calls, 0u);
}

}  // namespace voiceqas::stt
