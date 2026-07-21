#include "voiceqas/audio/resampler.hpp"

#include <algorithm>
#include <cmath>

namespace voiceqas::audio {

namespace {

constexpr int kHalfWidth = 8;

double sinc(double x) {
    if (std::abs(x) < 1e-9) {
        return 1.0;
    }
    const double pix = M_PI * x;
    return std::sin(pix) / pix;
}

double lanczos_window(double x, int a) {
    if (std::abs(x) >= a) {
        return 0.0;
    }
    return sinc(x) * sinc(x / static_cast<double>(a));
}

}  // namespace

std::vector<int16_t> resample_sinc_pcm16(
    std::span<const int16_t> input,
    int from_rate,
    int to_rate) {
    if (from_rate == to_rate || input.empty()) {
        return {input.begin(), input.end()};
    }

    const double ratio = static_cast<double>(from_rate) / static_cast<double>(to_rate);
    const size_t out_len = static_cast<size_t>(std::floor(static_cast<double>(input.size()) / ratio));
    std::vector<int16_t> out(out_len);

    for (size_t i = 0; i < out_len; ++i) {
        const double src_pos = static_cast<double>(i) * ratio;
        const int center = static_cast<int>(std::floor(src_pos));
        const double frac = src_pos - static_cast<double>(center);

        double sum = 0.0;
        double weight_sum = 0.0;
        for (int tap = -kHalfWidth; tap <= kHalfWidth; ++tap) {
            const int idx = center + tap;
            if (idx < 0 || idx >= static_cast<int>(input.size())) {
                continue;
            }
            const double x = static_cast<double>(tap) - frac;
            const double w = lanczos_window(x, kHalfWidth);
            sum += static_cast<double>(input[static_cast<size_t>(idx)]) * w;
            weight_sum += w;
        }
        if (weight_sum > 1e-9) {
            sum /= weight_sum;
        }
        out[i] = static_cast<int16_t>(std::clamp(std::lround(sum), -32768L, 32767L));
    }
    return out;
}

}  // namespace voiceqas::audio
