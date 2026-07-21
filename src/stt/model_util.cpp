#include "voiceqas/stt/model_util.hpp"

#include <algorithm>
#include <cctype>

#include "voiceqas/audio/strip_json.hpp"

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

    auto parse_bool = [](const std::string& v) -> std::optional<bool> {
        if (v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "yes") {
            return true;
        }
        if (v == "0" || v == "false" || v == "FALSE" || v == "off" || v == "no") {
            return false;
        }
        return std::nullopt;
    };

    if (const auto v = header_value(req, "X-Audio-AGC"); !v.empty()) {
        options.normalize_enabled = parse_bool(v);
    } else if (body && body->contains("normalize_enabled")) {
        options.normalize_enabled = body->at("normalize_enabled").get<bool>();
    }

    if (const auto v = header_value(req, "X-Audio-Enhancement"); !v.empty()) {
        options.enhancement_enabled = parse_bool(v);
    } else if (body && body->contains("enhancement_enabled")) {
        options.enhancement_enabled = body->at("enhancement_enabled").get<bool>();
    }

    audio::ChannelStripConfig strip = audio::ChannelStripConfig{};
    bool strip_set = false;
    if (const auto v = header_value(req, "X-Audio-Strip"); !v.empty()) {
        strip_set = audio::merge_strip_json_string(strip, v);
    } else if (body && body->contains("audio_strip") && body->at("audio_strip").is_object()) {
        audio::merge_strip_json(strip, body->at("audio_strip"));
        strip_set = true;
    }
    if (strip_set) {
        options.strip = strip;
    }

    if (const auto v = header_value(req, "X-STT-Diarization"); !v.empty()) {
        options.diarization_enabled = parse_bool(v);
    } else if (body && body->contains("diarization_enabled")) {
        options.diarization_enabled = body->at("diarization_enabled").get<bool>();
    }

    if (const auto v = header_value(req, "X-STT-Focus-Primary"); !v.empty()) {
        options.focus_primary = parse_bool(v);
    } else if (body && body->contains("focus_primary")) {
        options.focus_primary = body->at("focus_primary").get<bool>();
    }

    return options;
}

}  // namespace voiceqas::stt
