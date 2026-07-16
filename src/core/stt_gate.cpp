#include "voiceqas/stt_gate.hpp"

#include <algorithm>
#include <cmath>

namespace voiceqas {

SttGate::SttGate(const AnalyzerConfig& config) : config_(config) {}

double SttGate::composite_score(const WindowMetrics& m) const {
    // Quality-only: silence is presence, gated separately via max_silence_ratio_for_ready.
    double score = 100.0;

    score -= std::clamp(m.clipping_ratio / std::max(config_.max_clipping_ratio, 1e-6), 0.0, 1.0) * 30.0;

    const double snr_factor = std::clamp(m.snr_estimate_db / std::max(config_.min_snr_db, 1e-6), 0.0, 1.5);
    score = score * (0.5 + 0.5 * snr_factor);

    const double flatness_penalty = std::clamp((m.spectral_flatness - 0.3) / 0.7, 0.0, 1.0) * 15.0;
    score -= flatness_penalty;

    const double rtp_penalty = std::clamp(m.packet_loss_pct / 5.0, 0.0, 1.0) * 10.0
                             + std::clamp(m.jitter_ms / 50.0, 0.0, 1.0) * 10.0;
    score -= rtp_penalty;

    const double level_penalty = m.rms_dbfs < -50.0 ? std::min(20.0, (-50.0 - m.rms_dbfs) * 0.5) : 0.0;
    score -= level_penalty;

    return std::clamp(score, 0.0, 100.0);
}

bool SttGate::passes_thresholds(const WindowMetrics& m) const {
    const double silence_cap = config_.max_silence_ratio_for_ready;
    return m.speech_quality_score >= config_.stt_ready_threshold
        && m.silence_ratio <= silence_cap
        && m.clipping_ratio <= config_.max_clipping_ratio
        && m.snr_estimate_db >= config_.min_snr_db;
}

bool SttGate::evaluate(WindowMetrics& metrics) {
    metrics.speech_quality_score = composite_score(metrics);
    metrics.composite_score = metrics.speech_quality_score;

    if (passes_thresholds(metrics)) {
        consecutive_ok_++;
        consecutive_bad_ = 0;
        if (!current_state_ && consecutive_ok_ >= config_.hysteresis_ok_windows) {
            current_state_ = true;
        }
    } else {
        consecutive_bad_++;
        consecutive_ok_ = 0;
        if (current_state_ && consecutive_bad_ >= config_.hysteresis_bad_windows) {
            current_state_ = false;
        }
    }

    metrics.stt_ready = current_state_;
    return current_state_;
}

void SttGate::reset() {
    consecutive_ok_ = 0;
    consecutive_bad_ = 0;
    current_state_ = false;
}

}  // namespace voiceqas
