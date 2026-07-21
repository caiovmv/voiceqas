#include <gtest/gtest.h>

#include <vector>

#include "voiceqas/stt/diarization.hpp"

namespace voiceqas::stt {

TEST(DiarizationTest, PicksLongerSpeakerAsPrimary) {
    // Speaker 0: 400ms, Speaker 1: 900ms (after alternate assignment by turn order).
    std::vector<SpeechTurn> raw = {
        {.start_ms = 0, .end_ms = 400},
        {.start_ms = 500, .end_ms = 1400},
    };
    std::vector<int16_t> pcm(16000, 1000);  // 1s @ 16k
    DiarizationConfig cfg;
    cfg.min_turn_ms = 300;
    cfg.primary_mode = "first_then_max_presence";

    const auto result = pick_primary_turns(raw, pcm, cfg);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.turns.size(), 2u);
    // First turn 400ms vs second 900ms — second wins unless first is within 10%.
    EXPECT_EQ(result.primary_speaker, 1);
    EXPECT_TRUE(result.turns[1].is_primary);
    EXPECT_FALSE(result.turns[0].is_primary);
    EXPECT_FALSE(result.primary_pcm.empty());
}

TEST(DiarizationTest, PrefersFirstWhenWithinTenPercent) {
    std::vector<SpeechTurn> raw = {
        {.start_ms = 0, .end_ms = 1000},
        {.start_ms = 1100, .end_ms = 2050},  // 950ms — within 10% of 1000
    };
    std::vector<int16_t> pcm(32000, 2000);
    DiarizationConfig cfg;
    cfg.min_turn_ms = 300;
    cfg.primary_mode = "first_then_max_presence";

    const auto result = pick_primary_turns(raw, pcm, cfg);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.primary_speaker, 0);
}

TEST(DiarizationTest, DropsShortTurns) {
    std::vector<SpeechTurn> raw = {
        {.start_ms = 0, .end_ms = 100},
        {.start_ms = 200, .end_ms = 800},
    };
    std::vector<int16_t> pcm(16000, 500);
    DiarizationConfig cfg;
    cfg.min_turn_ms = 300;

    const auto result = pick_primary_turns(raw, pcm, cfg);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.turns.size(), 1u);
    EXPECT_EQ(result.primary_speaker, 0);
}

}  // namespace voiceqas::stt
