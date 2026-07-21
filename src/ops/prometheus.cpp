#include "voiceqas/ops/prometheus.hpp"

#include <sstream>

namespace voiceqas::ops {

PrometheusMetrics& PrometheusMetrics::instance() {
    static PrometheusMetrics metrics;
    return metrics;
}

PrometheusMetrics::PipelineKey PrometheusMetrics::pipeline_key(
    const std::string& direction,
    const std::string& stage) {
    return direction + "|" + stage;
}

void PrometheusMetrics::on_vqa_window(
    const std::string& session_id,
    bool stt_ready,
    double composite_score,
    double snr_db,
    double silence_ratio) {
    {
        std::lock_guard lock(mutex_);
        session_stt_ready_[session_id] = stt_ready;
    }
    last_composite_score_.store(composite_score);
    last_snr_db_.store(snr_db);
    last_silence_ratio_.store(silence_ratio);
}

void PrometheusMetrics::on_session_removed(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    session_stt_ready_.erase(session_id);
}

void PrometheusMetrics::reset_pipeline_fleet() {
    std::lock_guard lock(mutex_);
    pipeline_fleet_.clear();
}

void PrometheusMetrics::update_pipeline_stage(const PipelinePrometheusUpdate& update) {
    std::lock_guard lock(mutex_);
    auto& bucket = pipeline_fleet_[pipeline_key(update.direction, update.stage)];
    bucket.bytes = update.bytes;
    bucket.latency_ms = update.latency_ms;
    bucket.jitter_ms = update.jitter_ms;
    bucket.composite_score = update.composite_score;
    bucket.dropped_bytes = update.dropped_bytes;
}

void PrometheusMetrics::set_pipeline_active_sessions(int count) {
    pipeline_active_sessions_.store(count);
}

void PrometheusMetrics::set_pipeline_finished_sessions(int recent_count, uint64_t total_count) {
    pipeline_finished_sessions_.store(recent_count);
    pipeline_finished_sessions_total_.store(total_count);
}

PrometheusVqaSnapshot PrometheusMetrics::snapshot() const {
    PrometheusVqaSnapshot out;
    out.composite_score = last_composite_score_.load();
    out.snr_db = last_snr_db_.load();
    out.silence_ratio = last_silence_ratio_.load();
    std::lock_guard lock(mutex_);
    out.active_sessions = static_cast<int>(session_stt_ready_.size());
    for (const auto& [id, ready] : session_stt_ready_) {
        (void)id;
        if (ready) {
            ++out.stt_ready_sessions;
        }
    }
    return out;
}

std::string PrometheusMetrics::render_prometheus(const PrometheusVqaSnapshot& base) const {
    std::ostringstream out;
    out << "# HELP voiceqas_vqa_composite_score Last published VQA composite score (0-100).\n"
        << "# TYPE voiceqas_vqa_composite_score gauge\n"
        << "voiceqas_vqa_composite_score " << base.composite_score << "\n"
        << "# HELP voiceqas_vqa_snr_db Last published SNR estimate (dB).\n"
        << "# TYPE voiceqas_vqa_snr_db gauge\n"
        << "voiceqas_vqa_snr_db " << base.snr_db << "\n"
        << "# HELP voiceqas_vqa_silence_ratio Last published silence ratio (0-1).\n"
        << "# TYPE voiceqas_vqa_silence_ratio gauge\n"
        << "voiceqas_vqa_silence_ratio " << base.silence_ratio << "\n"
        << "# HELP voiceqas_vqa_active_sessions Sessions with recent VQA windows.\n"
        << "# TYPE voiceqas_vqa_active_sessions gauge\n"
        << "voiceqas_vqa_active_sessions " << base.active_sessions << "\n"
        << "# HELP voiceqas_vqa_stt_ready_sessions Sessions currently STT-ready per VQA gate.\n"
        << "# TYPE voiceqas_vqa_stt_ready_sessions gauge\n"
        << "voiceqas_vqa_stt_ready_sessions " << base.stt_ready_sessions << "\n"
        << "# HELP voiceqas_pipeline_active_sessions Active media pipeline sessions.\n"
        << "# TYPE voiceqas_pipeline_active_sessions gauge\n"
        << "voiceqas_pipeline_active_sessions " << pipeline_active_sessions_.load() << "\n"
        << "# HELP voiceqas_pipeline_finished_sessions Recently finished pipeline sessions in retention.\n"
        << "# TYPE voiceqas_pipeline_finished_sessions gauge\n"
        << "voiceqas_pipeline_finished_sessions " << pipeline_finished_sessions_.load() << "\n"
        << "# HELP voiceqas_pipeline_finished_sessions_total Total finished pipeline sessions since startup.\n"
        << "# TYPE voiceqas_pipeline_finished_sessions_total counter\n"
        << "voiceqas_pipeline_finished_sessions_total " << pipeline_finished_sessions_total_.load() << "\n";

    std::lock_guard lock(mutex_);
    for (const auto& [key, stage] : pipeline_fleet_) {
        const auto sep = key.find('|');
        const auto direction = key.substr(0, sep);
        const auto stage_name = key.substr(sep + 1);
        const auto labels = "direction=\"" + direction + "\",stage=\"" + stage_name + "\"";
        out << "# TYPE voiceqas_pipeline_bytes_total counter\n"
            << "voiceqas_pipeline_bytes_total{" << labels << "} " << stage.bytes << "\n"
            << "# TYPE voiceqas_pipeline_latency_ms gauge\n"
            << "voiceqas_pipeline_latency_ms{" << labels << "} " << stage.latency_ms << "\n"
            << "# TYPE voiceqas_pipeline_jitter_ms gauge\n"
            << "voiceqas_pipeline_jitter_ms{" << labels << "} " << stage.jitter_ms << "\n"
            << "# TYPE voiceqas_pipeline_composite_score gauge\n"
            << "voiceqas_pipeline_composite_score{" << labels << "} " << stage.composite_score << "\n"
            << "# TYPE voiceqas_pipeline_dropped_bytes_total counter\n"
            << "voiceqas_pipeline_dropped_bytes_total{" << labels << "} " << stage.dropped_bytes << "\n";
    }
    return out.str();
}

}  // namespace voiceqas::ops
