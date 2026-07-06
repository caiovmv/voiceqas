#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "voiceqas/analyzer.hpp"
#include "voiceqas/stt_gate.hpp"

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

std::vector<int16_t> make_silence(int sample_rate, double seconds) {
    return std::vector<int16_t>(static_cast<size_t>(sample_rate * seconds), 0);
}

std::vector<int16_t> make_clipped(int sample_rate, double seconds) {
    auto out = make_sine(sample_rate, 440.0, seconds, 32000.0);
    for (auto& s : out) {
        s = s > 0 ? 32000 : -32000;
    }
    return out;
}

}  // namespace

TEST(AnalyzerTest, SilenceHasLowScore) {
    AnalyzerConfig cfg;
    cfg.window_ms = 500;
    cfg.frame_ms = 20;
    VoiceAnalyzer analyzer(cfg);
    const auto silence = make_silence(8000, 1.0);
    const auto result = analyzer.analyze_pcm_batch(silence, 8000);
    EXPECT_LT(result.composite_score, 50.0);
    EXPECT_FALSE(result.stt_ready);
}

TEST(AnalyzerTest, CleanSpeechCanBeReady) {
    AnalyzerConfig cfg;
    cfg.window_ms = 500;
    cfg.frame_ms = 20;
    cfg.stt_ready_threshold = 50.0;
    cfg.speech_energy_threshold_dbfs = -50.0;
    VoiceAnalyzer analyzer(cfg);
    const auto speech = make_sine(8000, 300.0, 2.0, 10000.0);
    const auto result = analyzer.analyze_pcm_batch(speech, 8000);
    EXPECT_GT(result.composite_score, 40.0);
}

TEST(AnalyzerTest, ClippingReducesScore) {
    AnalyzerConfig cfg;
    cfg.window_ms = 500;
    cfg.frame_ms = 20;
    VoiceAnalyzer clean(cfg);
    VoiceAnalyzer clipped(cfg);
    const auto speech = make_sine(8000, 300.0, 1.0, 10000.0);
    const auto bad = make_clipped(8000, 1.0);
    const auto clean_result = clean.analyze_pcm_batch(speech, 8000);
    const auto clipped_result = clipped.analyze_pcm_batch(bad, 8000);
    EXPECT_LT(clipped_result.composite_score, clean_result.composite_score);
}

TEST(SttGateTest, HysteresisRequiresConsecutiveWindows) {
    AnalyzerConfig cfg;
    cfg.hysteresis_ok_windows = 2;
    SttGate gate(cfg);

    WindowMetrics good;
    good.composite_score = 80;
    good.silence_ratio = 0.1;
    good.clipping_ratio = 0.0;
    good.snr_estimate_db = 20;

    EXPECT_FALSE(gate.evaluate(good));
    EXPECT_TRUE(gate.evaluate(good));
}

}  // namespace voiceqas
