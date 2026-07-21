#include "voiceqas/audio/dsp/channel_strip.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace voiceqas::audio {

namespace {

constexpr double kMinDb = -96.0;

double linear_to_dbfs(double linear) {
    if (linear <= 1e-10) {
        return kMinDb;
    }
    return std::max(kMinDb, 20.0 * std::log10(linear));
}

double dbfs_to_linear_fs(double dbfs) {
    return std::pow(10.0, dbfs / 20.0);
}

double smooth_coeff(double transition_ms, int sample_rate) {
    if (transition_ms <= 0.0 || sample_rate <= 0) {
        return 1.0;
    }
    const double sample_ms = 1000.0 / static_cast<double>(sample_rate);
    return 1.0 - std::exp(-sample_ms / transition_ms);
}

std::vector<float> to_float(std::span<const int16_t> samples) {
    std::vector<float> out(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        out[i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    return out;
}

void from_float(std::span<const float> in, std::span<int16_t> out) {
    const size_t n = std::min(in.size(), out.size());
    for (size_t i = 0; i < n; ++i) {
        const double scaled = static_cast<double>(in[i]) * 32768.0;
        out[i] = static_cast<int16_t>(
            std::clamp(static_cast<long long>(std::llround(scaled)), -32768LL, 32767LL));
    }
}

}  // namespace

void apply_peak_limiter_ceiling_inplace(std::span<int16_t> samples, double ceiling_dbfs) {
    const double ceiling = 32768.0 * dbfs_to_linear_fs(ceiling_dbfs);
    const int16_t clip = static_cast<int16_t>(std::min(ceiling, 32767.0));
    for (int16_t& s : samples) {
        s = static_cast<int16_t>(
            std::clamp(static_cast<int>(s), -static_cast<int>(clip), static_cast<int>(clip)));
    }
}

VoiceChannelStrip::VoiceChannelStrip(AudioProcessingConfig config)
    : config_(std::move(config)), enhancer_(), agc_() {
    config_.sync_legacy_from_strip();
    enhancer_ = RnnoiseEnhancer(config_.enhancement);
    agc_ = AgcState(config_);
}

void VoiceChannelStrip::reset() {
    enhancer_.reset();
    agc_.reset();
    hpf_.reset();
    for (auto& b : eq_bands_) {
        b.reset();
    }
    deess_detect_.reset();
    deess_env_ = 0.0;
    comp_env_ = 0.0;
    filters_rate_ = 0;
}

void VoiceChannelStrip::reconfigure(AudioProcessingConfig config) {
    config_ = std::move(config);
    config_.sync_legacy_from_strip();
    enhancer_ = RnnoiseEnhancer(config_.enhancement);
    agc_ = AgcState(config_);
    reset();
}

void VoiceChannelStrip::ensure_filters(int sample_rate) {
    if (filters_rate_ == sample_rate && !eq_bands_.empty()) {
        return;
    }
    filters_rate_ = sample_rate;
    const auto& s = config_.strip;
    hpf_.set_coeffs(dsp::Biquad::highpass(sample_rate, s.hpf.cutoff_hz));
    hpf_.reset();

    eq_bands_.assign(s.eq.bands.size(), dsp::Biquad{});
    for (size_t i = 0; i < s.eq.bands.size(); ++i) {
        const auto& band = s.eq.bands[i];
        eq_bands_[i].set_coeffs(
            dsp::Biquad::peaking(sample_rate, band.freq_hz, band.gain_db, band.q));
        eq_bands_[i].reset();
    }

    const double q = std::max(0.5, s.deesser.center_hz / std::max(1.0, s.deesser.bandwidth_hz));
    deess_detect_.set_coeffs(dsp::Biquad::bandpass(sample_rate, s.deesser.center_hz, q));
    deess_detect_.reset();
}

void VoiceChannelStrip::process_hpf(std::span<float> samples, int sample_rate) {
    if (!config_.strip.hpf.enabled || samples.empty()) {
        return;
    }
    ensure_filters(sample_rate);
    hpf_.process_inplace(samples);
}

void VoiceChannelStrip::process_eq(std::span<float> samples, int sample_rate) {
    if (!config_.strip.eq.enabled || samples.empty()) {
        return;
    }
    ensure_filters(sample_rate);
    for (auto& band : eq_bands_) {
        band.process_inplace(samples);
    }
}

void VoiceChannelStrip::process_deesser(std::span<float> samples, int sample_rate) {
    const auto& d = config_.strip.deesser;
    if (!d.enabled || samples.empty()) {
        return;
    }
    ensure_filters(sample_rate);
    const double thr = dbfs_to_linear_fs(d.threshold_db);
    const double ratio = std::max(1.0, d.ratio);
    const double atk = smooth_coeff(d.attack_ms, sample_rate);
    const double rel = smooth_coeff(d.release_ms, sample_rate);

    dsp::Biquad detect = deess_detect_;
    for (float& s : samples) {
        const float band = detect.process(s);
        const double level = std::fabs(static_cast<double>(band));
        const double coeff = level > deess_env_ ? atk : rel;
        deess_env_ += (level - deess_env_) * coeff;

        double gr = 1.0;
        if (deess_env_ > thr && thr > 1e-12) {
            const double over = deess_env_ / thr;
            const double reduced = std::pow(over, 1.0 / ratio - 1.0);
            gr = std::clamp(reduced, 0.05, 1.0);
        }
        s = static_cast<float>(static_cast<double>(s) - (1.0 - gr) * static_cast<double>(band));
    }
    deess_detect_ = detect;
}

void VoiceChannelStrip::process_compressor(std::span<float> samples, int sample_rate) {
    const auto& c = config_.strip.compressor;
    if (!c.enabled || samples.empty()) {
        return;
    }
    const double thr = dbfs_to_linear_fs(c.threshold_db);
    const double ratio = std::max(1.0, c.ratio);
    const double atk = smooth_coeff(c.attack_ms, sample_rate);
    const double rel = smooth_coeff(c.release_ms, sample_rate);
    const double makeup = std::pow(10.0, c.makeup_db / 20.0);

    for (float& s : samples) {
        const double level = std::fabs(static_cast<double>(s));
        const double coeff = level > comp_env_ ? atk : rel;
        comp_env_ += (level - comp_env_) * coeff;

        double gr = 1.0;
        if (comp_env_ > thr && thr > 1e-12) {
            const double env_db = linear_to_dbfs(comp_env_);
            const double thr_db = c.threshold_db;
            const double gr_db = (env_db - thr_db) * (1.0 - 1.0 / ratio);
            gr = std::pow(10.0, -gr_db / 20.0);
        }
        s = static_cast<float>(static_cast<double>(s) * gr * makeup);
    }
}

void VoiceChannelStrip::process_limiter(std::span<float> samples) {
    if (!config_.strip.limiter.enabled || samples.empty()) {
        return;
    }
    const float ceil = static_cast<float>(dbfs_to_linear_fs(config_.strip.limiter.ceiling_dbfs));
    for (float& s : samples) {
        s = std::clamp(s, -ceil, ceil);
    }
}

ChannelStripTimings VoiceChannelStrip::process_inplace(std::span<int16_t> samples, int sample_rate) {
    ChannelStripTimings t;
    if (samples.empty() || sample_rate <= 0) {
        return t;
    }

    // Keep enhancer / AGC configs aligned with strip.
    config_.sync_legacy_from_strip();

    if (config_.strip.nr.enabled) {
        const auto started = std::chrono::steady_clock::now();
        t.nr_ms = enhancer_.process_inplace(samples, sample_rate);
        t.nr_applied = enhancer_.enabled();
        if (t.nr_ms <= 0.0) {
            t.nr_ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();
        }
    }

    auto buf = to_float(samples);

    {
        const auto started = std::chrono::steady_clock::now();
        process_hpf(buf, sample_rate);
        t.hpf_ms = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - started)
                       .count();
    }
    {
        const auto started = std::chrono::steady_clock::now();
        process_eq(buf, sample_rate);
        t.eq_ms = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - started)
                      .count();
    }
    {
        const auto started = std::chrono::steady_clock::now();
        process_deesser(buf, sample_rate);
        t.deesser_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - started)
                           .count();
    }
    {
        const auto started = std::chrono::steady_clock::now();
        process_compressor(buf, sample_rate);
        t.compressor_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count();
    }
    {
        const auto started = std::chrono::steady_clock::now();
        process_limiter(buf);
        t.limiter_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - started)
                           .count();
    }

    from_float(buf, samples);

    if (config_.strip.agc.enabled) {
        const auto started = std::chrono::steady_clock::now();
        agc_.process_inplace(samples, sample_rate);
        t.agc_ms = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - started)
                       .count();
        t.agc_applied = true;
    }

    return t;
}

}  // namespace voiceqas::audio
