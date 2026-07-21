#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "voiceqas/audio/config.hpp"

namespace voiceqas::audio {

class AgcState {
public:
    explicit AgcState(AudioProcessingConfig config = {});

    void reset();
    void process_inplace(std::span<int16_t> samples, int sample_rate = 8000);
    void apply_peak_limiter_inplace(std::span<int16_t> samples) const;

private:
    AudioProcessingConfig config_;
    double gain_linear_ = 1.0;
};

void normalize_pcm_inplace(std::span<int16_t> samples, AgcState& state, const AudioProcessingConfig& config);

}  // namespace voiceqas::audio
