#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "voiceqas/audio/config.hpp"
#include "voiceqas/audio/decoder.hpp"
#include "voiceqas/audio/encoder.hpp"
#include "voiceqas/audio/normalizer.hpp"
#include "voiceqas/metrics.hpp"
#include "voiceqas/ports/pipeline_telemetry.hpp"
#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/rtp/packetizer.hpp"

namespace voiceqas::media {

struct MediaSessionConfig {
    std::string session_id;
    std::string channel_id = "default";
    AudioFormat format = AudioFormat::RtpG722;
    int sample_rate = 0;
    std::string remote_host;
    uint16_t remote_port = 0;
    std::string inbound_host;
    uint16_t inbound_port = 0;
    uint32_t inbound_ssrc = 0;
    uint32_t outbound_ssrc = 0;
};

struct OutboundAudioResult {
    std::vector<std::vector<uint8_t>> rtp_packets;
    size_t bytes_sent = 0;
    bool ok = false;
    std::string error;
};

class MediaSessionManager {
public:
    MediaSessionManager(
        audio::AudioProcessingConfig audio_config,
        audio::MediaRelayConfig relay_config,
        std::shared_ptr<ports::IPipelineTelemetry> telemetry = ports::noop_pipeline_telemetry());

    bool open_session(MediaSessionConfig config, std::string& error);
    bool close_session(const std::string& session_id);
    bool has_session(const std::string& session_id) const;

    OutboundAudioResult send_agent_pcm(
        const std::string& session_id,
        std::span<const int16_t> pcm,
        int input_sample_rate);

    std::optional<MediaSessionConfig> get_session_config(const std::string& session_id) const;
    std::vector<MediaSessionConfig> list_sessions() const;

    using RouteCallback =
        std::function<void(const std::string& host, uint16_t port, const std::string& session_id, bool bind)>;
    void set_route_callback(RouteCallback callback) { route_callback_ = std::move(callback); }

    using UdpSendFn = std::function<bool(const std::string& host, uint16_t port, std::span<const uint8_t> packet)>;
    void set_udp_sender(UdpSendFn sender) { udp_sender_ = std::move(sender); }

private:
    struct SessionState {
        MediaSessionConfig config;
        bool format_locked = false;
        rtp::RtpDepacketizer depacketizer;
        rtp::RtpPacketizer packetizer;
        audio::AgcState agc;
    };

    bool lock_format(SessionState& session, AudioFormat format, int sample_rate, std::string& error);

    audio::AudioProcessingConfig audio_config_;
    audio::MediaRelayConfig relay_config_;
    std::shared_ptr<ports::IPipelineTelemetry> telemetry_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionState> sessions_;
    UdpSendFn udp_sender_;
    RouteCallback route_callback_;
};

}  // namespace voiceqas::media
