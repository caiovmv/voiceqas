#include "voiceqas/ops/pipeline_tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "voiceqas/ops/metrics_hub.hpp"
#include "voiceqas/ops/pipeline_snapshot.hpp"
#include "voiceqas/ops/prometheus.hpp"
#include "voiceqas/ops/transport_sankey.hpp"

namespace voiceqas::ops {

namespace {

constexpr int64_t kSessionTtlMs = 60'000;

void touch_stage_metrics(PipelineStageMetrics& metrics, uint64_t bytes_in, uint64_t bytes_out,
                         double latency_ms) {
    metrics.bytes_in += bytes_in;
    metrics.bytes_out += bytes_out;
    if (bytes_in > 0) {
        ++metrics.packets_in;
    }
    if (bytes_out > 0) {
        ++metrics.packets_out;
    }
    if (latency_ms > 0.0) {
        if (metrics.latency_ms_p50 <= 0.0) {
            metrics.latency_ms_p50 = latency_ms;
            metrics.latency_ms_p95 = latency_ms;
        } else {
            metrics.latency_ms_p50 = metrics.latency_ms_p50 * 0.8 + latency_ms * 0.2;
            const auto tail = std::max(latency_ms, metrics.latency_ms_p95);
            metrics.latency_ms_p95 = metrics.latency_ms_p95 * 0.95 + tail * 0.05;
        }
    }
}

}  // namespace

PipelineTracker& PipelineTracker::instance() {
    static PipelineTracker tracker;
    return tracker;
}

int64_t PipelineTracker::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void PipelineTracker::update_latency(StageBucket& bucket, double latency_ms) {
    touch_stage_metrics(bucket.metrics, 0, 0, latency_ms);
}

void PipelineTracker::update_bytes_rate(
    StageBucket& bucket,
    uint64_t bytes,
    int64_t now_ms,
    int64_t& last_rate_ms,
    uint64_t& last_bytes) {
    if (last_rate_ms <= 0) {
        last_rate_ms = now_ms;
        last_bytes = bytes;
        return;
    }
    const auto delta_ms = now_ms - last_rate_ms;
    if (delta_ms <= 0) {
        return;
    }
    const auto delta_bytes = bytes > last_bytes ? bytes - last_bytes : 0;
    const auto rate = static_cast<double>(delta_bytes) * 1000.0 / static_cast<double>(delta_ms);
    if (bucket.metrics.bytes_per_sec <= 0.0) {
        bucket.metrics.bytes_per_sec = rate;
    } else {
        bucket.metrics.bytes_per_sec = bucket.metrics.bytes_per_sec * 0.7 + rate * 0.3;
    }
    last_rate_ms = now_ms;
    last_bytes = bytes;
}

PipelineTracker::SessionState& PipelineTracker::session_state(const std::string& session_id) {
    auto& session = sessions_[session_id];
    const auto now = now_ms();
    if (session.first_seen_ms <= 0) {
        session.first_seen_ms = now;
    }
    session.last_seen_ms = now;
    return session;
}

PipelineTracker::StageBucket& PipelineTracker::stage(
    SessionState& session,
    const std::string& stage_id,
    const char* direction) {
    auto& bucket = session.stages[stage_id];
    if (bucket.direction.empty()) {
        bucket.direction = direction;
    }
    return bucket;
}

PipelineTracker::StageBucket& PipelineTracker::transport(
    SessionState& session,
    const std::string& node_id,
    const char* direction) {
    auto& bucket = session.transports[node_id];
    if (bucket.direction.empty()) {
        bucket.direction = direction;
    }
    return bucket;
}

void PipelineTracker::touch_transport_rate(
    const std::string& session_id,
    const std::string& node_id,
    SessionState& session,
    StageBucket& bucket) {
    static thread_local std::unordered_map<std::string, int64_t> last_rate_ms;
    static thread_local std::unordered_map<std::string, uint64_t> last_bytes;
    const auto key = session_id + ":" + node_id + ":" + bucket.direction;
    update_bytes_rate(bucket, bucket.metrics.bytes_out, session.last_seen_ms, last_rate_ms[key], last_bytes[key]);
}

void PipelineTracker::record_transport_ingress(
    const std::string& session_id,
    const std::string& transport_id,
    uint64_t bytes,
    double latency_ms,
    double jitter_ms) {
    if (bytes == 0 && latency_ms <= 0.0) {
        return;
    }
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& node = transport(session, transport_id, kPipelineDirectionInbound);
    touch_stage_metrics(node.metrics, bytes, bytes, latency_ms);
    if (jitter_ms > 0.0) {
        node.metrics.jitter_ms = jitter_ms;
    }
    touch_transport_rate(session_id, transport_id, session, node);
    auto& media = transport(session, transport_node::kMediaPipeline, kPipelineDirectionInbound);
    touch_stage_metrics(media.metrics, bytes, bytes, latency_ms);
    touch_transport_rate(session_id, transport_node::kMediaPipeline, session, media);
}

void PipelineTracker::record_transport_egress(
    const std::string& session_id,
    const std::string& transport_id,
    uint64_t bytes,
    double latency_ms) {
    if (bytes == 0 && latency_ms <= 0.0) {
        return;
    }
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& media = transport(session, transport_node::kMediaPipeline, kPipelineDirectionOutbound);
    touch_stage_metrics(media.metrics, bytes, bytes, latency_ms);
    touch_transport_rate(session_id, transport_node::kMediaPipeline, session, media);
    auto& node = transport(session, transport_id, kPipelineDirectionOutbound);
    touch_stage_metrics(node.metrics, bytes, bytes, latency_ms);
    touch_transport_rate(session_id, transport_id, session, node);
}

void PipelineTracker::record_media_pipeline(
    const std::string& session_id,
    uint64_t bytes,
    double latency_ms,
    const char* direction) {
    if (bytes == 0 && latency_ms <= 0.0) {
        return;
    }
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& media = transport(session, transport_node::kMediaPipeline, direction);
    touch_stage_metrics(media.metrics, bytes, bytes, latency_ms);
    touch_transport_rate(session_id, transport_node::kMediaPipeline, session, media);
}

void PipelineTracker::record_external_ai_inbound(
    const std::string& session_id,
    uint64_t bytes,
    double latency_ms) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& external = transport(session, transport_node::kExternalAi, kPipelineDirectionInbound);
    touch_stage_metrics(external.metrics, bytes, bytes, latency_ms);
    touch_transport_rate(session_id, transport_node::kExternalAi, session, external);
    auto& media = transport(session, transport_node::kMediaPipeline, kPipelineDirectionInbound);
    touch_stage_metrics(media.metrics, 0, bytes, latency_ms);
    touch_transport_rate(session_id, transport_node::kMediaPipeline, session, media);
}

void PipelineTracker::record_external_ai_outbound(
    const std::string& session_id,
    uint64_t bytes,
    double latency_ms) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& external = transport(session, transport_node::kExternalAi, kPipelineDirectionOutbound);
    touch_stage_metrics(external.metrics, bytes, bytes, latency_ms);
    external.metrics.processing_ms = static_cast<int64_t>(latency_ms);
    touch_transport_rate(session_id, transport_node::kExternalAi, session, external);
    auto& media = transport(session, transport_node::kMediaPipeline, kPipelineDirectionOutbound);
    touch_stage_metrics(media.metrics, bytes, bytes, latency_ms);
    touch_transport_rate(session_id, transport_node::kMediaPipeline, session, media);
}

void PipelineTracker::record_rtp_ingress(
    const std::string& session_id,
    uint64_t bytes,
    double jitter_ms,
    double packet_loss_pct,
    bool accumulate_bytes) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& sip = stage(session, pipeline_stage::kSipIn, kPipelineDirectionInbound);
    auto& rtp = stage(session, pipeline_stage::kRtpIngress, kPipelineDirectionInbound);
    if (accumulate_bytes) {
        touch_stage_metrics(sip.metrics, 0, bytes, 0.0);
        touch_stage_metrics(rtp.metrics, bytes, bytes, 0.0);
        static thread_local std::unordered_map<std::string, int64_t> last_rate_ms;
        static thread_local std::unordered_map<std::string, uint64_t> last_bytes;
        update_bytes_rate(rtp, rtp.metrics.bytes_in, session.last_seen_ms, last_rate_ms[session_id],
                          last_bytes[session_id]);
        auto& sip_transport = transport(session, transport_node::kSipTrunk, kPipelineDirectionInbound);
        touch_stage_metrics(sip_transport.metrics, bytes, bytes, 0.0);
        if (jitter_ms > 0.0) {
            sip_transport.metrics.jitter_ms = jitter_ms;
        }
        touch_transport_rate(session_id, transport_node::kSipTrunk, session, sip_transport);
        auto& media = transport(session, transport_node::kMediaPipeline, kPipelineDirectionInbound);
        touch_stage_metrics(media.metrics, bytes, bytes, 0.0);
        if (jitter_ms > 0.0) {
            media.metrics.jitter_ms = jitter_ms;
        }
        touch_transport_rate(session_id, transport_node::kMediaPipeline, session, media);
    }
    if (jitter_ms > 0.0) {
        rtp.metrics.jitter_ms = jitter_ms;
    }
    if (packet_loss_pct > 0.0) {
        rtp.metrics.packet_loss_pct = packet_loss_pct;
    }
}

void PipelineTracker::record_vqa_path(
    const std::string& session_id,
    uint64_t payload_bytes,
    uint64_t pcm_bytes,
    double decode_ms,
    double agc_ms,
    double enhancement_ms,
    double jitter_ms,
    double packet_loss_pct) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& decode = stage(session, pipeline_stage::kDecodeVqa, kPipelineDirectionInbound);
    auto& agc = stage(session, pipeline_stage::kAgcVqa, kPipelineDirectionInbound);
    auto& enh = stage(session, pipeline_stage::kEnhancement, kPipelineDirectionInbound);
    touch_stage_metrics(decode.metrics, payload_bytes, pcm_bytes, decode_ms);
    touch_stage_metrics(agc.metrics, pcm_bytes, pcm_bytes, agc_ms);
    touch_stage_metrics(enh.metrics, pcm_bytes, pcm_bytes, enhancement_ms);
    enh.metrics.enabled = true;
    decode.metrics.jitter_ms = jitter_ms;
    decode.metrics.packet_loss_pct = packet_loss_pct;
}

void PipelineTracker::record_vqa_window(const std::string& session_id, const WindowMetrics& metrics) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& enh = stage(session, pipeline_stage::kEnhancement, kPipelineDirectionInbound);
    auto& vqa = stage(session, pipeline_stage::kVqa, kPipelineDirectionInbound);
    auto& gate = stage(session, pipeline_stage::kSttGate, kPipelineDirectionInbound);
    const auto pcm_bytes = enh.metrics.bytes_out > 0 ? enh.metrics.bytes_out : enh.metrics.bytes_in;
    touch_stage_metrics(vqa.metrics, pcm_bytes, pcm_bytes, static_cast<double>(metrics.window_start_ms > 0 ? 500 : 0));
    vqa.metrics.composite_score = metrics.composite_score;
    vqa.metrics.snr_db = metrics.snr_estimate_db;
    vqa.metrics.rms_dbfs = metrics.rms_dbfs;
    vqa.metrics.jitter_ms = metrics.jitter_ms;
    vqa.metrics.packet_loss_pct = metrics.packet_loss_pct;
    vqa.metrics.stt_ready = metrics.stt_ready;
    gate.metrics.stt_ready = metrics.stt_ready;
    gate.metrics.composite_score = metrics.composite_score;
}

void PipelineTracker::record_stt_gate_drop(const std::string& session_id, uint64_t bytes) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& gate = stage(session, pipeline_stage::kSttGate, kPipelineDirectionInbound);
    auto& dropped = stage(session, pipeline_stage::kSttDropped, kPipelineDirectionInbound);
    gate.metrics.dropped_bytes += bytes;
    gate.metrics.dropped_packets += 1;
    touch_stage_metrics(dropped.metrics, bytes, bytes, 0.0);
}

void PipelineTracker::record_stt_prepare(const std::string& session_id, const SttPrepareTimings& timings) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& gate = stage(session, pipeline_stage::kSttGate, kPipelineDirectionInbound);
    auto& decode = stage(session, pipeline_stage::kDecodeStt, kPipelineDirectionInbound);
    auto& agc = stage(session, pipeline_stage::kAgcStt, kPipelineDirectionInbound);
    auto& resample = stage(session, pipeline_stage::kResample16k, kPipelineDirectionInbound);
    touch_stage_metrics(gate.metrics, timings.payload_bytes, timings.payload_bytes, 0.0);
    touch_stage_metrics(decode.metrics, timings.payload_bytes, timings.pcm_bytes, timings.decode_ms);
    touch_stage_metrics(agc.metrics, timings.pcm_bytes, timings.pcm_bytes, timings.agc_ms);
    auto& enh = stage(session, pipeline_stage::kEnhancement, kPipelineDirectionInbound);
    touch_stage_metrics(enh.metrics, timings.pcm_bytes, timings.pcm_bytes, timings.enhancement_ms);
    enh.metrics.enabled = true;
    touch_stage_metrics(resample.metrics, timings.pcm_bytes, timings.pcm_bytes, timings.resample_ms);
}

void PipelineTracker::record_stt_buffer(const std::string& session_id, int sample_rate, size_t pcm_samples) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& buffer = stage(session, pipeline_stage::kSttBuffer, kPipelineDirectionInbound);
    const auto bytes = pcm_samples * sizeof(int16_t);
    buffer.metrics.bytes_in = std::max(buffer.metrics.bytes_in, bytes);
    buffer.metrics.bytes_out = buffer.metrics.bytes_in;
    buffer.metrics.buffer_ms = sample_rate > 0
        ? static_cast<int64_t>(pcm_samples) * 1000 / sample_rate
        : 0;
}

void PipelineTracker::record_vad(
    const std::string& session_id,
    double latency_ms,
    uint64_t bytes_in,
    uint64_t bytes_out) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& vad = stage(session, pipeline_stage::kVad, kPipelineDirectionInbound);
    touch_stage_metrics(vad.metrics, bytes_in, bytes_out, latency_ms);
}

void PipelineTracker::record_asr(const std::string& session_id, int64_t processing_ms, uint64_t bytes_in) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& asr = stage(session, pipeline_stage::kAsr, kPipelineDirectionInbound);
    touch_stage_metrics(asr.metrics, bytes_in, bytes_in, static_cast<double>(processing_ms));
    asr.metrics.processing_ms = processing_ms;
}

void PipelineTracker::record_ai_agent_inbound(const std::string& session_id, uint64_t bytes) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& agent = stage(session, pipeline_stage::kAiAgent, kPipelineDirectionInbound);
    touch_stage_metrics(agent.metrics, bytes, bytes, 0.0);
}

void PipelineTracker::record_outbound(const std::string& session_id, const OutboundTimings& timings) {
    std::lock_guard lock(mutex_);
    auto& session = session_state(session_id);
    auto& agent = stage(session, pipeline_stage::kAiAgent, kPipelineDirectionOutbound);
    auto& pcm_in = stage(session, pipeline_stage::kAgentPcmIn, kPipelineDirectionOutbound);
    auto& resample = stage(session, pipeline_stage::kResampleOut, kPipelineDirectionOutbound);
    auto& limit = stage(session, pipeline_stage::kPeakLimit, kPipelineDirectionOutbound);
    auto& encode = stage(session, pipeline_stage::kEncode, kPipelineDirectionOutbound);
    auto& packetize = stage(session, pipeline_stage::kRtpPacketize, kPipelineDirectionOutbound);
    auto& egress = stage(session, pipeline_stage::kRtpEgress, kPipelineDirectionOutbound);
    auto& sip = stage(session, pipeline_stage::kSipOut, kPipelineDirectionOutbound);
    touch_stage_metrics(agent.metrics, timings.pcm_bytes, timings.pcm_bytes, 0.0);
    touch_stage_metrics(pcm_in.metrics, timings.pcm_bytes, timings.pcm_bytes, 0.0);
    touch_stage_metrics(resample.metrics, timings.pcm_bytes, timings.pcm_bytes, timings.resample_ms);
    touch_stage_metrics(limit.metrics, timings.pcm_bytes, timings.pcm_bytes, timings.limiter_ms);
    touch_stage_metrics(encode.metrics, timings.pcm_bytes, timings.rtp_bytes, timings.encode_ms);
    touch_stage_metrics(packetize.metrics, timings.rtp_bytes, timings.rtp_bytes, 0.0);
    packetize.metrics.packets_out += timings.rtp_packets;
    touch_stage_metrics(egress.metrics, timings.rtp_bytes, timings.rtp_bytes, 0.0);
    egress.metrics.packets_out += timings.rtp_packets;
    touch_stage_metrics(sip.metrics, timings.rtp_bytes, timings.rtp_bytes, 0.0);
    auto& sip_transport = transport(session, transport_node::kSipTrunk, kPipelineDirectionOutbound);
    touch_stage_metrics(sip_transport.metrics, timings.rtp_bytes, timings.rtp_bytes, 0.0);
    touch_transport_rate(session_id, transport_node::kSipTrunk, session, sip_transport);
    auto& media = transport(session, transport_node::kMediaPipeline, kPipelineDirectionOutbound);
    touch_stage_metrics(media.metrics, timings.pcm_bytes, timings.rtp_bytes, timings.encode_ms);
    touch_transport_rate(session_id, transport_node::kMediaPipeline, session, media);
}

void PipelineTracker::set_session_codec(const std::string& session_id, const std::string& codec) {
    std::lock_guard lock(mutex_);
    session_state(session_id).codec = codec;
}

nlohmann::json PipelineTracker::finished_session_to_json(const FinishedSessionRecord& record) {
    return {
        {"session_id", record.session_id},
        {"codec", record.codec},
        {"reason", record.reason},
        {"composite_score", record.composite_score},
        {"first_seen_ms", record.first_seen_ms},
        {"finished_ms", record.finished_ms},
        {"duration_ms", record.duration_ms},
    };
}

nlohmann::json PipelineTracker::active_session_to_json(
    const std::string& session_id,
    const SessionState& session) {
    double composite_score = 0.0;
    if (const auto it = session.stages.find(pipeline_stage::kVqa); it != session.stages.end()) {
        composite_score = it->second.metrics.composite_score;
    }
    return {
        {"session_id", session_id},
        {"first_seen_ms", session.first_seen_ms},
        {"last_seen_ms", session.last_seen_ms},
        {"codec", session.codec},
        {"composite_score", composite_score},
    };
}

int PipelineTracker::count_active_sessions_locked(int64_t now) const {
    int count = 0;
    for (const auto& [session_id, session] : sessions_) {
        (void)session_id;
        if (now - session.last_seen_ms <= kSessionTtlMs) {
            ++count;
        }
    }
    return count;
}

void PipelineTracker::finish_session_locked(const std::string& session_id, const std::string& reason) {
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return;
    }

    FinishedSessionRecord record;
    record.session_id = session_id;
    record.codec = it->second.codec;
    record.reason = reason;
    record.first_seen_ms = it->second.first_seen_ms > 0 ? it->second.first_seen_ms : it->second.last_seen_ms;
    record.finished_ms = now_ms();
    record.duration_ms = record.finished_ms - record.first_seen_ms;
    if (const auto vqa_it = it->second.stages.find(pipeline_stage::kVqa); vqa_it != it->second.stages.end()) {
        record.composite_score = vqa_it->second.metrics.composite_score;
    }
    record.stages = it->second.stages;
    record.transports = it->second.transports;

    finished_sessions_.push_front(record);
    if (finished_sessions_.size() > kMaxFinishedSessions) {
        finished_sessions_.pop_back();
    }
    ++finished_sessions_total_;
    sessions_.erase(it);
}

void PipelineTracker::finish_session(const std::string& session_id, const std::string& reason) {
    std::lock_guard lock(mutex_);
    finish_session_locked(session_id, reason);
}

void PipelineTracker::remove_session(const std::string& session_id) {
    finish_session(session_id, "closed");
}

void PipelineTracker::prune_stale_sessions_locked() {
    const auto now = now_ms();
    std::vector<std::string> stale;
    stale.reserve(sessions_.size());
    for (const auto& [session_id, session] : sessions_) {
        if (now - session.last_seen_ms > kSessionTtlMs) {
            stale.push_back(session_id);
        }
    }
    for (const auto& session_id : stale) {
        finish_session_locked(session_id, "idle_timeout");
    }
}

nlohmann::json PipelineTracker::build_snapshot_from_state(
    const SessionState& session,
    const std::string& scope,
    const std::optional<std::string>& session_id) const {
    std::unordered_map<std::string, PipelineStageMetrics> stages;
    for (const auto& [name, bucket] : session.stages) {
        stages[name] = bucket.metrics;
    }
    auto sankey = build_sankey_json(stages, sankey_link_defs(), sankey_node_defs());
    std::unordered_map<std::string, PipelineStageMetrics> transport_metrics;
    for (const auto& [name, bucket] : session.transports) {
        transport_metrics[name] = bucket.metrics;
    }
    const auto transport_sankey = build_transport_sankey_pair(transport_metrics);
    nlohmann::json out = {
        {"scope", scope},
        {"ts_ms", now_ms()},
        {"codec", session.codec},
        {"nodes", sankey["nodes"]},
        {"links", sankey["links"]},
        {"echarts", build_echarts_sankey_option(sankey["nodes"], sankey["links"])},
        {"transport_sankey", transport_sankey},
    };
    if (session_id) {
        out["session_id"] = *session_id;
    }
    return out;
}

void PipelineTracker::merge_session_stages_into(SessionState& fleet, const SessionState& session) {
    for (const auto& [stage_name, bucket] : session.stages) {
        auto& target = fleet.stages[stage_name];
        if (target.direction.empty()) {
            target.direction = bucket.direction;
        }
        auto& m = target.metrics;
        const auto& s = bucket.metrics;
        m.bytes_in += s.bytes_in;
        m.bytes_out += s.bytes_out;
        m.packets_in += s.packets_in;
        m.packets_out += s.packets_out;
        m.dropped_bytes += s.dropped_bytes;
        m.dropped_packets += s.dropped_packets;
        m.latency_ms_p50 = m.latency_ms_p50 <= 0.0 ? s.latency_ms_p50
            : (m.latency_ms_p50 + s.latency_ms_p50) * 0.5;
        m.latency_ms_p95 = std::max(m.latency_ms_p95, s.latency_ms_p95);
        m.bytes_per_sec += s.bytes_per_sec;
        m.jitter_ms = m.jitter_ms <= 0.0 ? s.jitter_ms : (m.jitter_ms + s.jitter_ms) * 0.5;
        m.packet_loss_pct = std::max(m.packet_loss_pct, s.packet_loss_pct);
        if (stage_name == pipeline_stage::kVqa) {
            m.composite_score = m.composite_score <= 0.0 ? s.composite_score
                : (m.composite_score + s.composite_score) * 0.5;
            m.snr_db = m.snr_db <= 0.0 ? s.snr_db : (m.snr_db + s.snr_db) * 0.5;
        }
        m.processing_ms = std::max(m.processing_ms, s.processing_ms);
        m.buffer_ms = std::max(m.buffer_ms, s.buffer_ms);
        m.enabled = s.enabled;
    }
}

void PipelineTracker::merge_session_transports_into(SessionState& fleet, const SessionState& session) {
    for (const auto& [node_name, bucket] : session.transports) {
        auto& target = fleet.transports[node_name];
        if (target.direction.empty()) {
            target.direction = bucket.direction;
        }
        auto& m = target.metrics;
        const auto& s = bucket.metrics;
        m.bytes_in += s.bytes_in;
        m.bytes_out += s.bytes_out;
        m.packets_in += s.packets_in;
        m.packets_out += s.packets_out;
        m.latency_ms_p50 = m.latency_ms_p50 <= 0.0 ? s.latency_ms_p50
            : (m.latency_ms_p50 + s.latency_ms_p50) * 0.5;
        m.latency_ms_p95 = std::max(m.latency_ms_p95, s.latency_ms_p95);
        m.bytes_per_sec += s.bytes_per_sec;
        m.jitter_ms = m.jitter_ms <= 0.0 ? s.jitter_ms : (m.jitter_ms + s.jitter_ms) * 0.5;
        m.processing_ms = std::max(m.processing_ms, s.processing_ms);
    }
}

nlohmann::json PipelineTracker::aggregate_fleet_snapshot() {
    SessionState fleet;
    fleet.last_seen_ms = now_ms();
    const auto now = now_ms();
    for (const auto& [session_id, session] : sessions_) {
        (void)session_id;
        if (now - session.last_seen_ms > kSessionTtlMs) {
            continue;
        }
        merge_session_stages_into(fleet, session);
        merge_session_transports_into(fleet, session);
    }
    for (const auto& record : finished_sessions_) {
        SessionState finished;
        finished.codec = record.codec;
        finished.stages = record.stages;
        finished.transports = record.transports;
        merge_session_stages_into(fleet, finished);
        merge_session_transports_into(fleet, finished);
    }
    return build_snapshot_from_state(fleet, "fleet", std::nullopt);
}

nlohmann::json PipelineTracker::build_snapshot(const std::optional<std::string>& session_id) {
    std::lock_guard lock(mutex_);
    prune_stale_sessions_locked();
    if (session_id) {
        const auto it = sessions_.find(*session_id);
        if (it != sessions_.end()) {
            return build_snapshot_from_state(it->second, "session", session_id);
        }
        for (const auto& record : finished_sessions_) {
            if (record.session_id == *session_id) {
                SessionState finished;
                finished.codec = record.codec;
                finished.stages = record.stages;
                finished.transports = record.transports;
                return build_snapshot_from_state(finished, "session", session_id);
            }
        }
        return {
            {"scope", "session"},
            {"session_id", *session_id},
            {"ts_ms", now_ms()},
            {"nodes", nlohmann::json::array()},
            {"links", nlohmann::json::array()},
            {"echarts", build_echarts_sankey_option(nlohmann::json::array(), nlohmann::json::array())},
        };
    }
    return aggregate_fleet_snapshot();
}

nlohmann::json PipelineTracker::list_sessions() {
    std::lock_guard lock(mutex_);
    prune_stale_sessions_locked();
    const auto now = now_ms();
    nlohmann::json active = nlohmann::json::array();
    for (const auto& [session_id, session] : sessions_) {
        if (now - session.last_seen_ms > kSessionTtlMs) {
            continue;
        }
        active.push_back(active_session_to_json(session_id, session));
    }

    nlohmann::json finished = nlohmann::json::array();
    for (const auto& record : finished_sessions_) {
        finished.push_back(finished_session_to_json(record));
    }

    nlohmann::json session_ids = nlohmann::json::array();
    for (const auto& row : active) {
        session_ids.push_back(row.at("session_id"));
    }
    for (const auto& row : finished) {
        const auto& id = row.at("session_id");
        if (std::none_of(session_ids.begin(), session_ids.end(),
                [&id](const nlohmann::json& existing) { return existing == id; })) {
            session_ids.push_back(id);
        }
    }

    return {
        {"status", "ok"},
        {"sessions", active},
        {"active_sessions", active},
        {"finished_sessions", finished},
        {"session_ids", session_ids},
        {"finished_sessions_total", finished_sessions_total_},
    };
}

void PipelineTracker::sync_fleet_prometheus() {
    std::lock_guard lock(mutex_);
    const auto now = now_ms();
    PrometheusMetrics::instance().reset_pipeline_fleet();
    PrometheusMetrics::instance().set_pipeline_active_sessions(count_active_sessions_locked(now));
    PrometheusMetrics::instance().set_pipeline_finished_sessions(
        static_cast<int>(finished_sessions_.size()),
        finished_sessions_total_);

    std::unordered_map<std::string, StageBucket> fleet_stages;
    for (const auto& [session_id, session] : sessions_) {
        (void)session_id;
        for (const auto& [stage_name, bucket] : session.stages) {
            auto& target = fleet_stages[stage_name];
            if (target.direction.empty()) {
                target.direction = bucket.direction;
            }
            auto& m = target.metrics;
            const auto& s = bucket.metrics;
            m.bytes_out += s.bytes_out;
            m.latency_ms_p50 = m.latency_ms_p50 <= 0.0 ? s.latency_ms_p50
                : (m.latency_ms_p50 + s.latency_ms_p50) * 0.5;
            m.jitter_ms = std::max(m.jitter_ms, s.jitter_ms);
            m.composite_score = stage_name == pipeline_stage::kVqa && s.composite_score > 0.0
                ? (m.composite_score <= 0.0 ? s.composite_score : (m.composite_score + s.composite_score) * 0.5)
                : m.composite_score;
            m.dropped_bytes += s.dropped_bytes;
        }
    }

    for (const auto& [stage_name, bucket] : fleet_stages) {
        PipelinePrometheusUpdate update;
        update.stage = stage_name;
        update.direction = bucket.direction;
        update.bytes = bucket.metrics.bytes_out;
        update.latency_ms = bucket.metrics.latency_ms_p50;
        update.jitter_ms = bucket.metrics.jitter_ms;
        update.composite_score = bucket.metrics.composite_score;
        update.dropped_bytes = bucket.metrics.dropped_bytes;
        PrometheusMetrics::instance().update_pipeline_stage(update);
    }
}

void publish_pipeline_snapshot(const std::string& session_id) {
    auto& tracker = PipelineTracker::instance();
    auto json = tracker.build_snapshot(session_id);
    tracker.sync_fleet_prometheus();
    json["type"] = "pipeline_snapshot";
    json["session_id"] = session_id;
    OpsMetricsHub::instance().publish_event(std::move(json));
}

}  // namespace voiceqas::ops
