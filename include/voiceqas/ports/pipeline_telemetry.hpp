#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "voiceqas/metrics.hpp"

namespace voiceqas::ports {

struct SttPrepareTimings {
    double decode_ms = 0.0;
    double agc_ms = 0.0;
    double enhancement_ms = 0.0;
    double resample_ms = 0.0;
    size_t pcm_bytes = 0;
    size_t payload_bytes = 0;
};

struct OutboundTimings {
    double resample_ms = 0.0;
    double limiter_ms = 0.0;
    double encode_ms = 0.0;
    size_t pcm_bytes = 0;
    size_t rtp_bytes = 0;
    uint64_t rtp_packets = 0;
};

class IPipelineTelemetry {
public:
    virtual ~IPipelineTelemetry() = default;

    virtual void record_rtp_ingress(
        const std::string& session_id,
        uint64_t bytes,
        double jitter_ms,
        double packet_loss_pct,
        bool accumulate_bytes = true) = 0;
    virtual void record_vqa_path(
        const std::string& session_id,
        uint64_t payload_bytes,
        uint64_t pcm_bytes,
        double decode_ms,
        double agc_ms,
        double enhancement_ms,
        double jitter_ms,
        double packet_loss_pct) = 0;
    virtual void record_vqa_window(const std::string& session_id, const WindowMetrics& metrics) = 0;
    virtual void record_stt_gate_drop(const std::string& session_id, uint64_t bytes) = 0;
    virtual void record_stt_prepare(const std::string& session_id, const SttPrepareTimings& timings) = 0;
    virtual void record_stt_buffer(const std::string& session_id, int sample_rate, size_t pcm_samples) = 0;
    virtual void set_session_codec(const std::string& session_id, const std::string& codec) = 0;
    virtual void finish_session(const std::string& session_id, const std::string& reason) = 0;
    virtual void remove_session(const std::string& session_id) = 0;
    virtual void on_session_removed(const std::string& session_id) = 0;
    virtual void record_vad(
        const std::string& session_id,
        double latency_ms,
        uint64_t bytes_in,
        uint64_t bytes_out) = 0;
    virtual void record_outbound(const std::string& session_id, const OutboundTimings& timings) = 0;
};

std::shared_ptr<IPipelineTelemetry> noop_pipeline_telemetry();

}  // namespace voiceqas::ports
