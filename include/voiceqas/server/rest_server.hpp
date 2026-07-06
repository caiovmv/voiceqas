#pragma once

#include <memory>
#include <string>
#include <thread>

#include "voiceqas/analyzer.hpp"
#include "voiceqas/stt/session_manager.hpp"

namespace voiceqas {

class RestServer {
public:
    RestServer(std::string bind_addr,
               std::string web_root,
               std::string openapi_path,
               std::shared_ptr<SessionManager> sessions,
               std::shared_ptr<stt::SttSessionManager> stt_sessions);
    void run();
    void stop();

private:
    std::string bind_addr_;
    std::string web_root_;
    std::string openapi_path_;
    std::shared_ptr<SessionManager> sessions_;
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
    std::thread thread_;
    bool running_ = false;
};

}  // namespace voiceqas
