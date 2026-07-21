#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace voiceqas::ops {

struct PrometheusVqaSnapshot {
    double composite_score = 0.0;
    double snr_db = 0.0;
    double silence_ratio = 0.0;
    int active_sessions = 0;
    int stt_ready_sessions = 0;
};

struct PipelinePrometheusUpdate {
    std::string stage;
    std::string direction;
    uint64_t bytes = 0;
    double latency_ms = 0.0;
    double jitter_ms = 0.0;
    double composite_score = 0.0;
    uint64_t dropped_bytes = 0;
};

class PrometheusMetrics {
public:
    static PrometheusMetrics& instance();

    void on_vqa_window(const std::string& session_id, bool stt_ready, double composite_score,
                       double snr_db, double silence_ratio);
    void on_session_removed(const std::string& session_id);
    void reset_pipeline_fleet();
    void update_pipeline_stage(const PipelinePrometheusUpdate& update);
    void set_pipeline_active_sessions(int count);
    void set_pipeline_finished_sessions(int recent_count, uint64_t total_count);

    PrometheusVqaSnapshot snapshot() const;
    std::string render_prometheus(const PrometheusVqaSnapshot& base) const;

private:
    PrometheusMetrics() = default;

    struct PipelineFleetStage {
        uint64_t bytes = 0;
        double latency_ms = 0.0;
        double jitter_ms = 0.0;
        double composite_score = 0.0;
        uint64_t dropped_bytes = 0;
    };

    using PipelineKey = std::string;

    static PipelineKey pipeline_key(const std::string& direction, const std::string& stage);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, bool> session_stt_ready_;
    std::unordered_map<PipelineKey, PipelineFleetStage> pipeline_fleet_;
    std::atomic<int> pipeline_active_sessions_{0};
    std::atomic<int> pipeline_finished_sessions_{0};
    std::atomic<uint64_t> pipeline_finished_sessions_total_{0};
    std::atomic<double> last_composite_score_{0.0};
    std::atomic<double> last_snr_db_{0.0};
    std::atomic<double> last_silence_ratio_{0.0};
};

}  // namespace voiceqas::ops
