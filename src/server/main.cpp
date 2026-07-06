#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include "voiceqas/analyzer.hpp"
#include "voiceqas/config.hpp"
#include "voiceqas/server/grpc_server.hpp"
#include "voiceqas/server/rest_server.hpp"
#include "voiceqas/server/ws_server.hpp"
#include "voiceqas/stt/client.hpp"
#include "voiceqas/stt/session_manager.hpp"

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) {
    g_running = false;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const auto server_cfg = voiceqas::load_config(argc, argv);
    auto sessions = std::make_shared<voiceqas::SessionManager>(voiceqas::global_analyzer_config());

    const auto stt_cfg = voiceqas::global_stt_config();
    auto stt_engine = std::make_shared<voiceqas::stt::SttEngine>(stt_cfg);
    auto stt_sessions = std::make_shared<voiceqas::stt::SttSessionManager>(
        stt_engine, stt_cfg, stt_cfg.target_sample_rate);

    voiceqas::RestServer rest(
        server_cfg.rest_addr,
        server_cfg.web_root,
        server_cfg.openapi_path,
        sessions,
        stt_sessions);
    voiceqas::WebSocketServer ws(server_cfg.ws_addr, sessions, stt_sessions);

    rest.run();
    ws.run();

    std::thread grpc_thread([&server_cfg, sessions, stt_sessions]() {
        voiceqas::GrpcServer grpc(server_cfg.grpc_addr, sessions, stt_sessions);
        grpc.run();
    });

    const auto ready = stt_engine->ready_status();
    std::cout << "voiceqas started\n"
              << "  REST: " << server_cfg.rest_addr << '\n'
              << "  gRPC: " << server_cfg.grpc_addr << '\n'
              << "  WS:   " << server_cfg.ws_addr << '\n'
              << "  STT:  embedded sherpa-onnx (parakeet="
              << (ready.parakeet_ready ? "ready" : "missing") << ", whisper="
              << (ready.whisper_ready ? "ready" : "missing") << ")\n";

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    rest.stop();
    ws.stop();
    if (grpc_thread.joinable()) {
        grpc_thread.detach();
    }

    return 0;
}
