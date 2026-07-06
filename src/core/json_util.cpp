#include "voiceqas/json_util.hpp"

namespace voiceqas {

nlohmann::json window_metrics_to_json(const WindowMetrics& m) {
    return {
        {"window_start_ms", m.window_start_ms},
        {"rms_dbfs", m.rms_dbfs},
        {"peak_dbfs", m.peak_dbfs},
        {"clipping_ratio", m.clipping_ratio},
        {"snr_estimate_db", m.snr_estimate_db},
        {"silence_ratio", m.silence_ratio},
        {"spectral_flatness", m.spectral_flatness},
        {"packet_loss_pct", m.packet_loss_pct},
        {"jitter_ms", m.jitter_ms},
        {"composite_score", m.composite_score},
        {"stt_ready", m.stt_ready},
    };
}

nlohmann::json batch_result_to_json(const BatchResult& r) {
    nlohmann::json windows = nlohmann::json::array();
    for (const auto& w : r.windows) {
        windows.push_back(window_metrics_to_json(w));
    }

    nlohmann::json segments = nlohmann::json::array();
    for (const auto& [start, end] : r.stt_ready_segments) {
        segments.push_back({{"start_ms", start}, {"end_ms", end}});
    }

    return {
        {"composite_score", r.composite_score},
        {"stt_ready", r.stt_ready},
        {"aggregated", window_metrics_to_json(r.aggregated)},
        {"windows", windows},
        {"stt_ready_segments", segments},
    };
}

}  // namespace voiceqas
