#include "voiceqas/server/routes/register_routes.hpp"

#include "voiceqas/config/channel_registry.hpp"
#include "voiceqas/server/auth.hpp"

namespace voiceqas::routes {

namespace {

config::ChannelProfile profile_from_json(const nlohmann::json& body) {
    config::ChannelProfile ch;
    ch.id = body.at("id").get<std::string>();
    ch.label = body.value("label", ch.id);
    ch.transport = config::transport_from_string(body.value("transport", "rest"));
    ch.enabled = body.value("enabled", true);
    ch.inherit_pipeline = body.value("inherit_pipeline", true);
    if (body.contains("media") && body["media"].is_object()) {
        const auto& m = body["media"];
        ch.media.preferred_ingress_codec = m.value("preferred_ingress_codec", "");
        ch.media.remote_host = m.value("remote_host", "");
        ch.media.remote_port = static_cast<uint16_t>(m.value("remote_port", 0));
    }
    if (body.contains("pipeline_override")) {
        ch.pipeline_override = body["pipeline_override"];
    }
    if (body.contains("stt_override") && body["stt_override"].is_object()) {
        const auto& s = body["stt_override"];
        if (s.contains("require_stt_ready")) {
            ch.stt_override.require_stt_ready = s["require_stt_ready"].get<bool>();
        }
        if (s.contains("diarization_focus_primary")) {
            ch.stt_override.diarization_focus_primary = s["diarization_focus_primary"].get<bool>();
        }
    }
    return ch;
}

}  // namespace

void register_config_routes(httplib::Server& server, const RouteContext& ctx) {
    if (!ctx.channel_registry) {
        return;
    }

    server.Get("/v1/config/pipeline-default", [ctx](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_read_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        res.set_content(ctx.channel_registry->pipeline_default_json().dump(), "application/json");
    });

    server.Put("/v1/config/pipeline-default", [ctx](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_write_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        try {
            const auto body = nlohmann::json::parse(req.body);
            std::string error;
            if (!ctx.channel_registry->update_pipeline_default(body, error)) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", error}}.dump(), "application/json");
                return;
            }
            res.set_content(
                nlohmann::json{{"status", "ok"}, {"pipeline_default", ctx.channel_registry->pipeline_default_json()}}
                    .dump(),
                "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Get("/v1/config/channels", [ctx](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_read_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        res.set_content(ctx.channel_registry->list_json().dump(), "application/json");
    });

    server.Get(R"(/v1/config/channels/([^/]+))", [ctx](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_read_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        const auto id = req.matches[1].str();
        const auto j = ctx.channel_registry->get_json(id);
        if (j.contains("error")) {
            res.status = 404;
        }
        res.set_content(j.dump(), "application/json");
    });

    server.Post("/v1/config/channels", [ctx](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_write_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        try {
            const auto body = nlohmann::json::parse(req.body);
            auto ch = profile_from_json(body);
            std::string error;
            if (!ctx.channel_registry->upsert_channel(ch, error)) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", error}}.dump(), "application/json");
                return;
            }
            res.set_content(ctx.channel_registry->get_json(ch.id).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Put(R"(/v1/config/channels/([^/]+))", [ctx](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_write_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        try {
            const auto id = req.matches[1].str();
            const auto body = nlohmann::json::parse(req.body);
            auto ch = profile_from_json(body);
            ch.id = id;
            std::string error;
            if (!ctx.channel_registry->upsert_channel(ch, error)) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", error}}.dump(), "application/json");
                return;
            }
            res.set_content(ctx.channel_registry->get_json(id).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Delete(R"(/v1/config/channels/([^/]+))", [ctx](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_write_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        const auto id = req.matches[1].str();
        std::string error;
        if (!ctx.channel_registry->delete_channel(id, error)) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", error}}.dump(), "application/json");
            return;
        }
        res.set_content(R"({"status":"ok"})", "application/json");
    });
}

}  // namespace voiceqas::routes
