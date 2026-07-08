#include "voiceqas/server/routes/register_routes.hpp"

#include "voiceqas/server/auth.hpp"
#include "voiceqas/server/routes/route_helpers.hpp"

#include "voiceqas/json_util.hpp"
#include "voiceqas/ops/pipeline_tracker.hpp"
#include "voiceqas/ops/transport_sankey.hpp"
#include "voiceqas/wav.hpp"

#include <cstring>

namespace voiceqas::routes {

void register_vqa_routes(httplib::Server& server, const RouteContext& ctx) {
    server.Get("/v1/playground/grpc/ready", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(grpc_ready_json().dump(), "application/json");
    });

    server.Post("/v1/playground/grpc/ready", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(grpc_ready_json().dump(), "application/json");
    });

    server.Post("/v1/playground/grpc/analyze-batch", [ctx](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            const auto format = format_from_json(body);
            const int sample_rate = body.value("sample_rate", sample_rate_for_format(format));
            auto payload = decode_payload_json(body);
            if (payload.empty() && body.contains("payload")) {
                const auto& raw = body["payload"];
                if (raw.is_string()) {
                    const auto s = raw.get<std::string>();
                    payload.assign(s.begin(), s.end());
                }
            }
            std::optional<std::string> session_id;
            if (body.contains("session_id")) {
                session_id = body.at("session_id").get<std::string>();
            }
            const auto result = ctx.sessions->analyze_batch(
                format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                sample_rate,
                session_id);
            res.set_content(batch_result_to_json(result).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/v1/playground/grpc/analyze-stream", [ctx](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            const std::string session_id = body.value("session_id", "playground");
            const auto format = format_from_json(body);
            const auto& frames = body.at("frames");

            std::ostringstream sse;
            for (const auto& frame : frames) {
                auto payload = decode_payload_json(frame);
                const int64_t ts = frame.value("timestamp_ms", 0);
                if (auto report = ctx.sessions->push_frame(
                        session_id,
                        format,
                        std::span<const uint8_t>(payload.data(), payload.size()),
                        ts)) {
                    auto json = window_metrics_to_json(*report);
                    json["session_id"] = session_id;
                    sse << "data: " << json.dump() << "\n\n";
                }
            }
            ctx.sessions->remove_session(session_id);
            res.set_header("Cache-Control", "no-cache");
            res.set_content(sse.str(), "text/event-stream");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/v1/analyze/batch", [ctx](const httplib::Request& req, httplib::Response& res) {
        try {
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

            const auto session_id = telemetry_session_id_from_request(req).value_or("rest-analyze");
            const auto result = ctx.sessions->analyze_batch(
                format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                sample_rate,
                std::optional<std::string>{session_id});

            ops::PipelineTracker::instance().record_transport_ingress(
                session_id,
                ops::transport_node::kRest,
                payload.size(),
                0.0,
                0.0);

            res.set_content(batch_result_to_json(result).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/v1/tools/pack-rtp", [](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            const auto format = audio_format_from_string(body.at("format").get<std::string>());
            if (format != AudioFormat::RtpG722 && format != AudioFormat::RtpG729
                && format != AudioFormat::RtpPcmu && format != AudioFormat::RtpPcma) {
                res.status = 400;
                res.set_content(R"({"error":"pack-rtp supports rtp_pcmu, rtp_pcma, rtp_g722, rtp_g729"})", "application/json");
                return;
            }
            const int frame_ms = body.value("frame_ms", 20);
            auto bytes = decode_payload_json(body);
            if (bytes.empty() && body.contains("pcm_bytes")) {
                for (const auto& v : body["pcm_bytes"]) {
                    bytes.push_back(static_cast<uint8_t>(v.get<int>()));
                }
            }
            const auto pcm = pcm_from_bytes(bytes);
            res.set_content(pack_rtp_frames(format, pcm, frame_ms).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/v1/tools/decode-rtp", [](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            const auto format = audio_format_from_string(body.at("format").get<std::string>());
            res.set_content(decode_rtp_frames(format, body.at("frames")).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    server.Post("/v1/analyze/segment", [ctx](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            const auto format = audio_format_from_string(body.at("format").get<std::string>());
            const int sample_rate = body.value("sample_rate", sample_rate_for_format(format));
            auto payload = decode_payload_json(body);

            std::optional<std::string> session_id;
            if (body.contains("session_id")) {
                session_id = body.at("session_id").get<std::string>();
            }
            const auto result = ctx.sessions->analyze_batch(
                format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                sample_rate,
                session_id);
            res.set_content(batch_result_to_json(result).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
}

}  // namespace voiceqas::routes
