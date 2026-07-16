#include "voiceqas/ops/adapters/ops_ports.hpp"

#include "voiceqas/ops/metrics_hub.hpp"
#include "voiceqas/ops/pipeline_tracker.hpp"
#include "voiceqas/ops/prometheus.hpp"

namespace voiceqas::ops {
namespace {

class OpsMetricsPublisherAdapter final : public ports::IMetricsPublisher {
public:
    void publish_vqa(const std::string& session_id, const WindowMetrics& metrics) override {
        OpsMetricsHub::instance().publish(session_id, metrics);
    }

    void publish_stt(
        const std::string& session_id,
        const stt::TranscriptResult& result,
        bool partial) override {
        publish_stt_result(session_id, result, partial);
    }
};

class OpsPipelineTelemetryAdapter final : public ports::IPipelineTelemetry {
public:
    void record_rtp_ingress(
        const std::string& session_id,
        uint64_t bytes,
        double jitter_ms,
        double packet_loss_pct,
        bool accumulate_bytes) override {
        PipelineTracker::instance().record_rtp_ingress(
            session_id, bytes, jitter_ms, packet_loss_pct, accumulate_bytes);
    }

    void record_vqa_path(
        const std::string& session_id,
        uint64_t payload_bytes,
        uint64_t pcm_bytes,
        double decode_ms,
        double agc_ms,
        double enhancement_ms,
        double jitter_ms,
        double packet_loss_pct) override {
        PipelineTracker::instance().record_vqa_path(
            session_id,
            payload_bytes,
            pcm_bytes,
            decode_ms,
            agc_ms,
            enhancement_ms,
            jitter_ms,
            packet_loss_pct);
    }

    void record_vqa_window(const std::string& session_id, const WindowMetrics& metrics) override {
        PipelineTracker::instance().record_vqa_window(session_id, metrics);
    }

    void record_stt_gate_drop(const std::string& session_id, uint64_t bytes) override {
        PipelineTracker::instance().record_stt_gate_drop(session_id, bytes);
    }

    void record_stt_prepare(
        const std::string& session_id,
        const ports::SttPrepareTimings& timings) override {
        SttPrepareTimings ops_timings{
            .decode_ms = timings.decode_ms,
            .agc_ms = timings.agc_ms,
            .enhancement_ms = timings.enhancement_ms,
            .resample_ms = timings.resample_ms,
            .pcm_bytes = timings.pcm_bytes,
            .payload_bytes = timings.payload_bytes,
        };
        PipelineTracker::instance().record_stt_prepare(session_id, ops_timings);
    }

    void record_stt_buffer(const std::string& session_id, int sample_rate, size_t pcm_samples) override {
        PipelineTracker::instance().record_stt_buffer(session_id, sample_rate, pcm_samples);
    }

    void set_session_codec(const std::string& session_id, const std::string& codec) override {
        PipelineTracker::instance().set_session_codec(session_id, codec);
    }

    void finish_session(const std::string& session_id, const std::string& reason) override {
        PipelineTracker::instance().finish_session(session_id, reason);
    }

    void remove_session(const std::string& session_id) override {
        PipelineTracker::instance().remove_session(session_id);
    }

    void on_session_removed(const std::string& session_id) override {
        PrometheusMetrics::instance().on_session_removed(session_id);
    }

    void record_vad(
        const std::string& session_id,
        double latency_ms,
        uint64_t bytes_in,
        uint64_t bytes_out) override {
        PipelineTracker::instance().record_vad(session_id, latency_ms, bytes_in, bytes_out);
    }

    void record_outbound(const std::string& session_id, const ports::OutboundTimings& timings) override {
        OutboundTimings ops_timings{
            .resample_ms = timings.resample_ms,
            .limiter_ms = timings.limiter_ms,
            .encode_ms = timings.encode_ms,
            .pcm_bytes = timings.pcm_bytes,
            .rtp_bytes = timings.rtp_bytes,
            .rtp_packets = timings.rtp_packets,
        };
        PipelineTracker::instance().record_outbound(session_id, ops_timings);
    }
};

}  // namespace

std::shared_ptr<ports::IMetricsPublisher> make_ops_metrics_publisher() {
    static auto instance = std::make_shared<OpsMetricsPublisherAdapter>();
    return instance;
}

std::shared_ptr<ports::IPipelineTelemetry> make_ops_pipeline_telemetry() {
    static auto instance = std::make_shared<OpsPipelineTelemetryAdapter>();
    return instance;
}

}  // namespace voiceqas::ops
