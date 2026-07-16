#include "voiceqas/audio/enhancement.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "voiceqas/audio/resampler.hpp"

#if defined(VOICEQAS_HAS_RNNOISE)
#include <rnnoise.h>
#endif

namespace voiceqas::audio {

namespace {

constexpr int kRnnoiseRate = 48000;
constexpr int kRnnoiseFrame = 480;  // 10 ms @ 48 kHz

}  // namespace

struct RnnoiseEnhancer::Impl {
#if defined(VOICEQAS_HAS_RNNOISE)
    DenoiseState* state = nullptr;
#endif
    std::vector<float> pending;
    int last_rate = 0;

    Impl() {
#if defined(VOICEQAS_HAS_RNNOISE)
        state = rnnoise_create(nullptr);
#endif
    }

    ~Impl() {
#if defined(VOICEQAS_HAS_RNNOISE)
        if (state) {
            rnnoise_destroy(state);
        }
#endif
    }

    void reset() {
        pending.clear();
        last_rate = 0;
#if defined(VOICEQAS_HAS_RNNOISE)
        if (state) {
            rnnoise_destroy(state);
            state = rnnoise_create(nullptr);
        }
#endif
    }
};

RnnoiseEnhancer::RnnoiseEnhancer(EnhancementConfig config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}

RnnoiseEnhancer::~RnnoiseEnhancer() = default;

RnnoiseEnhancer::RnnoiseEnhancer(RnnoiseEnhancer&&) noexcept = default;
RnnoiseEnhancer& RnnoiseEnhancer::operator=(RnnoiseEnhancer&&) noexcept = default;

void RnnoiseEnhancer::reset() {
    if (impl_) {
        impl_->reset();
    }
}

bool RnnoiseEnhancer::available() const {
#if defined(VOICEQAS_HAS_RNNOISE)
    return impl_ && impl_->state != nullptr;
#else
    return false;
#endif
}

double RnnoiseEnhancer::process_inplace(std::span<int16_t> samples, int sample_rate) {
    if (!enabled() || samples.empty() || sample_rate <= 0) {
        return 0.0;
    }

    const auto started = std::chrono::steady_clock::now();

#if defined(VOICEQAS_HAS_RNNOISE)
    if (impl_->last_rate != 0 && impl_->last_rate != sample_rate) {
        impl_->reset();
    }
    impl_->last_rate = sample_rate;

    const auto up = resample_sinc_pcm16(samples, sample_rate, kRnnoiseRate);
    std::vector<float> float_in(up.size());
    for (size_t i = 0; i < up.size(); ++i) {
        // RNNoise expects ±32768-scale float (same as int16), not ±1.
        float_in[i] = static_cast<float>(up[i]);
    }

    impl_->pending.insert(impl_->pending.end(), float_in.begin(), float_in.end());

    std::vector<float> float_out;
    float_out.reserve(impl_->pending.size());
    size_t offset = 0;
    while (impl_->pending.size() - offset >= static_cast<size_t>(kRnnoiseFrame)) {
        float frame[kRnnoiseFrame];
        for (int i = 0; i < kRnnoiseFrame; ++i) {
            frame[i] = impl_->pending[offset + static_cast<size_t>(i)];
        }
        rnnoise_process_frame(impl_->state, frame, frame);
        float_out.insert(float_out.end(), frame, frame + kRnnoiseFrame);
        offset += static_cast<size_t>(kRnnoiseFrame);
    }
    if (offset > 0) {
        impl_->pending.erase(impl_->pending.begin(),
                             impl_->pending.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    if (float_out.empty()) {
        // Not enough samples for a full RNNoise frame yet — leave PCM unchanged.
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - started)
            .count();
    }

    std::vector<int16_t> pcm48(float_out.size());
    for (size_t i = 0; i < float_out.size(); ++i) {
        const auto v = static_cast<int32_t>(std::lround(float_out[i]));
        pcm48[i] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
    }

    auto down = resample_sinc_pcm16(pcm48, kRnnoiseRate, sample_rate);
    const double wet = std::clamp(config_.wet_dry, 0.0, 1.0);
    const double dry = 1.0 - wet;
    const size_t n = std::min(down.size(), samples.size());
    if (wet >= 1.0 - 1e-9) {
        std::copy_n(down.begin(), n, samples.begin());
    } else if (wet <= 1e-9) {
        // leave dry samples
    } else {
        for (size_t i = 0; i < n; ++i) {
            const double mixed = dry * static_cast<double>(samples[i]) + wet * static_cast<double>(down[i]);
            samples[i] = static_cast<int16_t>(
                std::clamp(static_cast<long long>(std::llround(mixed)), -32768LL, 32767LL));
        }
    }
#else
    (void)sample_rate;
#endif

    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
        .count();
}

}  // namespace voiceqas::audio
