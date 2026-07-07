#include "voiceqas/server/media_relay.hpp"

#include <boost/asio.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "voiceqas/audio/decoder.hpp"
#include "voiceqas/audio/ingress_codec.hpp"
#include "voiceqas/metrics.hpp"
#include "voiceqas/ops/webhook.hpp"
#include "voiceqas/ports/pipeline_telemetry.hpp"
#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/stt/client.hpp"
#include "voiceqas/stt/session_manager.hpp"

namespace voiceqas {

namespace {

std::pair<std::string, uint16_t> parse_host_port(const std::string& addr) {
    const auto colon = addr.rfind(':');
    if (colon == std::string::npos) {
        return {addr, 10000};
    }
    return {addr.substr(0, colon), static_cast<uint16_t>(std::stoi(addr.substr(colon + 1)))};
}

std::string endpoint_key(const std::string& host, uint16_t port) {
    return host + ":" + std::to_string(port);
}

std::string endpoint_key(const boost::asio::ip::udp::endpoint& ep) {
    return endpoint_key(ep.address().to_string(), ep.port());
}

}  // namespace

struct MediaRelayServer::Impl {
    boost::asio::io_context io;
    boost::asio::steady_timer stt_timer{io};
    std::unique_ptr<boost::asio::ip::udp::socket> socket;
    std::array<uint8_t, 2048> buffer{};
    boost::asio::ip::udp::endpoint remote_endpoint;
    std::mutex route_mutex;
    std::unordered_map<std::string, std::string> endpoint_to_session;
    std::unordered_set<std::string> pinned_routes;
    static constexpr int kSttFlushIntervalSec = 15;
};

MediaRelayServer::MediaRelayServer(
    std::string bind_addr,
    audio::MediaRelayConfig relay_config,
    std::shared_ptr<media::MediaSessionManager> media_sessions,
    std::shared_ptr<VqaSessionManager> vqa_sessions,
    std::shared_ptr<stt::SttSessionManager> stt_sessions,
    std::shared_ptr<ports::IPipelineTelemetry> telemetry)
    : impl_(std::make_unique<Impl>()),
      bind_addr_(std::move(bind_addr)),
      relay_config_(std::move(relay_config)),
      media_sessions_(std::move(media_sessions)),
      vqa_sessions_(std::move(vqa_sessions)),
      stt_sessions_(std::move(stt_sessions)),
      telemetry_(std::move(telemetry)) {}

MediaRelayServer::~MediaRelayServer() {
    stop();
}

void MediaRelayServer::bind_endpoint(
    const std::string& host,
    uint16_t port,
    const std::string& session_id) {
    if (!impl_) {
        return;
    }
    const auto key = endpoint_key(host, port);
    std::lock_guard lock(impl_->route_mutex);
    impl_->endpoint_to_session[key] = session_id;
    impl_->pinned_routes.insert(key);
}

void MediaRelayServer::unbind_endpoint(const std::string& host, uint16_t port) {
    if (!impl_) {
        return;
    }
    const auto key = endpoint_key(host, port);
    std::lock_guard lock(impl_->route_mutex);
    impl_->pinned_routes.erase(key);
    impl_->endpoint_to_session.erase(key);
}

void MediaRelayServer::run() {
    if (running_.exchange(true)) {
        return;
    }

    thread_ = std::thread([this]() {
        const auto [host, port] = parse_host_port(bind_addr_);
        boost::asio::ip::udp::endpoint listen_endpoint(
            boost::asio::ip::make_address(host), port);
        impl_->socket = std::make_unique<boost::asio::ip::udp::socket>(impl_->io, listen_endpoint);

        media_sessions_->set_udp_sender(
            [this](const std::string& remote_host, uint16_t remote_port, std::span<const uint8_t> packet) {
                if (!impl_->socket) {
                    return false;
                }
                boost::system::error_code ec;
                boost::asio::ip::udp::endpoint dest(
                    boost::asio::ip::make_address(remote_host), remote_port);
                impl_->socket->send_to(boost::asio::buffer(packet.data(), packet.size()), dest, 0, ec);
                return !ec;
            });

        std::function<void()> schedule_stt_flush;
        schedule_stt_flush = [this, &schedule_stt_flush]() {
            impl_->stt_timer.expires_after(std::chrono::seconds(Impl::kSttFlushIntervalSec));
            impl_->stt_timer.async_wait([this, &schedule_stt_flush](const boost::system::error_code& ec) {
                if (ec || !running_) {
                    return;
                }
                if (stt_sessions_) {
                    stt_sessions_->emit_partials_for_all_sessions();
                }
                schedule_stt_flush();
            });
        };
        schedule_stt_flush();

        std::function<void()> do_receive;
        do_receive = [this, &do_receive]() {
            impl_->socket->async_receive_from(
                boost::asio::buffer(impl_->buffer),
                impl_->remote_endpoint,
                [this, &do_receive](const boost::system::error_code& ec, std::size_t bytes) {
                    if (ec || !running_) {
                        return;
                    }

                    const auto key = endpoint_key(impl_->remote_endpoint);
                    std::string session_id;
                    bool pinned = false;
                    {
                        std::lock_guard lock(impl_->route_mutex);
                        pinned = impl_->pinned_routes.contains(key);
                        const auto it = impl_->endpoint_to_session.find(key);
                        if (it != impl_->endpoint_to_session.end()) {
                            session_id = it->second;
                        }
                    }

                    if (session_id.empty()) {
                        session_id = key;
                    }

                    std::optional<AudioFormat> registered_format;
                    if (auto session_cfg = media_sessions_->get_session_config(session_id)) {
                        registered_format = session_cfg->format;
                    }

                    std::optional<AudioFormat> detected_format;
                    const auto header = rtp::parse_header(
                        std::span<const uint8_t>(impl_->buffer.data(), bytes));
                    if (header) {
                        detected_format = audio::format_from_rtp_payload_type(header->payload_type);
                    }

                    const auto resolved = audio::resolve_ingress_codec(
                        relay_config_, registered_format, detected_format);
                    const AudioFormat format = resolved.format;
                    telemetry_->set_session_codec(session_id, audio_format_to_string(format));

                    if (resolved.suboptimal) {
                        const auto preferred = audio::preferred_ingress_format(relay_config_);
                        const auto actual = resolved.detected.value_or(resolved.format);
                        const auto source = resolved.registered
                            ? (resolved.detected ? "registered_vs_rtp" : "registered_suboptimal")
                            : (resolved.autodetected ? "autodetect" : "fallback");
                        ops::publish_codec_mismatch_alert(session_id, preferred, actual, source);
                    }

                    static thread_local int64_t ts = 0;
                    const auto payload = std::span<const uint8_t>(impl_->buffer.data(), bytes);

                    telemetry_->record_rtp_ingress(session_id, bytes, 0.0, 0.0);

                    const auto decoded = ingress_processor_.decode(session_id, format, payload);
                    if (decoded && vqa_sessions_) {
                        telemetry_->record_vqa_path(
                            session_id,
                            bytes,
                            decoded->pcm.size() * sizeof(int16_t),
                            0.0,
                            0.0,
                            decoded->stats.jitter_ms,
                            decoded->stats.packet_loss_pct);
                        if (auto report = vqa_sessions_->push_pcm(
                                session_id, decoded->pcm, ts, decoded->sample_rate)) {
                            telemetry_->record_rtp_ingress(
                                session_id, 0, report->jitter_ms, report->packet_loss_pct, false);
                            if (stt_sessions_) {
                                stt_sessions_->update_stt_ready(session_id, report->stt_ready);
                            }
                        }
                    }
                    if (decoded && stt_sessions_) {
                        stt_sessions_->ensure_session_bound(session_id);
                        stt_sessions_->append_pcm(session_id, decoded->pcm, decoded->sample_rate);
                        if (auto partial = stt_sessions_->emit_partial_if_due(session_id)) {
                            (void)partial;
                        }
                    }

                    if (!pinned) {
                        std::lock_guard lock(impl_->route_mutex);
                        impl_->endpoint_to_session[key] = session_id;
                    }

                    ts += 20;
                    do_receive();
                });
        };

        do_receive();
        impl_->io.run();
    });

    std::cout << "media relay UDP listening on " << bind_addr_ << '\n';
}

void MediaRelayServer::stop() {
    running_ = false;
    if (impl_->socket) {
        boost::system::error_code ec;
        impl_->socket->close(ec);
    }
    impl_->io.stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace voiceqas
