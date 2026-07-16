#include "voiceqas/server/routes/register_routes.hpp"

#include "voiceqas/server/auth.hpp"
#include "voiceqas/server/routes/route_helpers.hpp"

#include "voiceqas/audio/ingress_codec.hpp"
#include "voiceqas/media/session.hpp"
#include "voiceqas/ops/webhook.hpp"

#include <cstring>

namespace voiceqas::routes {

void register_media_routes(httplib::Server& server, const RouteContext& ctx) {

    if (ctx.media_sessions) {
        server.Get("/v1/media/sessions", [ctx](const httplib::Request& req, httplib::Response& res) {
            if (!check_ops_read_auth(req)) {
                res.status = 401;
                res.set_content(R"({"error":"unauthorized"})", "application/json");
                return;
            }
            nlohmann::json sessions = nlohmann::json::array();
            for (const auto& cfg : ctx.media_sessions->list_sessions()) {
                sessions.push_back({
                    {"session_id", cfg.session_id},
                    {"format", static_cast<int>(cfg.format)},
                    {"sample_rate", cfg.sample_rate},
                    {"remote_host", cfg.remote_host},
                    {"remote_port", cfg.remote_port},
                    {"inbound_host", cfg.inbound_host},
                    {"inbound_port", cfg.inbound_port},
                    {"channel_id", cfg.channel_id},
                });
            }
            res.set_content(nlohmann::json{{"status", "ok"}, {"sessions", sessions}}.dump(), "application/json");
        });

        server.Post("/v1/media/sessions", [ctx](const httplib::Request& req, httplib::Response& res) {
            if (!check_ops_write_auth(req)) {
                res.status = 401;
                res.set_content(R"({"error":"unauthorized"})", "application/json");
                return;
            }
            try {
                const auto body = nlohmann::json::parse(req.body);
                media::MediaSessionConfig cfg;
                cfg.session_id = body.at("session_id").get<std::string>();
                const auto& media_cfg = ctx.media_config;
                cfg.format = body.contains("format")
                    ? format_from_json(body)
                    : audio::preferred_ingress_format(media_cfg);
                cfg.sample_rate = body.value("sample_rate", sample_rate_for_format(cfg.format));
                cfg.remote_host = body.value("remote_host", "");
                cfg.remote_port = static_cast<uint16_t>(body.value("remote_port", 0));
                cfg.inbound_host = body.value("inbound_host", "");
                cfg.inbound_port = static_cast<uint16_t>(body.value("inbound_port", 0));
                cfg.inbound_ssrc = static_cast<uint32_t>(body.value("inbound_ssrc", 0));
                cfg.outbound_ssrc = static_cast<uint32_t>(body.value("outbound_ssrc", 0));
                cfg.channel_id = body.value("channel_id", "default");

                std::string error;
                if (!ctx.media_sessions->open_session(cfg, error)) {
                    res.status = 400;
                    res.set_content(nlohmann::json{{"error", error}}.dump(), "application/json");
                    return;
                }
                const auto preferred = audio::preferred_ingress_format(media_cfg);
                if (cfg.format != preferred) {
                    ops::publish_codec_mismatch_alert(
                        cfg.session_id, preferred, cfg.format, "session_register");
                }
                res.set_content(nlohmann::json{
                    {"status", "ok"},
                    {"session_id", cfg.session_id},
                    {"format", static_cast<int>(cfg.format)},
                    {"sample_rate", cfg.sample_rate},
                    {"channel_id", cfg.channel_id},
                }.dump(), "application/json");
            } catch (const std::exception& e) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
            }
        });

        server.Delete(R"(/v1/media/sessions/(.+))", [ctx](const httplib::Request& req, httplib::Response& res) {
            if (!check_ops_write_auth(req)) {
                res.status = 401;
                res.set_content(R"({"error":"unauthorized"})", "application/json");
                return;
            }
            const auto session_id = req.matches[1].str();
            if (ctx.stt_sessions) {
                ctx.stt_sessions->flush_and_publish(session_id, {}, false);
            }
            if (ctx.sessions) {
                ctx.sessions->remove_session(session_id);
            }
            if (!ctx.media_sessions->close_session(session_id)) {
                res.status = 404;
                res.set_content(R"({"error":"session not found"})", "application/json");
                return;
            }
            res.set_content(R"({"status":"ok"})", "application/json");
        });

        server.Post(R"(/v1/media/sessions/([^/]+)/agent-audio)", [ctx](const httplib::Request& req, httplib::Response& res) {
            if (!check_ops_write_auth(req)) {
                res.status = 401;
                res.set_content(R"({"error":"unauthorized"})", "application/json");
                return;
            }
            try {
                const auto session_id = req.matches[1].str();
                const int input_rate = sample_rate_from_header(req, 16000);
                std::vector<uint8_t> payload;
                const auto& content_type = req.get_header_value("Content-Type");
                if (!req.body.empty() && content_type.find("json") == std::string::npos) {
                    payload.assign(req.body.begin(), req.body.end());
                } else {
                    const auto body = nlohmann::json::parse(req.body);
                    payload = decode_payload_json(body);
                }
                if (payload.size() % 2 != 0) {
                    res.status = 400;
                    res.set_content(R"({"error":"pcm payload must be even length"})", "application/json");
                    return;
                }
                std::vector<int16_t> pcm(payload.size() / 2);
                std::memcpy(pcm.data(), payload.data(), payload.size());
                const auto result = ctx.media_sessions->send_agent_pcm(session_id, pcm, input_rate);
                if (!result.ok) {
                    res.status = 400;
                    res.set_content(nlohmann::json{{"error", result.error}}.dump(), "application/json");
                    return;
                }
                res.set_content(nlohmann::json{
                    {"status", "ok"},
                    {"rtp_packets", result.rtp_packets.size()},
                    {"bytes_sent", result.bytes_sent},
                }.dump(), "application/json");
            } catch (const std::exception& e) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
            }
        });
    }
}

}  // namespace voiceqas::routes
