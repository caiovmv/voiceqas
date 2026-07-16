#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "voiceqas/analyzer.hpp"
#include "voiceqas/audio/config.hpp"
#include "voiceqas/config/channel_registry.hpp"
#include "voiceqas/media/session.hpp"
#include "voiceqas/media/rtp_ingress.hpp"
#include "voiceqas/ports/pipeline_telemetry.hpp"
#include "voiceqas/stt/session_manager.hpp"

namespace voiceqas {

class MediaRelayServer {
public:
    MediaRelayServer(
        std::string bind_addr,
        audio::MediaRelayConfig relay_config,
        audio::AudioProcessingConfig audio_config,
        std::shared_ptr<media::MediaSessionManager> media_sessions,
        std::shared_ptr<VqaSessionManager> vqa_sessions,
        std::shared_ptr<stt::SttSessionManager> stt_sessions,
        std::shared_ptr<ports::IPipelineTelemetry> telemetry,
        std::shared_ptr<config::ChannelRegistry> channel_registry = nullptr);

    ~MediaRelayServer();

    void run();
    void stop();

    void bind_endpoint(const std::string& host, uint16_t port, const std::string& session_id);
    void unbind_endpoint(const std::string& host, uint16_t port);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string bind_addr_;
    audio::MediaRelayConfig relay_config_;
    std::shared_ptr<media::MediaSessionManager> media_sessions_;
    std::shared_ptr<VqaSessionManager> vqa_sessions_;
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
    std::shared_ptr<ports::IPipelineTelemetry> telemetry_;
    std::shared_ptr<config::ChannelRegistry> channel_registry_;
    media::RtpIngressProcessor ingress_processor_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace voiceqas
