#include <gtest/gtest.h>

#include "voiceqas/stt_gate.hpp"

namespace voiceqas {
namespace {

WindowMetrics make_metrics(double score, double silence, double clipping, double snr) {
    WindowMetrics m;
    m.composite_score = score;
    m.silence_ratio = silence;
    m.clipping_ratio = clipping;
    m.snr_estimate_db = snr;
    return m;
}

}  // namespace

TEST(SttGateTest, RejectsHighSilence) {
    AnalyzerConfig cfg;
    SttGate gate(cfg);
    auto m = make_metrics(90, 0.9, 0.0, 20);
    gate.evaluate(m);
    EXPECT_FALSE(m.stt_ready);
}

TEST(SttGateTest, RejectsHighClipping) {
    AnalyzerConfig cfg;
    SttGate gate(cfg);
    auto m = make_metrics(90, 0.1, 0.5, 20);
    gate.evaluate(m);
    EXPECT_FALSE(m.stt_ready);
}

}  // namespace voiceqas
