#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "voiceqas/analyzer.hpp"
#include "mocks/mock_ports.hpp"

namespace voiceqas {
namespace {

std::vector<int16_t> make_sine(int sample_rate, double freq, double seconds, double amplitude = 12000.0) {
    const size_t n = static_cast<size_t>(sample_rate * seconds);
    std::vector<int16_t> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sample_rate);
        out[i] = static_cast<int16_t>(amplitude * std::sin(2.0 * M_PI * freq * t));
    }
    return out;
}

}  // namespace

TEST(VqaSessionManagerPortsTest, PushPcmPublishesViaInjectedPort) {
    auto metrics = std::make_shared<test::MockMetricsPublisher>();
    auto telemetry = std::make_shared<test::MockPipelineTelemetry>();

    AnalyzerConfig cfg;
    cfg.window_ms = 500;
    cfg.frame_ms = 20;
    cfg.sample_rate = 8000;
    VqaSessionManager manager(cfg, {}, metrics, telemetry);

    const auto speech = make_sine(8000, 300.0, 1.0);
    const auto report = manager.push_pcm("sess-1", speech, 0, 8000);
    ASSERT_TRUE(report.has_value());
    ASSERT_EQ(metrics->vqa_sessions.size(), 1u);
    EXPECT_EQ(metrics->vqa_sessions.front(), "sess-1");
}

TEST(VqaSessionManagerPortsTest, DefaultCtorUsesNoopPorts) {
    AnalyzerConfig cfg;
    cfg.window_ms = 500;
    cfg.frame_ms = 20;
    cfg.sample_rate = 8000;
    VqaSessionManager manager(cfg);
    const auto speech = make_sine(8000, 300.0, 0.5);
    EXPECT_TRUE(manager.push_pcm("noop", speech, 0, 8000).has_value());
}

}  // namespace voiceqas
