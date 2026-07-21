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
    cfg.max_silence_ratio_for_ready = 0.40;
    cfg.hysteresis_ok_windows = 1;
    SttGate gate(cfg);
    auto m = make_metrics(90, 0.9, 0.0, 20);
    // Overwrite after evaluate will recompute quality; seed inputs only.
    m.silence_ratio = 0.9;
    m.clipping_ratio = 0.0;
    m.snr_estimate_db = 20;
    m.rms_dbfs = -20;
    m.spectral_flatness = 0.2;
    gate.evaluate(m);
    EXPECT_FALSE(m.stt_ready);
    // Quality score ignores silence; presence fails the gate.
    EXPECT_GT(m.speech_quality_score, 60.0);
}

TEST(SttGateTest, AcceptsBorderlinePresence) {
    AnalyzerConfig cfg;
    cfg.max_silence_ratio_for_ready = 0.40;
    cfg.stt_ready_threshold = 50.0;
    cfg.hysteresis_ok_windows = 1;
    SttGate gate(cfg);
    WindowMetrics m;
    m.silence_ratio = 0.35;
    m.clipping_ratio = 0.0;
    m.snr_estimate_db = 20;
    m.rms_dbfs = -20;
    m.spectral_flatness = 0.2;
    EXPECT_TRUE(gate.evaluate(m));
}

TEST(SttGateTest, RejectsHighClipping) {
    AnalyzerConfig cfg;
    cfg.hysteresis_ok_windows = 1;
    SttGate gate(cfg);
    WindowMetrics m;
    m.silence_ratio = 0.1;
    m.clipping_ratio = 0.5;
    m.snr_estimate_db = 20;
    m.rms_dbfs = -20;
    m.spectral_flatness = 0.2;
    gate.evaluate(m);
    EXPECT_FALSE(m.stt_ready);
}

}  // namespace voiceqas
