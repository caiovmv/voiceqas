#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "voiceqas/audio/enhancement.hpp"

namespace voiceqas::audio {

TEST(RnnoiseEnhancerTest, DisabledIsNoop) {
    EnhancementConfig cfg;
    cfg.enabled = false;
    RnnoiseEnhancer enhancer(cfg);

    std::vector<int16_t> pcm(480 * 4);
    for (size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = static_cast<int16_t>(1000 * std::sin(2.0 * 3.141592653589793 * i / 48.0));
    }
    const auto before = pcm;
    const double ms = enhancer.process_inplace(pcm, 48000);
    EXPECT_DOUBLE_EQ(ms, 0.0);
    EXPECT_EQ(pcm, before);
}

TEST(RnnoiseEnhancerTest, EnabledDoesNotCrash) {
    EnhancementConfig cfg;
    cfg.enabled = true;
    RnnoiseEnhancer enhancer(cfg);

    // Enough samples for at least one 10 ms @ 48 kHz frame after upsample from 8 kHz.
    std::vector<int16_t> pcm(1600);
    for (size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = static_cast<int16_t>(8000 * ((i % 2 == 0) ? 1 : -1));
    }
    const double ms = enhancer.process_inplace(pcm, 8000);
    EXPECT_GE(ms, 0.0);
    EXPECT_EQ(pcm.size(), 1600u);
#if defined(VOICEQAS_HAS_RNNOISE)
    EXPECT_TRUE(enhancer.available());
#else
    EXPECT_FALSE(enhancer.available());
#endif
}

}  // namespace voiceqas::audio
