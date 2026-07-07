#include "voiceqas/media/rtp_ingress.hpp"

#include "voiceqas/audio/decoder.hpp"

namespace voiceqas::media {

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
        }
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
    return frame;
}

void RtpIngressProcessor::remove_session(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    sessions_.erase(session_id);
}

}  // namespace voiceqas::media
