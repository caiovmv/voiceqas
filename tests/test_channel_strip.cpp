#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "voiceqas/audio/dsp/channel_strip.hpp"
#include "voiceqas/audio/strip_json.hpp"

using voiceqas::audio::AudioProcessingConfig;
using voiceqas::audio::VoiceChannelStrip;

namespace {

std::vector<int16_t> tone(int rate, double freq_hz, int samples, double amp = 0.3) {
    std::vector<int16_t> pcm(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const double v = amp * std::sin(2.0 * 3.141592653589793 * freq_hz * i / rate);
        pcm[static_cast<size_t>(i)] = static_cast<int16_t>(std::lround(v * 32767.0));
    }
    return pcm;
}

}  // namespace

TEST(ChannelStripTest, DisabledStagesLeaveSignalClose) {
    auto pcm = tone(16000, 1000.0, 1600);
    auto dry = pcm;
    AudioProcessingConfig cfg;
    cfg.strip.nr.enabled = false;
    cfg.strip.hpf.enabled = false;
    cfg.strip.eq.enabled = false;
    cfg.strip.deesser.enabled = false;
    cfg.strip.compressor.enabled = false;
    cfg.strip.limiter.enabled = false;
    cfg.strip.agc.enabled = false;
    cfg.sync_legacy_from_strip();

    VoiceChannelStrip strip(cfg);
    strip.process_inplace(pcm, 16000);

    double err = 0.0;
    for (size_t i = 0; i < pcm.size(); ++i) {
        const double d = static_cast<double>(pcm[i] - dry[i]);
        err += d * d;
    }
    EXPECT_LT(err / static_cast<double>(pcm.size()), 1.0);
}

TEST(ChannelStripTest, HpfChangesLowTone) {
    auto pcm = tone(16000, 40.0, 3200, 0.5);
    auto dry = pcm;
    AudioProcessingConfig cfg;
    cfg.strip.nr.enabled = false;
    cfg.strip.hpf.enabled = true;
    cfg.strip.hpf.cutoff_hz = 80.0;
    cfg.strip.eq.enabled = false;
    cfg.strip.deesser.enabled = false;
    cfg.strip.compressor.enabled = false;
    cfg.strip.limiter.enabled = false;
    cfg.strip.agc.enabled = false;
    cfg.sync_legacy_from_strip();

    VoiceChannelStrip strip(cfg);
    strip.process_inplace(pcm, 16000);

    double e_dry = 0.0;
    double e_wet = 0.0;
    for (size_t i = 800; i < pcm.size(); ++i) {
        e_dry += static_cast<double>(dry[i]) * dry[i];
        e_wet += static_cast<double>(pcm[i]) * pcm[i];
    }
    ASSERT_GT(e_dry, 1e6);
    EXPECT_LT(e_wet / e_dry, 0.5);
}

TEST(ChannelStripTest, MergeStripJsonOverrides) {
    voiceqas::audio::ChannelStripConfig strip;
    const bool ok = voiceqas::audio::merge_strip_json_string(
        strip,
        R"({"hpf":{"enabled":false,"cutoff_hz":120},"agc":{"target_rms_dbfs":-16}})");
    ASSERT_TRUE(ok);
    EXPECT_FALSE(strip.hpf.enabled);
    EXPECT_DOUBLE_EQ(strip.hpf.cutoff_hz, 120.0);
    EXPECT_DOUBLE_EQ(strip.agc.target_rms_dbfs, -16.0);
}
