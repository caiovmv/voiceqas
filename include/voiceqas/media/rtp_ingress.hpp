#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "voiceqas/audio/config.hpp"
#include "voiceqas/audio/dsp/channel_strip.hpp"
#include "voiceqas/config/channel_registry.hpp"
#include "voiceqas/metrics.hpp"
#include "voiceqas/rtp/depacketizer.hpp"

namespace voiceqas::media {

struct DecodedRtpFrame {
    std::vector<int16_t> pcm;
    int sample_rate = 8000;
    rtp::RtpStats stats{};
    AudioFormat format = AudioFormat::RtpPcmu;
    bool ok = false;
    /** Wall time spent in strip AGC stage. */
    double agc_ms = 0.0;
    /** Wall time spent in RNNoise (NR) stage. */
    double enhancement_ms = 0.0;
    /** True when AGC stage ran. */
    bool normalized = false;
    /** True when RNNoise ran. */
    bool enhanced = false;
};

/**
 * Shared RTP ingress: depacketize + decode once, then VoiceChannelStrip
 * (NR → HPF → EQ → DeEss → Comp → Lim → AGC) so VQA/STT share cleaned PCM.
 */
class RtpIngressProcessor {
public:
    explicit RtpIngressProcessor(audio::AudioProcessingConfig audio_config = {});

    void set_channel_registry(std::shared_ptr<config::ChannelRegistry> registry);
    void set_session_channel(const std::string& session_id, const std::string& channel_id);

    std::optional<DecodedRtpFrame> decode(
        const std::string& session_id,
        AudioFormat format,
        std::span<const uint8_t> payload);

    void remove_session(const std::string& session_id);

private:
    struct SessionState {
        std::optional<rtp::RtpDepacketizer> depacketizer;
        AudioFormat format = AudioFormat::RtpPcmu;
        audio::VoiceChannelStrip strip;
        std::string channel_id = "default";
        bool strip_initialized = false;
    };

    audio::AudioProcessingConfig audio_config_for_channel(const std::string& channel_id) const;
    void ensure_strip(SessionState& session);

    audio::AudioProcessingConfig audio_config_;
    std::shared_ptr<config::ChannelRegistry> channel_registry_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionState> sessions_;
};

}  // namespace voiceqas::media
