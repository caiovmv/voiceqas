#include "voiceqas/server/auth.hpp"
#include "voiceqas/server/rest_server.hpp"
#include "voiceqas/server/routes/register_routes.hpp"

#include <httplib.h>

#include <iostream>
#include <memory>
#include <thread>

namespace voiceqas {

RestServer::RestServer(
    std::string bind_addr,
    std::string web_root,
    std::string openapi_path,
    audio::MediaRelayConfig media_config,
    std::shared_ptr<VqaSessionManager> sessions,
    std::shared_ptr<stt::SttSessionManager> stt_sessions,
    std::shared_ptr<media::MediaSessionManager> media_sessions)
    : bind_addr_(std::move(bind_addr)),
      web_root_(std::move(web_root)),
      openapi_path_(std::move(openapi_path)),
      media_config_(std::move(media_config)),
      sessions_(std::move(sessions)),
      stt_sessions_(std::move(stt_sessions)),
      media_sessions_(std::move(media_sessions)) {}

void RestServer::run() {
    const auto colon = bind_addr_.rfind(':');
    const std::string host = bind_addr_.substr(0, colon);
    const int port = std::stoi(bind_addr_.substr(colon + 1));

    auto server = std::make_shared<httplib::Server>();
    server->set_read_timeout(180, 0);
    server->set_write_timeout(180, 0);

    server->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header(
            "Access-Control-Allow-Headers",
            "Content-Type, X-Sample-Rate, X-Audio-Format, X-Language, X-STT-Model, X-STT-Provider, X-Session-Id, X-Ops-Token");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    RouteContext ctx{
        .web_root = web_root_,
        .openapi_path = openapi_path_,
        .media_config = media_config_,
        .sessions = sessions_,
        .stt_sessions = stt_sessions_,
        .media_sessions = media_sessions_,
    };
    routes::register_all_routes(*server, ctx);

    if (!web_root_.empty()) {
        if (!server->set_mount_point("/ui", web_root_)) {
            std::cerr << "warning: web root not found: " << web_root_ << '\n';
        }
    }

    running_ = true;
    thread_ = std::thread([this, host, port, server]() {
        server->listen(host.c_str(), port);
        running_ = false;
    });
}

void RestServer::stop() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace voiceqas
