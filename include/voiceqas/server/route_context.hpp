#pragma once

#include <memory>
#include <string>

#include "voiceqas/analyzer.hpp"
#include "voiceqas/audio/config.hpp"
#include "voiceqas/config/channel_registry.hpp"
#include "voiceqas/media/session.hpp"
#include "voiceqas/stt/session_manager.hpp"

namespace voiceqas {

struct RouteContext {
    std::string web_root;
    std::string openapi_path;
    /// Target for HTTP playground → real gRPC (e.g. "0.0.0.0:50051" or "127.0.0.1:50051").
    std::string grpc_target;
    audio::MediaRelayConfig media_config;
    std::shared_ptr<VqaSessionManager> sessions;
    std::shared_ptr<stt::SttSessionManager> stt_sessions;
    std::shared_ptr<media::MediaSessionManager> media_sessions;
    std::shared_ptr<config::ChannelRegistry> channel_registry;
};

}  // namespace voiceqas
