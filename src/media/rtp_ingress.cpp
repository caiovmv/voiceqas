#include "voiceqas/media/rtp_ingress.hpp"

#include "voiceqas/audio/decoder.hpp"

namespace voiceqas::media {

RtpIngressProcessor::RtpIngressProcessor(audio::AudioProcessingConfig audio_config)
    : audio_config_(std::move(audio_config)) {
    audio_config_.sync_legacy_from_strip();
}

void RtpIngressProcessor::set_channel_registry(std::shared_ptr<config::ChannelRegistry> registry) {
    std::lock_guard lock(mutex_);
    channel_registry_ = std::move(registry);
}

void RtpIngressProcessor::set_session_channel(const std::string& session_id, const std::string& channel_id) {
    std::lock_guard lock(mutex_);
    auto& session = sessions_[session_id];
    if (session.channel_id != channel_id) {
        session.channel_id = channel_id.empty() ? "default" : channel_id;
        session.strip_initialized = false;
    }
}

audio::AudioProcessingConfig RtpIngressProcessor::audio_config_for_channel(
    const std::string& channel_id) const {
    if (channel_registry_) {
        return channel_registry_->resolve_audio(channel_id);
    }
    return audio_config_;
}

void RtpIngressProcessor::ensure_strip(SessionState& session) {
    if (session.strip_initialized) {
        return;
    }
    const auto cfg = audio_config_for_channel(session.channel_id);
    session.strip = audio::VoiceChannelStrip(cfg);
    session.strip_initialized = true;
}

std::optional<DecodedRtpFrame> RtpIngressProcessor::decode(
    const std::string& session_id,
    AudioFormat format,
    std::span<const uint8_t> payload) {
    rtp::RtpDepacketizer* depacketizer_ptr = nullptr;
    {
        std::lock_guard lock(mutex_);
        auto& session = sessions_[session_id];
        if (!session.depacketizer || session.format != format) {
            session.format = format;
            session.depacketizer.emplace(rtp_clock_rate_for_format(format));
            session.strip_initialized = false;
        }
        ensure_strip(session);
        depacketizer_ptr = &(*session.depacketizer);
    }

    const auto decoded = audio::decode_to_pcm(format, payload, depacketizer_ptr);
    if (!decoded.ok) {
        return std::nullopt;
    }

    DecodedRtpFrame frame;
    frame.pcm = std::move(decoded.pcm);
    frame.sample_rate = sample_rate_for_format(format);
    frame.stats = decoded.rtp_stats;
    frame.format = format;
    frame.ok = true;

    if (!frame.pcm.empty()) {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return frame;
        }
        ensure_strip(it->second);
        const auto timings = it->second.strip.process_inplace(frame.pcm, frame.sample_rate);
        frame.agc_ms = timings.agc_ms;
        frame.enhancement_ms = timings.nr_ms;
        frame.normalized = timings.agc_applied;
        frame.enhanced = timings.nr_applied;
    }

    return frame;
}

void RtpIngressProcessor::remove_session(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    sessions_.erase(session_id);
}

}  // namespace voiceqas::media
