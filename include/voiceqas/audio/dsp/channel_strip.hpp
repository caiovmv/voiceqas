#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "voiceqas/audio/config.hpp"
#include "voiceqas/audio/dsp/biquad.hpp"
#include "voiceqas/audio/enhancement.hpp"
#include "voiceqas/audio/normalizer.hpp"

namespace voiceqas::audio {

struct ChannelStripTimings {
    double nr_ms = 0.0;
    double hpf_ms = 0.0;
    double eq_ms = 0.0;
    double deesser_ms = 0.0;
    double compressor_ms = 0.0;
    double limiter_ms = 0.0;
    double agc_ms = 0.0;
    bool nr_applied = false;
    bool agc_applied = false;
};

/**
 * Inbound voice channel strip:
 * NR → HPF → EQ → De-esser → Compressor → Limiter → AGC
 */
class VoiceChannelStrip {
public:
    explicit VoiceChannelStrip(AudioProcessingConfig config = {});

    void reset();
    void reconfigure(AudioProcessingConfig config);

    ChannelStripTimings process_inplace(std::span<int16_t> samples, int sample_rate);

    const AudioProcessingConfig& config() const { return config_; }

private:
    void ensure_filters(int sample_rate);
    void process_hpf(std::span<float> samples, int sample_rate);
    void process_eq(std::span<float> samples, int sample_rate);
    void process_deesser(std::span<float> samples, int sample_rate);
    void process_compressor(std::span<float> samples, int sample_rate);
    void process_limiter(std::span<float> samples);

    AudioProcessingConfig config_;
    RnnoiseEnhancer enhancer_;
    AgcState agc_;

    int filters_rate_ = 0;
    dsp::Biquad hpf_;
    std::vector<dsp::Biquad> eq_bands_;
    dsp::Biquad deess_detect_;
    double deess_env_ = 0.0;
    double comp_env_ = 0.0;
};

/** Hard peak clamp to ceiling_dbfs (full-scale 32768). */
void apply_peak_limiter_ceiling_inplace(std::span<int16_t> samples, double ceiling_dbfs);

}  // namespace voiceqas::audio
