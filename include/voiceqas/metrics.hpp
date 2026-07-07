#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace voiceqas {

enum class AudioFormat {
    PcmS16Le8k,
    PcmS16Le16k,
    RtpPcmu,
    RtpPcma,
    RtpG722,
    RtpG729,
};

struct AnalyzerConfig {
    int frame_ms = 20;
    int window_ms = 500;
    int sample_rate = 8000;
    double stt_ready_threshold = 65.0;
    double min_snr_db = 12.0;
    double max_clipping_ratio = 0.02;
    double max_silence_ratio = 0.60;
    int hysteresis_ok_windows = 2;
    int hysteresis_bad_windows = 1;
    double speech_energy_threshold_dbfs = -40.0;
};

struct FrameMetrics {
    double rms_dbfs = -96.0;
    double peak_dbfs = -96.0;
    double clipping_ratio = 0.0;
    bool is_speech = false;
};

struct WindowMetrics {
    double rms_dbfs = -96.0;
    double peak_dbfs = -96.0;
    double clipping_ratio = 0.0;
    double snr_estimate_db = 0.0;
    double silence_ratio = 1.0;
    double spectral_flatness = 1.0;
    double packet_loss_pct = 0.0;
    double jitter_ms = 0.0;
    double composite_score = 0.0;
    bool stt_ready = false;
    int64_t window_start_ms = 0;
};

struct BatchResult {
    double composite_score = 0.0;
    bool stt_ready = false;
    WindowMetrics aggregated{};
    std::vector<WindowMetrics> windows;
    std::vector<std::pair<int64_t, int64_t>> stt_ready_segments;
};

int sample_rate_for_format(AudioFormat format);
int rtp_clock_rate_for_format(AudioFormat format);
AudioFormat audio_format_from_string(const std::string& s);
std::string audio_format_to_string(AudioFormat format);

}  // namespace voiceqas
