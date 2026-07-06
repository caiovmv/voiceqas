#include "voiceqas/server/rest_server.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "voiceqas/json_util.hpp"
#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/rtp/g722_codec.hpp"
#include "voiceqas/rtp/g729_codec.hpp"
#include "voiceqas/stt/json_util.hpp"
#include "voiceqas/stt/model_util.hpp"
#include "voiceqas/wav.hpp"

namespace voiceqas {

namespace {

AudioFormat format_from_header(const httplib::Request& req) {
    if (req.has_header("X-Audio-Format")) {
        return audio_format_from_string(req.get_header_value("X-Audio-Format"));
    }
    if (req.get_header_value("Content-Type").find("wav") != std::string::npos) {
        return AudioFormat::PcmS16Le8k;
    }
    return AudioFormat::PcmS16Le8k;
}

int sample_rate_from_header(const httplib::Request& req, int fallback) {
    if (req.has_header("X-Sample-Rate")) {
        return std::stoi(req.get_header_value("X-Sample-Rate"));
    }
    return fallback;
}

AudioFormat format_from_json(const nlohmann::json& body) {
    if (body.contains("format")) {
        if (body["format"].is_string()) {
            return audio_format_from_string(body["format"].get<std::string>());
        }
        const int v = body["format"].get<int>();
        switch (v) {
            case 1: return AudioFormat::PcmS16Le8k;
            case 2: return AudioFormat::PcmS16Le16k;
            case 3: return AudioFormat::RtpPcmu;
            case 4: return AudioFormat::RtpPcma;
            case 5: return AudioFormat::RtpG722;
            case 6: return AudioFormat::RtpG729;
            default: return AudioFormat::PcmS16Le8k;
        }
    }
    return AudioFormat::PcmS16Le8k;
}

std::vector<uint8_t> decode_payload_json(const nlohmann::json& body) {
    std::vector<uint8_t> payload;
    if (body.contains("pcm_bytes")) {
        for (const auto& v : body["pcm_bytes"]) {
            payload.push_back(static_cast<uint8_t>(v.get<int>()));
        }
        return payload;
    }
    if (body.contains("payload_bytes")) {
        for (const auto& v : body["payload_bytes"]) {
            payload.push_back(static_cast<uint8_t>(v.get<int>()));
        }
        return payload;
    }
    if (body.contains("payload_base64")) {
        const auto& b64 = body["payload_base64"].get<std::string>();
        // minimal: treat as raw bytes for playground (not strict base64)
        payload.assign(b64.begin(), b64.end());
    }
    return payload;
}

std::string read_file_or_empty(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

nlohmann::json grpc_ready_json() {
    return {
        {"status", "ready"},
        {"service", "voiceqas"},
        {"note", "HTTP playground bridge for VoiceQualityService.Ready"},
    };
}

std::vector<int16_t> pcm_from_bytes(const std::vector<uint8_t>& bytes) {
    std::vector<int16_t> pcm(bytes.size() / 2);
    std::memcpy(pcm.data(), bytes.data(), bytes.size());
    return pcm;
}

std::vector<uint8_t> pcm_to_bytes(std::span<const int16_t> pcm) {
    std::vector<uint8_t> bytes(pcm.size() * 2);
    std::memcpy(bytes.data(), pcm.data(), bytes.size());
    return bytes;
}

std::vector<uint8_t> build_rtp_packet(uint16_t seq, uint32_t ts, uint8_t pt, std::span<const uint8_t> payload) {
    std::vector<uint8_t> packet(12 + payload.size());
    packet[0] = 0x80;
    packet[1] = pt & 0x7F;
    packet[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(seq & 0xFF);
    packet[4] = static_cast<uint8_t>((ts >> 24) & 0xFF);
    packet[5] = static_cast<uint8_t>((ts >> 16) & 0xFF);
    packet[6] = static_cast<uint8_t>((ts >> 8) & 0xFF);
    packet[7] = static_cast<uint8_t>(ts & 0xFF);
    packet[8] = 0x12;
    packet[9] = 0x34;
    packet[10] = 0x56;
    packet[11] = 0x78;
    std::memcpy(packet.data() + 12, payload.data(), payload.size());
    return packet;
}

nlohmann::json pack_rtp_frames(
    AudioFormat format,
    std::span<const int16_t> pcm,
    int frame_ms) {
    const int sample_rate = sample_rate_for_format(format);
    const int frame_samples = sample_rate * frame_ms / 1000;
    std::vector<nlohmann::json> frames;
    uint16_t seq = 1;
    uint32_t ts = 0;

    auto push_frame = [&](std::span<const uint8_t> payload, uint8_t pt, uint32_t ts_step) {
        const auto packet = build_rtp_packet(seq++, ts, pt, payload);
        ts += ts_step;
        nlohmann::json arr = nlohmann::json::array();
        for (uint8_t b : packet) {
            arr.push_back(b);
        }
        frames.push_back(std::move(arr));
    };

    if (format == AudioFormat::RtpG722) {
        const auto encoded = rtp::encode_g722(pcm);
        const int bytes_per_frame = frame_samples / 2;
        for (size_t i = 0; i < encoded.size(); i += static_cast<size_t>(bytes_per_frame)) {
            const size_t n = std::min(static_cast<size_t>(bytes_per_frame), encoded.size() - i);
            push_frame({encoded.data() + i, n}, 9, static_cast<uint32_t>(n));
        }
    } else if (format == AudioFormat::RtpG729) {
        const auto encoded = rtp::encode_g722(pcm);
        const int bytes_per_frame = frame_samples / 2;
        for (size_t i = 0; i < encoded.size(); i += static_cast<size_t>(bytes_per_frame)) {
            const size_t n = std::min(static_cast<size_t>(bytes_per_frame), encoded.size() - i);
            push_frame({encoded.data() + i, n}, 9, static_cast<uint32_t>(n));
        }
    } else if (format == AudioFormat::RtpG729) {
        const auto encoded = rtp::encode_g729(pcm);
        const int bytes_per_frame = frame_ms == 20 ? 20 : 10;
        const uint32_t ts_step = frame_ms == 20 ? 160 : 80;
        for (size_t i = 0; i < encoded.size(); i += static_cast<size_t>(bytes_per_frame)) {
            const size_t n = std::min(static_cast<size_t>(bytes_per_frame), encoded.size() - i);
            push_frame({encoded.data() + i, n}, 18, ts_step);
        }
    }

    return {
        {"frames", frames},
        {"sample_rate", sample_rate},
        {"format", static_cast<int>(format)},
    };
}

nlohmann::json decode_rtp_frames(AudioFormat format, const nlohmann::json& frames_json) {
    std::vector<int16_t> pcm;
    for (const auto& frame : frames_json) {
        std::vector<uint8_t> packet;
        for (const auto& v : frame) {
            packet.push_back(static_cast<uint8_t>(v.get<int>()));
        }
        rtp::RtpDepacketizer depacketizer(rtp_clock_rate_for_format(format));
        rtp::PayloadType pt = rtp::PayloadType::Pcmu;
        switch (format) {
            case AudioFormat::RtpPcma: pt = rtp::PayloadType::Pcma; break;
            case AudioFormat::RtpG722: pt = rtp::PayloadType::G722; break;
            case AudioFormat::RtpG729: pt = rtp::PayloadType::G729; break;
            default: break;
        }
        auto decoded = depacketizer.decode_packet(packet, pt);
        pcm.insert(pcm.end(), decoded.pcm.begin(), decoded.pcm.end());
    }
    const auto bytes = pcm_to_bytes(pcm);
    nlohmann::json arr = nlohmann::json::array();
    for (uint8_t b : bytes) {
        arr.push_back(b);
    }
    return {
        {"pcm_bytes", arr},
        {"sample_rate", sample_rate_for_format(format)},
    };
}

}  // namespace

RestServer::RestServer(std::string bind_addr,
                       std::string web_root,
                       std::string openapi_path,
                       std::shared_ptr<SessionManager> sessions,
                       std::shared_ptr<stt::SttSessionManager> stt_sessions)
    : bind_addr_(std::move(bind_addr)),
      web_root_(std::move(web_root)),
      openapi_path_(std::move(openapi_path)),
      sessions_(std::move(sessions)),
      stt_sessions_(std::move(stt_sessions)) {}

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
        res.set_header("Access-Control-Allow-Headers", "Content-Type, X-Sample-Rate, X-Audio-Format, X-Language");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    server->Get("/ready", [this](const httplib::Request&, httplib::Response& res) {
        nlohmann::json body = {
            {"status", "ready"},
            {"service", "voiceqas"},
            {"interfaces", {"rest", "grpc", "websocket", "stt"}},
            {"docs", {"/docs/swagger", "/docs/redoc", "/ui/"}},
            {"stt", {
                {"ready", stt_sessions_ && stt_sessions_->engine().ready()},
                {"endpoint", "/v1/stt/transcribe"},
                {"models", {"parakeet", "whisper", "auto"}},
            }},
            {"toolchain", {
                {"cpp_standard", "c++26"},
                {"gcc", "15"},
                {"deps_manager", "vcpkg"},
            }},
        };
        const auto versions = read_file_or_empty("dependency-versions.txt");
        if (!versions.empty()) {
            body["dependency_versions"] = versions;
        }
        res.set_content(body.dump(), "application/json");
    });

    server->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/ui/");
    });

    server->Get("/docs", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/docs/swagger");
    });

    server->Get("/docs/swagger", [this](const httplib::Request&, httplib::Response& res) {
        const auto content = read_file_or_empty(web_root_ + "/swagger.html");
        if (content.empty()) {
            res.status = 404;
            return;
        }
        res.set_content(content, "text/html; charset=utf-8");
    });

    server->Get("/docs/redoc", [this](const httplib::Request&, httplib::Response& res) {
        const auto content = read_file_or_empty(web_root_ + "/redoc.html");
        if (content.empty()) {
            res.status = 404;
            return;
        }
        res.set_content(content, "text/html; charset=utf-8");
    });

    server->Get("/openapi/voiceqas.yaml", [this](const httplib::Request&, httplib::Response& res) {
        const auto content = read_file_or_empty(openapi_path_);
        if (content.empty()) {
            res.status = 404;
            res.set_content("openapi spec not found", "text/plain");
            return;
        }
        res.set_content(content, "application/yaml");
    });

    server->Get("/v1/playground/grpc/ready", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(grpc_ready_json().dump(), "application/json");
    });

    server->Post("/v1/playground/grpc/ready", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(grpc_ready_json().dump(), "application/json");
    });

    server->Post("/v1/playground/grpc/analyze-batch", [this](const httplib::Request& req, httplib::Response& res) {
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
            const auto result = sessions_->analyze_batch(
                format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                sample_rate);
            res.set_content(batch_result_to_json(result).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server->Post("/v1/playground/grpc/analyze-stream", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            const std::string session_id = body.value("session_id", "playground");
            const auto format = format_from_json(body);
            const auto& frames = body.at("frames");

            std::ostringstream sse;
            for (const auto& frame : frames) {
                auto payload = decode_payload_json(frame);
                const int64_t ts = frame.value("timestamp_ms", 0);
                if (auto report = sessions_->push_frame(
                        session_id,
                        format,
                        std::span<const uint8_t>(payload.data(), payload.size()),
                        ts)) {
                    auto json = window_metrics_to_json(*report);
                    json["session_id"] = session_id;
                    sse << "data: " << json.dump() << "\n\n";
                }
            }
            sessions_->remove_session(session_id);
            res.set_header("Cache-Control", "no-cache");
            res.set_content(sse.str(), "text/event-stream");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server->Post("/v1/analyze/batch", [this](const httplib::Request& req, httplib::Response& res) {
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

            const auto result = sessions_->analyze_batch(
                format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                sample_rate);

            res.set_content(batch_result_to_json(result).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server->Post("/v1/tools/pack-rtp", [](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            const auto format = audio_format_from_string(body.at("format").get<std::string>());
            if (format != AudioFormat::RtpG722 && format != AudioFormat::RtpG729) {
                res.status = 400;
                res.set_content(R"({"error":"pack-rtp supports rtp_g722 and rtp_g729 only"})", "application/json");
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

    server->Post("/v1/tools/decode-rtp", [](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            const auto format = audio_format_from_string(body.at("format").get<std::string>());
            res.set_content(decode_rtp_frames(format, body.at("frames")).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server->Get("/v1/stt/ready", [this](const httplib::Request&, httplib::Response& res) {
        nlohmann::json models = nlohmann::json::array();
        std::string status = "unavailable";
        std::string language = "pt";
        if (stt_sessions_) {
            const auto ready = stt_sessions_->engine().ready_status();
            status = ready.ready() ? "ready" : "unavailable";
            language = stt_sessions_->config().language;
            if (ready.parakeet_ready) {
                models.push_back({
                    {"id", "parakeet"},
                    {"name", ready.parakeet_model},
                    {"ready", true},
                });
            }
            if (ready.whisper_ready) {
                models.push_back({
                    {"id", "whisper"},
                    {"name", ready.whisper_model},
                    {"ready", true},
                });
            }
            models.push_back({
                {"id", "auto"},
                {"name", "parakeet with whisper fallback"},
                {"ready", ready.ready()},
            });
        }
        res.set_content(nlohmann::json{
            {"status", status},
            {"service", "voiceqas-stt"},
            {"language", language},
            {"default_model", stt_sessions_ ? stt_sessions_->config().default_model : "auto"},
            {"models", models},
        }.dump(), "application/json");
    });

    server->Post("/v1/stt/transcribe", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            if (!stt_sessions_) {
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

            const auto options = stt::transcribe_options_from_request(req, stt_sessions_->config());

            const auto result = stt_sessions_->transcribe_batch(
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

    server->Post("/v1/stt/transcribe/segment", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            if (!stt_sessions_) {
                res.status = 503;
                res.set_content(R"({"error":"STT not configured"})", "application/json");
                return;
            }
            const auto body = nlohmann::json::parse(req.body);
            const auto format = audio_format_from_string(body.at("format").get<std::string>());
            const int sample_rate = body.value("sample_rate", sample_rate_for_format(format));
            auto payload = decode_payload_json(body);

            const auto options = stt::transcribe_options_from_request(req, stt_sessions_->config(), &body);

            const auto result = stt_sessions_->transcribe_batch(
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

    server->Post("/v1/playground/grpc/stt-transcribe", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            if (!stt_sessions_) {
                res.status = 503;
                res.set_content(R"({"error":"STT not configured"})", "application/json");
                return;
            }
            const auto body = nlohmann::json::parse(req.body);
            const auto format = format_from_json(body);
            const int sample_rate = body.value("sample_rate", sample_rate_for_format(format));
            auto payload = decode_payload_json(body);

            const auto options = stt::transcribe_options_from_request(req, stt_sessions_->config(), &body);

            const auto result = stt_sessions_->transcribe_batch(
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

    server->Post("/v1/analyze/segment", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            const auto format = audio_format_from_string(body.at("format").get<std::string>());
            const int sample_rate = body.value("sample_rate", sample_rate_for_format(format));
            auto payload = decode_payload_json(body);

            const auto result = sessions_->analyze_batch(
                format,
                std::span<const uint8_t>(payload.data(), payload.size()),
                sample_rate);
            res.set_content(batch_result_to_json(result).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

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
