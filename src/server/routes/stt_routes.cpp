#include "voiceqas/server/routes/register_routes.hpp"

#include "voiceqas/server/auth.hpp"
#include "voiceqas/server/routes/route_helpers.hpp"

#include "voiceqas/stt/json_util.hpp"
#include "voiceqas/stt/model_util.hpp"
#include "voiceqas/stt/vad_model.hpp"
#include "voiceqas/wav.hpp"

#include <cstring>

namespace voiceqas::routes {

void register_stt_routes(httplib::Server& server, const RouteContext& ctx) {

    server->Get("/v1/stt/ready", [ctx](const httplib::Request&, httplib::Response& res) {
        nlohmann::json models = nlohmann::json::array();
        std::string status = "unavailable";
        std::string language = "pt";
        stt::SttReadyStatus ready_status;
        if (ctx.stt_sessions) {
            ready_status = ctx.stt_sessions->engine().ready_status();
            status = ready_status.ready() ? "ready" : "unavailable";
            language = ctx.stt_sessions->config().language;
            if (ready_status.parakeet_ready) {
                models.push_back({
                    {"id", "parakeet"},
                    {"name", ready_status.parakeet_model},
                    {"ready", true},
                });
            }
            if (ready_status.whisper_ready) {
                models.push_back({
                    {"id", "whisper"},
                    {"name", ready_status.whisper_model},
                    {"ready", true},
                });
            }
            models.push_back({
                {"id", "auto"},
                {"name", "parakeet with whisper fallback"},
                {"ready", ready_status.ready()},
            });
            if (ready_status.vad_ready || !ready_status.vad_model_id.empty()) {
                models.push_back({
                    {"id", "vad"},
                    {"name", ready_status.vad_model.empty() ? ready_status.vad_model_id : ready_status.vad_model},
                    {"ready", ready_status.vad_ready},
                    {"active_id", ready_status.vad_model_id},
                });
            }
            for (const auto& vad : stt::list_vad_models(
                     ctx.stt_sessions->config().models_dir, ready_status.vad_model_id)) {
                models.push_back({
                    {"id", "vad:" + vad.id},
                    {"name", vad.name},
                    {"ready", vad.present},
                    {"active", vad.active},
                    {"path", vad.path},
                });
            }
        }
        res.set_content(nlohmann::json{
            {"status", status},
            {"service", "voiceqas-stt"},
            {"language", language},
            {"default_model", ctx.stt_sessions ? ctx.stt_sessions->config().default_model : "auto"},
            {"provider", ready_status.provider},
            {"providers_available", ready_status.providers_available},
            {"loaded_providers", ready_status.loaded_providers},
            {"cuda_compiled", ready_status.cuda_compiled},
            {"models", models},
        }.dump(), "application/json");
    });

    server->Post("/v1/stt/vad/model", [ctx](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_write_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        if (!ctx.stt_sessions) {
            res.status = 503;
            res.set_content(R"({"error":"STT not configured"})", "application/json");
            return;
        }
        try {
            const auto body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            const auto model = body.value("model", std::string{"auto"});
            if (!ctx.stt_sessions->reload_vad_model(model)) {
                res.status = 400;
                res.set_content(
                    nlohmann::json{{"error", "failed to load VAD model"}, {"model", model}}.dump(),
                    "application/json");
                return;
            }
            const auto ready = ctx.stt_sessions->engine().ready_status();
            res.set_content(
                nlohmann::json{
                    {"status", "ok"},
                    {"model", model},
                    {"active_id", ready.vad_model_id},
                    {"active_name", ready.vad_model},
                    {"vad_ready", ready.vad_ready},
                }
                    .dump(),
                "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server->Post("/v1/stt/transcribe", [ctx](const httplib::Request& req, httplib::Response& res) {
        try {
            if (!ctx.stt_sessions) {
                res.status = 503;
                res.set_content(R"({"error":"STT not configured"})", "application/json");
                return;
            }

            std::vector<uint8_t> payload(req.body.begin(), req.body.end());
            AudioFormat format = format_from_header(req);
            int sample_rate = sample_rate_from_header(req, sample_rate_for_format(format));

            if (req.get_header_value("Content-Type").find("wav") != std::string::npos) {
                auto wav = parse_wav(payload);
                if (!wav) {
                    res.status = 400;
                    res.set_content(R"({"error":"invalid wav"})", "application/json");
                    return;
                }
                format = wav->sample_rate >= 16000 ? AudioFormat::PcmS16Le16k : AudioFormat::PcmS16Le8k;
                sample_rate = wav->sample_rate;
                payload.resize(wav->samples.size() * 2);
                std::memcpy(payload.data(), wav->samples.data(), payload.size());
            }

            const auto options = stt::transcribe_options_from_request(req, ctx.stt_sessions->config());

            const auto result = ctx.stt_sessions->transcribe_batch(
                format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                sample_rate,
                options);

            if (!result.ok) {
                res.status = 502;
                res.set_content(nlohmann::json{{"error", result.error}}.dump(), "application/json");
                return;
            }
            res.set_content(transcript_to_json(result).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server->Post("/v1/stt/transcribe/segment", [ctx](const httplib::Request& req, httplib::Response& res) {
        try {
            if (!ctx.stt_sessions) {
                res.status = 503;
                res.set_content(R"({"error":"STT not configured"})", "application/json");
                return;
            }
            const auto body = nlohmann::json::parse(req.body);
            const auto format = audio_format_from_string(body.at("format").get<std::string>());
            const int sample_rate = body.value("sample_rate", sample_rate_for_format(format));
            auto payload = decode_payload_json(body);

            const auto options = stt::transcribe_options_from_request(req, ctx.stt_sessions->config(), &body);

            const auto result = ctx.stt_sessions->transcribe_batch(
                format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                sample_rate,
                options);

            if (!result.ok) {
                res.status = 502;
                res.set_content(nlohmann::json{{"error", result.error}}.dump(), "application/json");
                return;
            }
            res.set_content(transcript_to_json(result).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server->Post("/v1/playground/grpc/stt-transcribe", [ctx](const httplib::Request& req, httplib::Response& res) {
        try {
            if (!ctx.stt_sessions) {
                res.status = 503;
                res.set_content(R"({"error":"STT not configured"})", "application/json");
                return;
            }
            const auto body = nlohmann::json::parse(req.body);
            const auto format = format_from_json(body);
            const int sample_rate = body.value("sample_rate", sample_rate_for_format(format));
            auto payload = decode_payload_json(body);

            const auto options = stt::transcribe_options_from_request(req, ctx.stt_sessions->config(), &body);

            const auto result = ctx.stt_sessions->transcribe_batch(
                format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                sample_rate,
                options);

            if (!result.ok) {
                res.status = 502;
                res.set_content(nlohmann::json{{"error", result.error}}.dump(), "application/json");
                return;
            }
            res.set_content(transcript_to_json(result).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

}

}  // namespace voiceqas::routes
