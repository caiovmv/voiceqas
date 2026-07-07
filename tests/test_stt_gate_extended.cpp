#include <gtest/gtest.h>

#include "voiceqas/metrics.hpp"
#include "voiceqas/stt_gate.hpp"

namespace voiceqas {

TEST(SttGateExtendedTest, RtpPenaltyReducesScore) {
    AnalyzerConfig cfg;
    cfg.stt_ready_threshold = 50.0;
    SttGate gate(cfg);

    WindowMetrics good;
    good.composite_score = 80;
    good.silence_ratio = 0.1;
    good.clipping_ratio = 0.0;
    good.snr_estimate_db = 20;
    good.packet_loss_pct = 0.0;

    WindowMetrics loss = good;
    loss.packet_loss_pct = 10.0;

    gate.evaluate(good);
    const auto score_good = good.composite_score;
    gate.reset();
    gate.evaluate(loss);
    EXPECT_LT(loss.composite_score, score_good);
}

}  // namespace voiceqas
