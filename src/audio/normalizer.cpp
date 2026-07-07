#include "voiceqas/audio/normalizer.hpp"

#include <algorithm>
#include <cmath>

namespace voiceqas::audio {

namespace {

constexpr double kMinDb = -96.0;

double linear_to_dbfs(double linear) {
    if (linear <= 1e-10) {
        return kMinDb;
    }
    return std::max(kMinDb, 20.0 * std::log10(linear / 32768.0));
}

double dbfs_to_linear(double dbfs) {
    return 32768.0 * std::pow(10.0, dbfs / 20.0);
}

double frame_rms(std::span<const int16_t> samples) {
    if (samples.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (int16_t s : samples) {
        const double v = static_cast<double>(s);
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

double smooth_coeff(double transition_ms, int sample_rate, size_t frame_samples) {
    const double frame_ms = 1000.0 * static_cast<double>(frame_samples) / static_cast<double>(sample_rate);
    if (transition_ms <= 0.0 || frame_ms <= 0.0) {
        return 1.0;
    }
    return 1.0 - std::exp(-frame_ms / transition_ms);
}

}  // namespace

AgcState::AgcState(AudioProcessingConfig config) : config_(std::move(config)) {}

void AgcState::reset() {
    gain_linear_ = 1.0;
}

void AgcState::apply_peak_limiter_inplace(std::span<int16_t> samples) const {
    const double ceiling = dbfs_to_linear(config_.limiter_ceiling_dbfs);
    const int16_t clip = static_cast<int16_t>(std::min(ceiling, 32767.0));
    for (int16_t& s : samples) {
        s = static_cast<int16_t>(std::clamp(static_cast<int>(s), -static_cast<int>(clip), static_cast<int>(clip)));
    }
}

void AgcState::process_inplace(std::span<int16_t> samples, int sample_rate) {
    if (samples.empty() || !config_.normalize_enabled) {
        return;
    }

    const int rate = sample_rate > 0 ? sample_rate : 8000;
    const double rms = frame_rms(samples);
    const double rms_dbfs = linear_to_dbfs(rms);
    const double target_linear = dbfs_to_linear(config_.agc_target_rms_dbfs);
    const double max_gain = std::pow(10.0, config_.agc_max_gain_db / 20.0);

    double desired_gain = 1.0;
    if (rms > 1e-6) {
        desired_gain = target_linear / rms;
    }
    desired_gain = std::clamp(desired_gain, 1.0 / max_gain, max_gain);

    const double alpha = rms_dbfs < config_.agc_target_rms_dbfs
        ? smooth_coeff(config_.agc_attack_ms, rate, samples.size())
        : smooth_coeff(config_.agc_release_ms, rate, samples.size());
    gain_linear_ += (desired_gain - gain_linear_) * alpha;

    for (int16_t& s : samples) {
        const double scaled = static_cast<double>(s) * gain_linear_;
        s = static_cast<int16_t>(std::clamp(static_cast<long long>(std::llround(scaled)), -32768LL, 32767LL));
    }

    apply_peak_limiter_inplace(samples);
}

void normalize_pcm_inplace(
    std::span<int16_t> samples,
    AgcState& state,
    const AudioProcessingConfig& config) {
    if (!config.normalize_enabled) {
        return;
    }
    state.process_inplace(samples, 8000);
}

}  // namespace voiceqas::audio
