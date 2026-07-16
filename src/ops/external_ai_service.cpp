#include "voiceqas/ops/external_ai_service.hpp"

#include <chrono>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "voiceqas/ops/external_ai_config.hpp"
#include "voiceqas/ops/pipeline_tracker.hpp"
#include "voiceqas/ops/transport_sankey.hpp"

namespace voiceqas::ops {

namespace {

struct ParsedUrl {
    std::string host;
    int port = 11434;
    bool https = false;
};

ParsedUrl parse_base_url(const std::string& url) {
    ParsedUrl out;
    std::string rest = url;
    if (rest.rfind("https://", 0) == 0) {
        out.https = true;
        rest = rest.substr(8);
    } else if (rest.rfind("http://", 0) == 0) {
        rest = rest.substr(7);
    }
    const auto slash = rest.find('/');
    if (slash != std::string::npos) {
        rest = rest.substr(0, slash);
    }
    const auto colon = rest.rfind(':');
    if (colon != std::string::npos) {
        out.host = rest.substr(0, colon);
        out.port = std::stoi(rest.substr(colon + 1));
    } else {
        out.host = rest;
        out.port = out.https ? 443 : 80;
    }
    return out;
}

int64_t elapsed_ms(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

}  // namespace

ExternalAiService& ExternalAiService::instance() {
    static ExternalAiService service;
    return service;
}

void ExternalAiService::on_stt_final(
    const std::string& session_id,
    const std::string& text,
    uint64_t transcript_bytes) {
    const auto& cfg = external_ai_config();
    const auto request_bytes = std::max<uint64_t>(transcript_bytes, text.size());
    PipelineTracker::instance().record_external_ai_inbound(session_id, request_bytes, 0.0);

    if (!cfg.enabled || text.empty()) {
        PipelineTracker::instance().record_external_ai_outbound(session_id, request_bytes, 0.0);
        return;
    }

    std::thread([session_id, text, request_bytes, cfg]() {
        const auto start = std::chrono::steady_clock::now();
        const auto parsed = parse_base_url(cfg.base_url);
        httplib::Client cli(parsed.host, parsed.port);
        cli.set_connection_timeout(cfg.timeout_ms / 1000, (cfg.timeout_ms % 1000) * 1000);
        cli.set_read_timeout(cfg.timeout_ms / 1000, (cfg.timeout_ms % 1000) * 1000);

        nlohmann::json body = {
            {"model", cfg.model},
            {"stream", false},
            {"messages",
             nlohmann::json::array({{{"role", "user"}, {"content", text}}})},
        };

        uint64_t response_bytes = 0;
        double latency_ms = 0.0;
        if (auto res = cli.Post("/api/chat", body.dump(), "application/json")) {
            latency_ms = static_cast<double>(elapsed_ms(start));
            if (res->status >= 200 && res->status < 300) {
                try {
                    const auto json = nlohmann::json::parse(res->body);
                    std::string content;
                    if (json.contains("message") && json["message"].is_object() &&
                        json["message"].contains("content")) {
                        content = json["message"]["content"].get<std::string>();
                    }
                    response_bytes = std::max<uint64_t>(content.size(), res->body.size());
                } catch (...) {
                    response_bytes = res->body.size();
                }
            } else {
                response_bytes = res->body.size();
            }
        } else {
            latency_ms = static_cast<double>(elapsed_ms(start));
            response_bytes = request_bytes;
        }

        PipelineTracker::instance().record_external_ai_outbound(session_id, response_bytes, latency_ms);
        PipelineTracker::instance().record_transport_egress(
            session_id, transport_node::kRest, response_bytes, latency_ms);
        publish_pipeline_snapshot(session_id);
    }).detach();
}

std::string ExternalAiService::chat(
    const std::string& system_prompt,
    const std::string& user_prompt,
    std::string* error_out) {
    const auto& cfg = external_ai_config();
    if (cfg.base_url.empty()) {
        if (error_out) {
            *error_out = "external_ai.base_url empty";
        }
        return {};
    }

    const auto parsed = parse_base_url(cfg.base_url);
    httplib::Client cli(parsed.host, parsed.port);
    cli.set_connection_timeout(cfg.timeout_ms / 1000, (cfg.timeout_ms % 1000) * 1000);
    cli.set_read_timeout(cfg.timeout_ms / 1000, (cfg.timeout_ms % 1000) * 1000);

    nlohmann::json messages = nlohmann::json::array();
    if (!system_prompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", system_prompt}});
    }
    messages.push_back({{"role", "user"}, {"content", user_prompt}});

    nlohmann::json body = {
        {"model", cfg.model},
        {"stream", false},
        {"messages", messages},
        // Low temperature keeps local models closer to the Lab section template.
        {"options", {{"temperature", 0.2}, {"top_p", 0.9}}},
    };

    auto res = cli.Post("/api/chat", body.dump(), "application/json");
    if (!res) {
        if (error_out) {
            *error_out = "failed to reach Ollama at " + cfg.base_url;
        }
        return {};
    }
    if (res->status < 200 || res->status >= 300) {
        if (error_out) {
            *error_out = "Ollama HTTP " + std::to_string(res->status) + ": " + res->body;
        }
        return {};
    }
    try {
        const auto json = nlohmann::json::parse(res->body);
        if (json.contains("message") && json["message"].is_object() &&
            json["message"].contains("content")) {
            return json["message"]["content"].get<std::string>();
        }
        if (error_out) {
            *error_out = "unexpected Ollama response shape";
        }
    } catch (const std::exception& e) {
        if (error_out) {
            *error_out = e.what();
        }
    }
    return {};
}

}  // namespace voiceqas::ops
