#pragma once

#include <cmath>
#include <span>

namespace voiceqas::audio::dsp {

struct BiquadCoeffs {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

class Biquad {
public:
    void set_coeffs(const BiquadCoeffs& c);
    void reset();
    float process(float x);
    void process_inplace(std::span<float> samples);

    static BiquadCoeffs highpass(double sample_rate, double cutoff_hz, double q = 0.7071067811865476);
    static BiquadCoeffs peaking(double sample_rate, double freq_hz, double gain_db, double q);
    static BiquadCoeffs bandpass(double sample_rate, double freq_hz, double q);

private:
    BiquadCoeffs c_{};
    double z1_ = 0.0;
    double z2_ = 0.0;
};

}  // namespace voiceqas::audio::dsp
