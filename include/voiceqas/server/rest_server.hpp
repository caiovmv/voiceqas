#pragma once

#include <memory>
#include <string>
#include <thread>

#include "voiceqas/analyzer.hpp"
#include "voiceqas/audio/config.hpp"
#include "voiceqas/config/channel_registry.hpp"
#include "voiceqas/media/session.hpp"
#include "voiceqas/stt/session_manager.hpp"

namespace voiceqas {

class RestServer {
public:
    RestServer(std::string bind_addr,
               std::string web_root,
               std::string openapi_path,
               audio::MediaRelayConfig media_config,
               std::shared_ptr<VqaSessionManager> sessions,
               std::shared_ptr<stt::SttSessionManager> stt_sessions,
               std::shared_ptr<media::MediaSessionManager> media_sessions = nullptr,
               std::string grpc_target = "127.0.0.1:50051",
               std::shared_ptr<config::ChannelRegistry> channel_registry = nullptr);
    void run();
    void stop();

private:
    std::string bind_addr_;
    std::string web_root_;
    std::string openapi_path_;
    std::string grpc_target_;
    audio::MediaRelayConfig media_config_;
    std::shared_ptr<VqaSessionManager> sessions_;
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
    std::shared_ptr<media::MediaSessionManager> media_sessions_;
    std::shared_ptr<config::ChannelRegistry> channel_registry_;
    std::thread thread_;
    bool running_ = false;
};

}  // namespace voiceqas
