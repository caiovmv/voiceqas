#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "voiceqas/audio/config.hpp"

namespace voiceqas::audio {

/**
 * Per-session RNNoise denoise (48 kHz frames). Resamples to/from stream rate.
 * No-op when enhancement.enabled is false or RNNoise was not linked.
 */
class RnnoiseEnhancer {
public:
    explicit RnnoiseEnhancer(EnhancementConfig config = {});
    ~RnnoiseEnhancer();

    RnnoiseEnhancer(const RnnoiseEnhancer&) = delete;
    RnnoiseEnhancer& operator=(const RnnoiseEnhancer&) = delete;
    RnnoiseEnhancer(RnnoiseEnhancer&&) noexcept;
    RnnoiseEnhancer& operator=(RnnoiseEnhancer&&) noexcept;

    void reset();
    bool available() const;
    bool enabled() const { return config_.enabled && available(); }

    /** Denoise in place; returns wall time in ms (0 if skipped). */
    double process_inplace(std::span<int16_t> samples, int sample_rate);

private:
    struct Impl;
    EnhancementConfig config_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace voiceqas::audio
