#include "voiceqas/stt/model_util.hpp"

#include <algorithm>
#include <cctype>

namespace voiceqas::stt {

namespace {

std::string header_value(const httplib::Request& req, const char* name) {
    return req.get_header_value(name);
}

SttModelChoice default_model_choice(const SttConfig& config) {
    return parse_model_choice(config.default_model, SttModelChoice::Auto);
}

}  // namespace

TranscribeOptions transcribe_options_from_json(
    const nlohmann::json& body,
    const SttConfig& config) {
    TranscribeOptions options;
    options.model = default_model_choice(config);
    if (body.contains("model")) {
        options.model = parse_model_choice(body.at("model").get<std::string>(), options.model);
    }
    if (body.contains("language")) {
        options.language = body.at("language").get<std::string>();
    } else {
        options.language = config.language;
    }
    if (body.contains("provider")) {
        options.provider = body.at("provider").get<std::string>();
    }
    return options;
}

TranscribeOptions transcribe_options_from_request(
    const httplib::Request& req,
    const SttConfig& config,
    const nlohmann::json* body) {
    TranscribeOptions options;
    options.model = default_model_choice(config);
    options.language = config.language;

    if (const auto header_model = header_value(req, "X-STT-Model"); !header_model.empty()) {
        options.model = parse_model_choice(header_model, options.model);
    } else if (req.has_param("model")) {
        options.model = parse_model_choice(req.get_param_value("model"), options.model);
    } else if (body && body->contains("model")) {
        options.model = parse_model_choice(body->at("model").get<std::string>(), options.model);
    }

    if (const auto header_lang = header_value(req, "X-Language"); !header_lang.empty()) {
        options.language = header_lang;
    } else if (body && body->contains("language")) {
        options.language = body->at("language").get<std::string>();
    }

    if (const auto session_id = header_value(req, "X-Session-Id"); !session_id.empty()) {
        options.telemetry_session_id = session_id;
    } else if (body && body->contains("session_id")) {
        options.telemetry_session_id = body->at("session_id").get<std::string>();
    }

    if (const auto provider = header_value(req, "X-STT-Provider"); !provider.empty()) {
        options.provider = provider;
    } else if (body && body->contains("provider")) {
        options.provider = body->at("provider").get<std::string>();
    }

    return options;
}

}  // namespace voiceqas::stt
