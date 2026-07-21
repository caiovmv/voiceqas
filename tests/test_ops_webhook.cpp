#include <gtest/gtest.h>

#include "voiceqas/ops/config.hpp"
#include "voiceqas/ops/webhook.hpp"

namespace voiceqas::ops {
namespace {

class OpsWebhookTest : public ::testing::Test {
protected:
    void SetUp() override {
        saved_ = global_ops_config();
        auto cfg = saved_;
        cfg.alert_score_threshold = 0.45;
        cfg.stt_not_ready_alert_ms = 100;
        cfg.webhook_url.clear();
        global_ops_config() = cfg;
    }

    void TearDown() override {
        global_ops_config() = saved_;
    }

    ops::OpsConfig saved_{};
};

TEST_F(OpsWebhookTest, LowScoreProducesAlert) {
    const std::string session = "webhook-low-score-1";
    const auto alert = evaluate_vqa_alert(session, {{"composite_score", 0.2}, {"stt_ready", true}});
    ASSERT_TRUE(alert.has_value());
    EXPECT_EQ((*alert)["alert_kind"], "low_score");
    EXPECT_EQ((*alert)["session_id"], session);
}

TEST_F(OpsWebhookTest, HealthyScoreProducesNoAlert) {
    const auto alert = evaluate_vqa_alert(
        "webhook-healthy-1", {{"composite_score", 0.9}, {"stt_ready", true}});
    EXPECT_FALSE(alert.has_value());
}

TEST_F(OpsWebhookTest, CodecMismatchIsIdempotentPerSession) {
    publish_codec_mismatch_alert(
        "codec-mismatch-1",
        AudioFormat::RtpG722,
        AudioFormat::RtpPcmu,
        "test");
    EXPECT_NO_THROW(publish_codec_mismatch_alert(
        "codec-mismatch-1",
        AudioFormat::RtpG722,
        AudioFormat::RtpPcmu,
        "test"));
}

TEST_F(OpsWebhookTest, MatchingCodecsSkipAlert) {
    EXPECT_NO_THROW(publish_codec_mismatch_alert(
        "codec-match-1",
        AudioFormat::RtpG722,
        AudioFormat::RtpG722,
        "test"));
}

}  // namespace
}  // namespace voiceqas::ops
