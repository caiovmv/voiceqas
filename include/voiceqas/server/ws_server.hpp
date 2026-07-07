#pragma once

#include <memory>
#include <string>
#include <thread>

#include "voiceqas/analyzer.hpp"
#include "voiceqas/stt/session_manager.hpp"

namespace voiceqas {

class WebSocketServer {
public:
    WebSocketServer(std::string bind_addr,
                    std::shared_ptr<VqaSessionManager> sessions,
                    std::shared_ptr<stt::SttSessionManager> stt_sessions);
    void run();
    void stop();

private:
    std::string bind_addr_;
    std::shared_ptr<VqaSessionManager> sessions_;
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
    std::thread thread_;
    bool running_ = false;
};

}  // namespace voiceqas
