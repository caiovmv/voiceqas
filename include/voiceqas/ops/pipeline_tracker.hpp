#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "voiceqas/metrics.hpp"

namespace voiceqas::ops {

inline constexpr const char* kPipelineDirectionInbound = "inbound";
inline constexpr const char* kPipelineDirectionOutbound = "outbound";

namespace pipeline_stage {

inline constexpr const char* kSipIn = "sip_in";
inline constexpr const char* kRtpIngress = "rtp_ingress";
inline constexpr const char* kDecodeVqa = "decode_vqa";
inline constexpr const char* kAgcVqa = "agc_vqa";
inline constexpr const char* kEnhancement = "enhancement";
inline constexpr const char* kVqa = "vqa";
inline constexpr const char* kSttGate = "stt_gate";
inline constexpr const char* kDecodeStt = "decode_stt";
inline constexpr const char* kAgcStt = "agc_stt";
inline constexpr const char* kResample16k = "resample_16k";
inline constexpr const char* kSttBuffer = "stt_buffer";
inline constexpr const char* kVad = "vad";
inline constexpr const char* kAsr = "asr";
inline constexpr const char* kAiAgent = "ai_agent";
inline constexpr const char* kSttDropped = "stt_dropped";
inline constexpr const char* kAgentPcmIn = "agent_pcm_in";
inline constexpr const char* kResampleOut = "resample_out";
inline constexpr const char* kPeakLimit = "peak_limit";
inline constexpr const char* kEncode = "encode";
inline constexpr const char* kRtpPacketize = "rtp_packetize";
inline constexpr const char* kRtpEgress = "rtp_egress";
inline constexpr const char* kSipOut = "sip_out";

}  // namespace pipeline_stage

struct PipelineStageMetrics {
    uint64_t bytes_in = 0;
    uint64_t bytes_out = 0;
    uint64_t packets_in = 0;
    uint64_t packets_out = 0;
    uint64_t dropped_bytes = 0;
    uint64_t dropped_packets = 0;
    double latency_ms_p50 = 0.0;
    double latency_ms_p95 = 0.0;
    double bytes_per_sec = 0.0;
    double jitter_ms = 0.0;
    double packet_loss_pct = 0.0;
    double composite_score = 0.0;
    double snr_db = 0.0;
    double rms_dbfs = 0.0;
    bool stt_ready = false;
    int64_t processing_ms = 0;
    int64_t buffer_ms = 0;
    bool enabled = true;
};

struct SttPrepareTimings {
    double decode_ms = 0.0;
    double agc_ms = 0.0;
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

class PipelineTracker {
public:
    static PipelineTracker& instance();

    void record_rtp_ingress(const std::string& session_id, uint64_t bytes, double jitter_ms,
                            double packet_loss_pct, bool accumulate_bytes = true);
    void record_vqa_path(
        const std::string& session_id,
        uint64_t payload_bytes,
        uint64_t pcm_bytes,
        double decode_ms,
        double agc_ms,
        double jitter_ms,
        double packet_loss_pct);
    void record_vqa_window(const std::string& session_id, const WindowMetrics& metrics);
    void record_stt_gate_drop(const std::string& session_id, uint64_t bytes);
    void record_stt_prepare(const std::string& session_id, const SttPrepareTimings& timings);
    void record_stt_buffer(const std::string& session_id, int sample_rate, size_t pcm_samples);
    void record_vad(const std::string& session_id, double latency_ms, uint64_t bytes_in,
                    uint64_t bytes_out);
    void record_asr(const std::string& session_id, int64_t processing_ms, uint64_t bytes_in);
    void record_ai_agent_inbound(const std::string& session_id, uint64_t bytes);
    void record_outbound(const std::string& session_id, const OutboundTimings& timings);
    void set_session_codec(const std::string& session_id, const std::string& codec);
    void finish_session(const std::string& session_id, const std::string& reason);
    void remove_session(const std::string& session_id);

    nlohmann::json build_snapshot(const std::optional<std::string>& session_id);
    nlohmann::json list_sessions();
    void sync_fleet_prometheus();

private:
    PipelineTracker() = default;

    struct StageBucket {
        PipelineStageMetrics metrics;
        std::string direction;
    };

    struct SessionState {
        std::unordered_map<std::string, StageBucket> stages;
        int64_t first_seen_ms = 0;
        int64_t last_seen_ms = 0;
        std::string codec;
        int64_t last_snapshot_ms = 0;
    };

    struct FinishedSessionRecord {
        std::string session_id;
        std::string codec;
        std::string reason;
        double composite_score = 0.0;
        int64_t first_seen_ms = 0;
        int64_t finished_ms = 0;
        int64_t duration_ms = 0;
        std::unordered_map<std::string, StageBucket> stages;
    };

    static constexpr std::size_t kMaxFinishedSessions = 200;

    void finish_session_locked(const std::string& session_id, const std::string& reason);
    static nlohmann::json finished_session_to_json(const FinishedSessionRecord& record);
    static nlohmann::json active_session_to_json(const std::string& session_id, const SessionState& session);
    int count_active_sessions_locked(int64_t now) const;

    static int64_t now_ms();
    static void update_latency(StageBucket& bucket, double latency_ms);
    static void update_bytes_rate(StageBucket& bucket, uint64_t bytes, int64_t now_ms, int64_t& last_rate_ms,
                                 uint64_t& last_bytes);
    SessionState& session_state(const std::string& session_id);
    StageBucket& stage(SessionState& session, const std::string& stage_id, const char* direction);
    void prune_stale_sessions_locked();
    static void merge_session_stages_into(SessionState& fleet, const SessionState& session);
    nlohmann::json build_snapshot_from_state(const SessionState& session, const std::string& scope,
                                             const std::optional<std::string>& session_id) const;
    nlohmann::json aggregate_fleet_snapshot();

    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionState> sessions_;
    std::deque<FinishedSessionRecord> finished_sessions_;
    uint64_t finished_sessions_total_ = 0;
};

void publish_pipeline_snapshot(const std::string& session_id);

}  // namespace voiceqas::ops
