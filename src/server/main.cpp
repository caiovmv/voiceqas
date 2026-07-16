#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include "voiceqas/analyzer.hpp"
#include "voiceqas/config.hpp"
#include "voiceqas/config/channel_registry.hpp"
#include "voiceqas/media/session.hpp"
#include "voiceqas/ops/adapters/ops_ports.hpp"
#include "voiceqas/ops/external_ai_config.hpp"
#include "voiceqas/server/grpc_server.hpp"
#include "voiceqas/server/grpc_playground_client.hpp"
#include "voiceqas/server/media_relay.hpp"
#include "voiceqas/server/rest_server.hpp"
#include "voiceqas/server/ws_server.hpp"
#include "voiceqas/stt/client.hpp"
#include "voiceqas/stt/session_manager.hpp"
#include "voiceqas/tracing/tracing.hpp"

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) {
    g_running = false;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const auto app_cfg = voiceqas::load_app_config(argc, argv);
    voiceqas::tracing::init(app_cfg.tracing);
    voiceqas::ops::configure_external_ai(app_cfg.external_ai);
    auto metrics = voiceqas::ops::make_ops_metrics_publisher();
    auto telemetry = voiceqas::ops::make_ops_pipeline_telemetry();
    auto sessions = std::make_shared<voiceqas::VqaSessionManager>(
        app_cfg.analyzer, app_cfg.audio, metrics, telemetry);

    auto stt_engine = std::make_shared<voiceqas::stt::SttEngine>(app_cfg.stt, telemetry);
    auto stt_sessions = std::make_shared<voiceqas::stt::SttSessionManager>(
        stt_engine,
        app_cfg.stt,
        app_cfg.ops,
        metrics,
        telemetry,
        app_cfg.audio,
        app_cfg.stt.target_sample_rate);

    auto media_sessions = std::make_shared<voiceqas::media::MediaSessionManager>(
        app_cfg.audio, app_cfg.media, telemetry);

    auto channel_registry = std::make_shared<voiceqas::config::ChannelRegistry>(
        app_cfg.server.channels_config_path, app_cfg.audio, app_cfg.stt);

    voiceqas::RestServer rest(
        app_cfg.server.rest_addr,
        app_cfg.server.web_root,
        app_cfg.server.openapi_path,
        app_cfg.media,
        sessions,
        stt_sessions,
        media_sessions,
        voiceqas::server::local_grpc_target(app_cfg.server.grpc_addr),
        channel_registry);
    voiceqas::WebSocketServer ws(app_cfg.server.ws_addr, sessions, stt_sessions);

    std::unique_ptr<voiceqas::MediaRelayServer> media_relay;
    if (app_cfg.media.enabled) {
        media_relay = std::make_unique<voiceqas::MediaRelayServer>(
            app_cfg.server.media_rtp_addr,
            app_cfg.media,
            app_cfg.audio,
            media_sessions,
            sessions,
            stt_sessions,
            telemetry,
            channel_registry);
        media_sessions->set_route_callback(
            [&media_relay](const std::string& host, uint16_t port, const std::string& session_id, bool bind) {
                if (!media_relay) {
                    return;
                }
                if (bind) {
                    media_relay->bind_endpoint(host, port, session_id);
                } else {
                    media_relay->unbind_endpoint(host, port);
                }
            });
        media_relay->run();
    }

    std::thread grpc_thread([&app_cfg, sessions, stt_sessions, media_sessions]() {
        voiceqas::GrpcServer grpc(app_cfg.server.grpc_addr, sessions, stt_sessions, media_sessions);
        grpc.run();
    });
    // Allow gRPC listener to bind before REST playground clients connect.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    rest.run();
    ws.run();

    const auto ready = stt_engine->ready_status();
    std::cout << "voiceqas started\n"
              << "  REST: " << app_cfg.server.rest_addr << '\n'
              << "  gRPC: " << app_cfg.server.grpc_addr << '\n'
              << "  WS:   " << app_cfg.server.ws_addr << '\n'
              << "  Media RTP: " << app_cfg.server.media_rtp_addr
              << " (preferred ingress: "
              << app_cfg.media.preferred_ingress_codec << ")\n"
              << "  STT:  embedded sherpa-onnx (parakeet="
              << (ready.parakeet_ready ? "ready" : "missing") << ", whisper="
              << (ready.whisper_ready ? "ready" : "missing") << ")\n";

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (media_relay) {
        media_relay->stop();
    }
    rest.stop();
    ws.stop();
    if (grpc_thread.joinable()) {
        grpc_thread.join();
    }

    voiceqas::tracing::shutdown();

    return 0;
}
