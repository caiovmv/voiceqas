#include <gtest/gtest.h>

#include "voiceqas/ops/prometheus.hpp"

namespace voiceqas::ops {
namespace {

TEST(PrometheusMetricsTest, SnapshotReflectsVqaWindows) {
    auto& metrics = PrometheusMetrics::instance();
    metrics.on_vqa_window("prom-s1", true, 0.82, 18.0, 0.1);
    metrics.on_vqa_window("prom-s2", false, 0.55, 12.0, 0.3);

    const auto snap = metrics.snapshot();
    EXPECT_DOUBLE_EQ(snap.composite_score, 0.55);
    EXPECT_DOUBLE_EQ(snap.snr_db, 12.0);
    EXPECT_DOUBLE_EQ(snap.silence_ratio, 0.3);
    EXPECT_EQ(snap.active_sessions, 2);
    EXPECT_EQ(snap.stt_ready_sessions, 1);

    metrics.on_session_removed("prom-s1");
    metrics.on_session_removed("prom-s2");
}

TEST(PrometheusMetricsTest, RenderIncludesPipelineCounters) {
    auto& metrics = PrometheusMetrics::instance();
    metrics.set_pipeline_active_sessions(3);
    metrics.set_pipeline_finished_sessions(2, 42);
    metrics.update_pipeline_stage(PipelinePrometheusUpdate{
        .stage = "rtp_ingress",
        .direction = "inbound",
        .bytes = 1024,
        .latency_ms = 1.5,
        .jitter_ms = 0.2,
    });

    const auto text = metrics.render_prometheus(metrics.snapshot());
    EXPECT_NE(text.find("voiceqas_pipeline_active_sessions"), std::string::npos);
    EXPECT_NE(text.find("voiceqas_pipeline_bytes_total"), std::string::npos);
    EXPECT_NE(text.find("rtp_ingress"), std::string::npos);

    metrics.reset_pipeline_fleet();
    metrics.set_pipeline_active_sessions(0);
    metrics.set_pipeline_finished_sessions(0, 0);
}

}  // namespace
}  // namespace voiceqas::ops
