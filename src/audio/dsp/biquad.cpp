#include "voiceqas/audio/dsp/biquad.hpp"

#include <algorithm>

namespace voiceqas::audio::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp_freq(double sample_rate, double freq_hz) {
    const double nyquist = sample_rate * 0.5 - 1.0;
    return std::clamp(freq_hz, 1.0, std::max(1.0, nyquist));
}

}  // namespace

void Biquad::set_coeffs(const BiquadCoeffs& c) {
    c_ = c;
}

void Biquad::reset() {
    z1_ = 0.0;
    z2_ = 0.0;
}

float Biquad::process(float x) {
    const double y = c_.b0 * x + z1_;
    z1_ = c_.b1 * x - c_.a1 * y + z2_;
    z2_ = c_.b2 * x - c_.a2 * y;
    return static_cast<float>(y);
}

void Biquad::process_inplace(std::span<float> samples) {
    for (float& s : samples) {
        s = process(s);
    }
}

BiquadCoeffs Biquad::highpass(double sample_rate, double cutoff_hz, double q) {
    const double f0 = clamp_freq(sample_rate, cutoff_hz);
    const double w0 = 2.0 * kPi * f0 / sample_rate;
    const double cos_w0 = std::cos(w0);
    const double sin_w0 = std::sin(w0);
    const double alpha = sin_w0 / (2.0 * std::max(0.05, q));
    const double b0 = (1.0 + cos_w0) * 0.5;
    const double b1 = -(1.0 + cos_w0);
    const double b2 = (1.0 + cos_w0) * 0.5;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cos_w0;
    const double a2 = 1.0 - alpha;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

BiquadCoeffs Biquad::peaking(double sample_rate, double freq_hz, double gain_db, double q) {
    const double f0 = clamp_freq(sample_rate, freq_hz);
    const double A = std::pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * kPi * f0 / sample_rate;
    const double cos_w0 = std::cos(w0);
    const double sin_w0 = std::sin(w0);
    const double alpha = sin_w0 / (2.0 * std::max(0.05, q));
    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cos_w0;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cos_w0;
    const double a2 = 1.0 - alpha / A;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

BiquadCoeffs Biquad::bandpass(double sample_rate, double freq_hz, double q) {
    const double f0 = clamp_freq(sample_rate, freq_hz);
    const double w0 = 2.0 * kPi * f0 / sample_rate;
    const double cos_w0 = std::cos(w0);
    const double sin_w0 = std::sin(w0);
    const double alpha = sin_w0 / (2.0 * std::max(0.05, q));
    const double b0 = alpha;
    const double b1 = 0.0;
    const double b2 = -alpha;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cos_w0;
    const double a2 = 1.0 - alpha;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

}  // namespace voiceqas::audio::dsp
