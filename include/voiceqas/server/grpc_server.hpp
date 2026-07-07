#pragma once

#include <memory>
#include <string>
#include <thread>

#include "voiceqas/analyzer.hpp"
#include "voiceqas/media/session.hpp"
#include "voiceqas/stt/session_manager.hpp"

namespace voiceqas {

class GrpcServer {
public:
    GrpcServer(std::string bind_addr,
               std::shared_ptr<VqaSessionManager> sessions,
               std::shared_ptr<stt::SttSessionManager> stt_sessions,
               std::shared_ptr<media::MediaSessionManager> media_sessions = nullptr);
    void run();
    void stop();

private:
    std::string bind_addr_;
    std::shared_ptr<VqaSessionManager> sessions_;
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
    std::shared_ptr<media::MediaSessionManager> media_sessions_;
};

}  // namespace voiceqas
