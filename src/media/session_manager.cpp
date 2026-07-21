#include "voiceqas/media/session.hpp"

#include "voiceqas/tracing/tracing.hpp"

#include <chrono>

#include "voiceqas/stt/audio_prepare.hpp"

namespace voiceqas::media {

namespace {

uint32_t default_outbound_ssrc() {
    return 0xA9012345;
}

}  // namespace

MediaSessionManager::MediaSessionManager(
    audio::AudioProcessingConfig audio_config,
    audio::MediaRelayConfig relay_config,
    std::shared_ptr<ports::IPipelineTelemetry> telemetry)
    : audio_config_(std::move(audio_config)),
      relay_config_(std::move(relay_config)),
      telemetry_(std::move(telemetry)) {}

bool MediaSessionManager::lock_format(
    SessionState& session,
    AudioFormat format,
    int sample_rate,
    std::string& error) {
    if (session.format_locked) {
        if (session.config.format != format) {
            error = "format mismatch after lock";
            return false;
        }
        return true;
    }

    session.config.format = format;
    session.config.sample_rate = sample_rate > 0 ? sample_rate : sample_rate_for_format(format);
    if (session.config.outbound_ssrc == 0) {
        session.config.outbound_ssrc = default_outbound_ssrc();
    }

    const auto pt = static_cast<uint8_t>(audio::payload_type_for_format(format));
    const auto ts_step = audio::rtp_timestamp_step_for_format(format);
    session.packetizer = rtp::RtpPacketizer(pt, ts_step, session.config.outbound_ssrc);
    session.format_locked = true;
    return true;
}

bool MediaSessionManager::open_session(MediaSessionConfig config, std::string& error) {
    if (config.session_id.empty()) {
        error = "session_id required";
        return false;
    }

    std::lock_guard lock(mutex_);
    if (sessions_.contains(config.session_id)) {
        error = "session already exists";
        return false;
    }

    SessionState state;
    state.config = std::move(config);
    if (state.config.sample_rate <= 0) {
        state.config.sample_rate = sample_rate_for_format(state.config.format);
    }
    state.depacketizer = rtp::RtpDepacketizer(rtp_clock_rate_for_format(state.config.format));
    state.agc = audio::AgcState(audio_config_);

    const auto pt = static_cast<uint8_t>(audio::payload_type_for_format(state.config.format));
    const auto ts_step = audio::rtp_timestamp_step_for_format(state.config.format);
    if (state.config.outbound_ssrc == 0) {
        state.config.outbound_ssrc = default_outbound_ssrc();
    }
    state.packetizer = rtp::RtpPacketizer(pt, ts_step, state.config.outbound_ssrc);
    state.format_locked = true;

    const auto sid = state.config.session_id;
    const auto remote_host = state.config.remote_host;
    const auto remote_port = state.config.remote_port;
    const auto inbound_host = state.config.inbound_host;
    const auto inbound_port = state.config.inbound_port;

    sessions_.emplace(sid, std::move(state));

    if (route_callback_) {
        if (!remote_host.empty() && remote_port > 0) {
            route_callback_(remote_host, remote_port, sid, true);
        }
        if (!inbound_host.empty() && inbound_port > 0) {
            route_callback_(inbound_host, inbound_port, sid, true);
        }
    }
    return true;
}

bool MediaSessionManager::close_session(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }
    if (route_callback_) {
        if (!it->second.config.remote_host.empty() && it->second.config.remote_port > 0) {
            route_callback_(
                it->second.config.remote_host,
                it->second.config.remote_port,
                session_id,
                false);
        }
        if (!it->second.config.inbound_host.empty() && it->second.config.inbound_port > 0) {
            route_callback_(
                it->second.config.inbound_host,
                it->second.config.inbound_port,
                session_id,
                false);
        }
    }
    sessions_.erase(it);
    return true;
}

bool MediaSessionManager::has_session(const std::string& session_id) const {
    std::lock_guard lock(mutex_);
    return sessions_.contains(session_id);
}

std::optional<MediaSessionConfig> MediaSessionManager::get_session_config(
    const std::string& session_id) const {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second.config;
}

std::vector<MediaSessionConfig> MediaSessionManager::list_sessions() const {
    std::lock_guard lock(mutex_);
    std::vector<MediaSessionConfig> out;
    out.reserve(sessions_.size());
    for (const auto& [id, state] : sessions_) {
        (void)id;
        out.push_back(state.config);
    }
    return out;
}

OutboundAudioResult MediaSessionManager::send_agent_pcm(
    const std::string& session_id,
    std::span<const int16_t> pcm,
    int input_sample_rate) {
    OutboundAudioResult result;
    std::lock_guard lock(mutex_);

    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        result.error = "unknown session";
        return result;
    }

    auto& session = it->second;
    if (!session.format_locked) {
        result.error = "session format not locked";
        return result;
    }

    tracing::StageSpan resample_span("resample_out", "Resample Out", session_id);
    resample_span.set_bytes_in(pcm.size() * sizeof(int16_t));
    const auto resample_started = std::chrono::steady_clock::now();
    auto working = stt::resample_pcm16(pcm, input_sample_rate, session.config.sample_rate);
    const auto resample_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - resample_started)
                                 .count();
    resample_span.set_metric("voiceqas.duration_ms", resample_ms);
    resample_span.set_bytes_out(working.size() * sizeof(int16_t));

    tracing::StageSpan limit_span("peak_limit", "Peak Limiter", session_id);
    limit_span.set_bytes_in(working.size() * sizeof(int16_t));
    const auto limit_started = std::chrono::steady_clock::now();
    audio::apply_peak_limiter_inplace(working);
    const auto limiter_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - limit_started)
                                .count();
    limit_span.set_metric("voiceqas.duration_ms", limiter_ms);

    tracing::StageSpan encode_span("encode", "Encode", session_id);
    encode_span.set_bytes_in(working.size() * sizeof(int16_t));
    const auto encode_started = std::chrono::steady_clock::now();
    const auto encoded = audio::encode_from_pcm(session.config.format, working, &session.packetizer);
    const auto encode_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - encode_started)
                               .count();
    encode_span.set_metric("voiceqas.duration_ms", encode_ms);
    if (!encoded.ok) {
        encode_span.set_metric("voiceqas.error", encoded.error);
        result.error = encoded.error;
        return result;
    }
    encode_span.set_bytes_out(encoded.rtp_packets.empty() ? 0 : encoded.rtp_packets[0].size());

    result.rtp_packets = encoded.rtp_packets;
    size_t rtp_bytes = 0;
    if (udp_sender_ && !session.config.remote_host.empty() && session.config.remote_port > 0) {
        for (const auto& packet : result.rtp_packets) {
            if (udp_sender_(session.config.remote_host, session.config.remote_port, packet)) {
                result.bytes_sent += packet.size();
                rtp_bytes += packet.size();
            }
        }
    }

    ports::OutboundTimings timings{
        .resample_ms = resample_ms,
        .limiter_ms = limiter_ms,
        .encode_ms = encode_ms,
        .pcm_bytes = working.size() * sizeof(int16_t),
        .rtp_bytes = rtp_bytes,
        .rtp_packets = result.rtp_packets.size(),
    };
    telemetry_->record_outbound(session_id, timings);

    result.ok = true;
    return result;
}

}  // namespace voiceqas::media
