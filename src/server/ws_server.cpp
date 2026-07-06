#include "voiceqas/server/ws_server.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "voiceqas/json_util.hpp"
#include "voiceqas/stt/json_util.hpp"
#include "voiceqas/stt/model_util.hpp"

namespace voiceqas {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace {

enum class WsMode {
    Quality,
    Stt,
};

struct WsSessionConfig {
    std::string session_id = "default";
    AudioFormat format = AudioFormat::PcmS16Le8k;
    int sample_rate = 8000;
    int64_t timestamp_ms = 0;
    bool ready = false;
};

std::string target_path(const http::request<http::string_body>& req) {
    const auto target = std::string(req.target());
    const auto query = target.find('?');
    return query == std::string::npos ? target : target.substr(0, query);
}

class QualityWsSession : public std::enable_shared_from_this<QualityWsSession> {
public:
    QualityWsSession(tcp::socket socket, std::shared_ptr<SessionManager> sessions)
        : ws_(std::move(socket)), sessions_(std::move(sessions)) {}

    void accept(http::request<http::string_body> req) {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(http::field::server, "voiceqas");
            }));
        ws_.async_accept(req, beast::bind_front_handler(&QualityWsSession::on_accept, shared_from_this()));
    }

private:
    void on_accept(beast::error_code ec) {
        if (ec) {
            return;
        }
        do_read();
    }

    void do_read() {
        ws_.async_read(buffer_, beast::bind_front_handler(&QualityWsSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t) {
        if (ec == websocket::error::closed) {
            if (config_.ready) {
                sessions_->remove_session(config_.session_id);
            }
            return;
        }
        if (ec) {
            return;
        }

        const auto data = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());

        if (!config_.ready) {
            try {
                const auto json = nlohmann::json::parse(data);
                config_.session_id = json.value("session_id", "default");
                config_.format = audio_format_from_string(json.value("format", "pcm_s16le_8k"));
                config_.timestamp_ms = json.value("timestamp_ms", 0);
                config_.ready = true;
                ws_.text(true);
                ws_.async_write(net::buffer(R"({"status":"ok","mode":"quality"})"),
                                beast::bind_front_handler(&QualityWsSession::on_write, shared_from_this()));
                return;
            } catch (const std::exception& e) {
                ws_.text(true);
                const auto msg = std::string(R"({"error":")") + e.what() + R"("})";
                ws_.async_write(net::buffer(msg),
                                beast::bind_front_handler(&QualityWsSession::on_write, shared_from_this()));
                return;
            }
        }

        std::vector<uint8_t> payload(data.begin(), data.end());
        if (auto report = sessions_->push_frame(
                config_.session_id,
                config_.format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                config_.timestamp_ms)) {
            config_.timestamp_ms += 20;
            auto json = window_metrics_to_json(*report);
            json["session_id"] = config_.session_id;
            const auto out = json.dump();
            ws_.text(true);
            ws_.async_write(net::buffer(out),
                            beast::bind_front_handler(&QualityWsSession::on_write, shared_from_this()));
            return;
        }

        do_read();
    }

    void on_write(beast::error_code ec, std::size_t) {
        if (ec) {
            return;
        }
        do_read();
    }

    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::shared_ptr<SessionManager> sessions_;
    WsSessionConfig config_;
};

class SttWsSession : public std::enable_shared_from_this<SttWsSession> {
public:
    SttWsSession(tcp::socket socket, std::shared_ptr<stt::SttSessionManager> stt_sessions)
        : ws_(std::move(socket)), stt_sessions_(std::move(stt_sessions)) {}

    void accept(http::request<http::string_body> req) {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(http::field::server, "voiceqas");
            }));
        ws_.async_accept(req, beast::bind_front_handler(&SttWsSession::on_accept, shared_from_this()));
    }

private:
    void on_accept(beast::error_code ec) {
        if (ec) {
            return;
        }
        do_read();
    }

    void do_read() {
        ws_.async_read(buffer_, beast::bind_front_handler(&SttWsSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t) {
        if (ec == websocket::error::closed) {
            if (config_.ready) {
                stt_sessions_->remove_session(config_.session_id);
            }
            return;
        }
        if (ec) {
            return;
        }

        const auto data = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());

        if (!config_.ready) {
            try {
                const auto json = nlohmann::json::parse(data);
                config_.session_id = json.value("session_id", "default");
                config_.format = audio_format_from_string(json.value("format", "pcm_s16le_8k"));
                config_.sample_rate = json.value("sample_rate", sample_rate_for_format(config_.format));
                config_.ready = true;

                stt::TranscribeOptions options = stt::transcribe_options_from_json(
                    json, stt_sessions_->config());
                stt_sessions_->bind_session_options(config_.session_id, options);

                const auto ready = stt_sessions_->engine().ready_status();
                const auto ack = nlohmann::json{
                    {"status", "ok"},
                    {"mode", "stt"},
                    {"default_model", stt_sessions_->config().default_model},
                    {"models", nlohmann::json::array({"parakeet", "whisper", "auto"})},
                    {"parakeet_ready", ready.parakeet_ready},
                    {"whisper_ready", ready.whisper_ready},
                }.dump();
                ws_.text(true);
                ws_.async_write(net::buffer(ack),
                                beast::bind_front_handler(&SttWsSession::on_write, shared_from_this()));
                return;
            } catch (const std::exception& e) {
                ws_.text(true);
                const auto msg = std::string(R"({"error":")") + e.what() + R"("})";
                ws_.async_write(net::buffer(msg),
                                beast::bind_front_handler(&SttWsSession::on_write, shared_from_this()));
                return;
            }
        }

        if (ws_.got_text()) {
            try {
                const auto json = nlohmann::json::parse(data);
                if (json.value("type", "") == "flush") {
                    stt::TranscribeOptions options;
                    if (json.contains("model")) {
                        options.model = stt::parse_model_choice(
                            json.at("model").get<std::string>(),
                            stt::parse_model_choice(stt_sessions_->config().default_model));
                    }
                    if (json.contains("language")) {
                        options.language = json.at("language").get<std::string>();
                    }
                    const auto result = stt_sessions_->flush(config_.session_id, options);
                    auto out = transcript_to_json(result);
                    out["session_id"] = config_.session_id;
                    out["type"] = result.ok ? "final" : "error";
                    const auto payload = out.dump();
                    ws_.text(true);
                    ws_.async_write(net::buffer(payload),
                                    beast::bind_front_handler(&SttWsSession::on_write, shared_from_this()));
                    return;
                }
            } catch (...) {
            }
            do_read();
            return;
        }

        std::vector<uint8_t> payload(data.begin(), data.end());
        stt_sessions_->append_chunk(
            config_.session_id,
            config_.format,
            std::span<const uint8_t>(payload.data(), payload.size()),
            config_.sample_rate);
        do_read();
    }

    void on_write(beast::error_code ec, std::size_t) {
        if (ec) {
            return;
        }
        do_read();
    }

    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
    WsSessionConfig config_;
};

class HttpUpgradeSession : public std::enable_shared_from_this<HttpUpgradeSession> {
public:
    HttpUpgradeSession(
        tcp::socket socket,
        std::shared_ptr<SessionManager> sessions,
        std::shared_ptr<stt::SttSessionManager> stt_sessions)
        : socket_(std::move(socket)),
          sessions_(std::move(sessions)),
          stt_sessions_(std::move(stt_sessions)) {}

    void run() {
        http::async_read(
            socket_,
            buffer_,
            req_,
            beast::bind_front_handler(&HttpUpgradeSession::on_read, shared_from_this()));
    }

private:
    void on_read(beast::error_code ec, std::size_t) {
        if (ec) {
            return;
        }

        const auto path = target_path(req_);
        WsMode mode = WsMode::Quality;
        if (path == "/v1/stt/stream") {
            mode = WsMode::Stt;
        } else if (path != "/v1/stream" && path != "/") {
            http::response<http::string_body> res{http::status::not_found, req_.version()};
            res.set(http::field::server, "voiceqas");
            res.set(http::field::content_type, "text/plain");
            res.body() = "unknown websocket path";
            res.prepare_payload();
            http::async_write(socket_, res, [self = shared_from_this()](beast::error_code, std::size_t) {
                beast::error_code ignored;
                self->socket_.shutdown(tcp::socket::shutdown_send, ignored);
            });
            return;
        }

        if (mode == WsMode::Stt) {
            std::make_shared<SttWsSession>(std::move(socket_), stt_sessions_)->accept(std::move(req_));
        } else {
            std::make_shared<QualityWsSession>(std::move(socket_), sessions_)->accept(std::move(req_));
        }
    }

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<SessionManager> sessions_;
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
};

class WsListener : public std::enable_shared_from_this<WsListener> {
public:
    WsListener(
        net::io_context& ioc,
        tcp::endpoint endpoint,
        std::shared_ptr<SessionManager> sessions,
        std::shared_ptr<stt::SttSessionManager> stt_sessions)
        : ioc_(ioc),
          acceptor_(ioc),
          sessions_(std::move(sessions)),
          stt_sessions_(std::move(stt_sessions)) {
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
    }

    void run() { do_accept(); }

private:
    void do_accept() {
        acceptor_.async_accept(net::make_strand(ioc_),
                               beast::bind_front_handler(&WsListener::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::make_shared<HttpUpgradeSession>(std::move(socket), sessions_, stt_sessions_)->run();
        }
        do_accept();
    }

    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<SessionManager> sessions_;
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
};

}  // namespace

WebSocketServer::WebSocketServer(
    std::string bind_addr,
    std::shared_ptr<SessionManager> sessions,
    std::shared_ptr<stt::SttSessionManager> stt_sessions)
    : bind_addr_(std::move(bind_addr)),
      sessions_(std::move(sessions)),
      stt_sessions_(std::move(stt_sessions)) {}

void WebSocketServer::run() {
    const auto colon = bind_addr_.rfind(':');
    const std::string host = bind_addr_.substr(0, colon);
    const auto port = static_cast<unsigned short>(std::stoi(bind_addr_.substr(colon + 1)));

    running_ = true;
    thread_ = std::thread([this, host, port]() {
        net::io_context ioc{1};
        auto listener = std::make_shared<WsListener>(
            ioc,
            tcp::endpoint{net::ip::make_address(host), port},
            sessions_,
            stt_sessions_);
        listener->run();
        std::cout << "WebSocket listening on " << bind_addr_
                  << " (/v1/stream quality, /v1/stt/stream transcription)\n";
        ioc.run();
        running_ = false;
    });
}

void WebSocketServer::stop() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace voiceqas
