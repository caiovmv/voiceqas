#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "voiceqas/audio/dsp/biquad.hpp"

using voiceqas::audio::dsp::Biquad;

TEST(BiquadTest, HighpassAttenuatesLowFrequency) {
    constexpr int kRate = 16000;
    constexpr int kn = 1600;
    Biquad hpf;
    hpf.set_coeffs(Biquad::highpass(kRate, 80.0));

    double energy_in = 0.0;
    double energy_out = 0.0;
    for (int i = 0; i < kn; ++i) {
        const float x = 0.5f * std::sin(2.0f * 3.14159265f * 40.0f * static_cast<float>(i) / kRate);
        energy_in += static_cast<double>(x) * x;
        const float y = hpf.process(x);
        energy_out += static_cast<double>(y) * y;
    }
    ASSERT_GT(energy_in, 1e-6);
    EXPECT_LT(energy_out / energy_in, 0.35);
}

TEST(BiquadTest, PeakingBoostsBandEnergy) {
    constexpr int kRate = 16000;
    constexpr int kn = 1600;
    Biquad peak;
    peak.set_coeffs(Biquad::peaking(kRate, 2500.0, 6.0, 0.8));

    double energy_in = 0.0;
    double energy_out = 0.0;
    for (int i = 0; i < kn; ++i) {
        const float x = 0.25f * std::sin(2.0f * 3.14159265f * 2500.0f * static_cast<float>(i) / kRate);
        energy_in += static_cast<double>(x) * x;
        const float y = peak.process(x);
        energy_out += static_cast<double>(y) * y;
    }
    ASSERT_GT(energy_in, 1e-6);
    EXPECT_GT(energy_out / energy_in, 1.5);
}
