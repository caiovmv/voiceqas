#include "voiceqas/config/channel_registry.hpp"

#include "voiceqas/audio/strip_json.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace voiceqas::config {
namespace {

std::string iso_now() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

ChannelTransport parse_transport(const std::string& s) {
    if (s == "sip_trunk" || s == "sip") {
        return ChannelTransport::SipTrunk;
    }
    if (s == "websocket" || s == "ws") {
        return ChannelTransport::WebSocket;
    }
    if (s == "webrtc") {
        return ChannelTransport::WebRtc;
    }
    if (s == "grpc") {
        return ChannelTransport::Grpc;
    }
    return ChannelTransport::Rest;
}

nlohmann::json yaml_to_json(const YAML::Node& node) {
    if (!node.IsDefined() || node.IsNull()) {
        return nullptr;
    }
    if (node.IsScalar()) {
        try {
            return node.as<std::string>();
        } catch (...) {
        }
        try {
            return node.as<bool>();
        } catch (...) {
        }
        try {
            return node.as<int>();
        } catch (...) {
        }
        try {
            return node.as<double>();
        } catch (...) {
        }
        return node.Scalar();
    }
    if (node.IsSequence()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : node) {
            arr.push_back(yaml_to_json(item));
        }
        return arr;
    }
    if (node.IsMap()) {
        nlohmann::json obj = nlohmann::json::object();
        for (const auto& it : node) {
            obj[it.first.as<std::string>()] = yaml_to_json(it.second);
        }
        return obj;
    }
    return nullptr;
}

void apply_pipeline_overlay(audio::AudioProcessingConfig& audio, const nlohmann::json& overlay) {
    if (!overlay.is_object()) {
        return;
    }
    if (overlay.contains("enhancement") && overlay["enhancement"].is_object()) {
        const auto& e = overlay["enhancement"];
        if (e.contains("enabled")) {
            audio.enhancement.enabled = e["enabled"].get<bool>();
        }
        if (e.contains("wet_dry")) {
            audio.enhancement.wet_dry = e["wet_dry"].get<double>();
        }
    }
    if (overlay.contains("strip")) {
        audio::merge_strip_json(audio.strip, overlay["strip"]);
    } else {
        audio::merge_strip_json(audio.strip, overlay);
    }
    audio.sync_legacy_from_strip();
}

nlohmann::json strip_to_json(const audio::ChannelStripConfig& strip) {
    nlohmann::json bands = nlohmann::json::array();
    for (const auto& b : strip.eq.bands) {
        bands.push_back({{"freq_hz", b.freq_hz}, {"gain_db", b.gain_db}, {"q", b.q}});
    }
    return {
        {"nr", {{"enabled", strip.nr.enabled}, {"wet_dry", strip.nr.wet_dry}}},
        {"hpf", {{"enabled", strip.hpf.enabled}, {"cutoff_hz", strip.hpf.cutoff_hz}}},
        {"eq", {{"enabled", strip.eq.enabled}, {"bands", bands}}},
        {"deesser",
         {{"enabled", strip.deesser.enabled},
          {"center_hz", strip.deesser.center_hz},
          {"bandwidth_hz", strip.deesser.bandwidth_hz},
          {"threshold_db", strip.deesser.threshold_db},
          {"ratio", strip.deesser.ratio},
          {"attack_ms", strip.deesser.attack_ms},
          {"release_ms", strip.deesser.release_ms}}},
        {"compressor",
         {{"enabled", strip.compressor.enabled},
          {"threshold_db", strip.compressor.threshold_db},
          {"ratio", strip.compressor.ratio},
          {"attack_ms", strip.compressor.attack_ms},
          {"release_ms", strip.compressor.release_ms},
          {"makeup_db", strip.compressor.makeup_db}}},
        {"limiter", {{"enabled", strip.limiter.enabled}, {"ceiling_dbfs", strip.limiter.ceiling_dbfs}}},
        {"agc",
         {{"enabled", strip.agc.enabled},
          {"target_rms_dbfs", strip.agc.target_rms_dbfs},
          {"max_gain_db", strip.agc.max_gain_db},
          {"attack_ms", strip.agc.attack_ms},
          {"release_ms", strip.agc.release_ms}}},
    };
}

}  // namespace

ChannelTransport transport_from_string(const std::string& s) {
    return parse_transport(s);
}

std::string transport_to_string(ChannelTransport t) {
    switch (t) {
        case ChannelTransport::SipTrunk:
            return "sip_trunk";
        case ChannelTransport::WebSocket:
            return "websocket";
        case ChannelTransport::WebRtc:
            return "webrtc";
        case ChannelTransport::Grpc:
            return "grpc";
        default:
            return "rest";
    }
}

ChannelRegistry::ChannelRegistry(
    std::string path,
    audio::AudioProcessingConfig base_audio,
    stt::SttConfig base_stt)
    : path_(std::move(path)), base_audio_(std::move(base_audio)), base_stt_(std::move(base_stt)) {
    load_from_disk();
}

void ChannelRegistry::load_from_disk() {
    std::lock_guard lock(mutex_);
    channels_.clear();
    if (path_.empty()) {
        ChannelProfile def;
        def.id = "default";
        def.label = "Global default";
        channels_.push_back(std::move(def));
        return;
    }
    try {
        if (!std::ifstream(path_).good()) {
            ChannelProfile def;
            def.id = "default";
            def.label = "Global default";
            channels_.push_back(std::move(def));
            return;
        }
        const YAML::Node root = YAML::LoadFile(path_);
        if (root["default_channel_id"]) {
            default_channel_id_ = root["default_channel_id"].as<std::string>();
        }
        if (root["pipeline_default"]) {
            pipeline_default_overlay_ = yaml_to_json(root["pipeline_default"]);
        }
        if (root["channels"] && root["channels"].IsSequence()) {
            for (const auto& node : root["channels"]) {
                ChannelProfile ch;
                ch.id = node["id"].as<std::string>();
                ch.label = node["label"] ? node["label"].as<std::string>() : ch.id;
                ch.transport = parse_transport(
                    node["transport"] ? node["transport"].as<std::string>() : "rest");
                ch.enabled = node["enabled"] ? node["enabled"].as<bool>() : true;
                ch.inherit_pipeline = node["inherit_pipeline"] ? node["inherit_pipeline"].as<bool>() : true;
                if (node["media"]) {
                    const auto& m = node["media"];
                    if (m["preferred_ingress_codec"]) {
                        ch.media.preferred_ingress_codec = m["preferred_ingress_codec"].as<std::string>();
                    }
                    if (m["remote_host"]) {
                        ch.media.remote_host = m["remote_host"].as<std::string>();
                    }
                    if (m["remote_port"]) {
                        ch.media.remote_port = static_cast<uint16_t>(m["remote_port"].as<int>());
                    }
                }
                if (node["pipeline_override"]) {
                    ch.pipeline_override = yaml_to_json(node["pipeline_override"]);
                }
                if (node["stt_override"]) {
                    const auto j = yaml_to_json(node["stt_override"]);
                    if (j.contains("require_stt_ready")) {
                        ch.stt_override.require_stt_ready = j["require_stt_ready"].get<bool>();
                    }
                    if (j.contains("diarization_focus_primary")) {
                        ch.stt_override.diarization_focus_primary =
                            j["diarization_focus_primary"].get<bool>();
                    }
                }
                if (node["updated_at"]) {
                    ch.updated_at = node["updated_at"].as<std::string>();
                }
                channels_.push_back(std::move(ch));
            }
        }
    } catch (...) {
        channels_.clear();
        ChannelProfile def;
        def.id = "default";
        def.label = "Global default";
        channels_.push_back(std::move(def));
    }
    if (channels_.empty()) {
        ChannelProfile def;
        def.id = "default";
        def.label = "Global default";
        channels_.push_back(std::move(def));
    }
}

bool ChannelRegistry::save_to_disk(std::string& error) const {
    if (path_.empty()) {
        error = "channels config path not set";
        return false;
    }
    try {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "version" << YAML::Value << 1;
        out << YAML::Key << "default_channel_id" << YAML::Value << default_channel_id_;
        out << YAML::Key << "pipeline_default";
        out << YAML::Value << YAML::Clone(YAML::Load(pipeline_default_overlay_.dump()));
        out << YAML::Key << "channels";
        out << YAML::Value << YAML::BeginSeq;
        for (const auto& ch : channels_) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << ch.id;
            out << YAML::Key << "label" << YAML::Value << ch.label;
            out << YAML::Key << "transport" << YAML::Value << transport_to_string(ch.transport);
            out << YAML::Key << "enabled" << YAML::Value << ch.enabled;
            out << YAML::Key << "inherit_pipeline" << YAML::Value << ch.inherit_pipeline;
            if (!ch.media.preferred_ingress_codec.empty() || !ch.media.remote_host.empty() ||
                ch.media.remote_port) {
                out << YAML::Key << "media" << YAML::Value << YAML::BeginMap;
                if (!ch.media.preferred_ingress_codec.empty()) {
                    out << YAML::Key << "preferred_ingress_codec" << YAML::Value
                        << ch.media.preferred_ingress_codec;
                }
                if (!ch.media.remote_host.empty()) {
                    out << YAML::Key << "remote_host" << YAML::Value << ch.media.remote_host;
                }
                if (ch.media.remote_port) {
                    out << YAML::Key << "remote_port" << YAML::Value
                        << static_cast<int>(ch.media.remote_port);
                }
                out << YAML::EndMap;
            }
            if (!ch.pipeline_override.empty()) {
                out << YAML::Key << "pipeline_override";
                out << YAML::Value << YAML::Clone(YAML::Load(ch.pipeline_override.dump()));
            }
            if (ch.stt_override.require_stt_ready || ch.stt_override.diarization_focus_primary) {
                out << YAML::Key << "stt_override" << YAML::Value << YAML::BeginMap;
                if (ch.stt_override.require_stt_ready) {
                    out << YAML::Key << "require_stt_ready" << YAML::Value
                        << *ch.stt_override.require_stt_ready;
                }
                if (ch.stt_override.diarization_focus_primary) {
                    out << YAML::Key << "diarization_focus_primary" << YAML::Value
                        << *ch.stt_override.diarization_focus_primary;
                }
                out << YAML::EndMap;
            }
            if (!ch.updated_at.empty()) {
                out << YAML::Key << "updated_at" << YAML::Value << ch.updated_at;
            }
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
        std::ofstream file(path_, std::ios::trunc);
        if (!file) {
            error = "failed to open channels config for write";
            return false;
        }
        file << out.c_str();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::optional<ChannelProfile> ChannelRegistry::find_channel(const std::string& id) const {
    for (const auto& ch : channels_) {
        if (ch.id == id) {
            return ch;
        }
    }
    return std::nullopt;
}

audio::AudioProcessingConfig ChannelRegistry::resolve_audio(const std::string& channel_id) const {
    std::lock_guard lock(mutex_);
    audio::AudioProcessingConfig audio = base_audio_;
    apply_pipeline_overlay(audio, pipeline_default_overlay_);
    const auto ch = find_channel(channel_id);
    if (ch && !ch->inherit_pipeline && !ch->pipeline_override.empty()) {
        apply_pipeline_overlay(audio, ch->pipeline_override);
    }
    return audio;
}

stt::SttConfig ChannelRegistry::resolve_stt(const std::string& channel_id) const {
    std::lock_guard lock(mutex_);
    stt::SttConfig stt = base_stt_;
    if (pipeline_default_overlay_.contains("stt") &&
        pipeline_default_overlay_["stt"].is_object()) {
        const auto& s = pipeline_default_overlay_["stt"];
        if (s.contains("require_stt_ready")) {
            stt.require_stt_ready = s["require_stt_ready"].get<bool>();
        }
        if (s.contains("diarization_focus_primary")) {
            stt.diarization.focus_primary = s["diarization_focus_primary"].get<bool>();
        }
    }
    const auto ch = find_channel(channel_id);
    if (ch) {
        if (ch->stt_override.require_stt_ready) {
            stt.require_stt_ready = *ch->stt_override.require_stt_ready;
        }
        if (ch->stt_override.diarization_focus_primary) {
            stt.diarization.focus_primary = *ch->stt_override.diarization_focus_primary;
        }
    }
    return stt;
}

nlohmann::json ChannelRegistry::pipeline_default_json() const {
    std::lock_guard lock(mutex_);
    audio::AudioProcessingConfig audio = base_audio_;
    apply_pipeline_overlay(audio, pipeline_default_overlay_);
    nlohmann::json j = {
        {"audio",
         {{"enhancement",
           {{"enabled", audio.enhancement.enabled}, {"wet_dry", audio.enhancement.wet_dry}}},
          {"strip", strip_to_json(audio.strip)}}},
        {"stt",
         {{"require_stt_ready", base_stt_.require_stt_ready},
          {"diarization_focus_primary", base_stt_.diarization.focus_primary},
          {"language", base_stt_.language},
          {"default_model", base_stt_.default_model}}},
    };
    if (pipeline_default_overlay_.contains("stt")) {
        j["stt"].update(pipeline_default_overlay_["stt"]);
    }
    return j;
}

nlohmann::json ChannelRegistry::channel_to_json(const ChannelProfile& ch, bool include_effective) const {
    nlohmann::json j = {
        {"id", ch.id},
        {"label", ch.label},
        {"transport", transport_to_string(ch.transport)},
        {"enabled", ch.enabled},
        {"inherit_pipeline", ch.inherit_pipeline},
        {"signaling",
         ch.transport == ChannelTransport::SipTrunk || ch.transport == ChannelTransport::WebRtc
             ? "pending"
             : "n/a"},
    };
    if (!ch.media.preferred_ingress_codec.empty() || !ch.media.remote_host.empty() ||
        ch.media.remote_port) {
        j["media"] = {{"preferred_ingress_codec", ch.media.preferred_ingress_codec},
                      {"remote_host", ch.media.remote_host},
                      {"remote_port", ch.media.remote_port}};
    }
    if (!ch.pipeline_override.empty()) {
        j["pipeline_override"] = ch.pipeline_override;
    }
    if (ch.stt_override.require_stt_ready || ch.stt_override.diarization_focus_primary) {
        j["stt_override"] = nlohmann::json::object();
        if (ch.stt_override.require_stt_ready) {
            j["stt_override"]["require_stt_ready"] = *ch.stt_override.require_stt_ready;
        }
        if (ch.stt_override.diarization_focus_primary) {
            j["stt_override"]["diarization_focus_primary"] =
                *ch.stt_override.diarization_focus_primary;
        }
    }
    if (!ch.updated_at.empty()) {
        j["updated_at"] = ch.updated_at;
    }
    if (include_effective) {
        j["effective_pipeline"] = pipeline_default_json()["audio"];
        if (!ch.inherit_pipeline && !ch.pipeline_override.empty()) {
            auto audio = resolve_audio(ch.id);
            j["effective_pipeline"] = {
                {"enhancement",
                 {{"enabled", audio.enhancement.enabled}, {"wet_dry", audio.enhancement.wet_dry}}},
                {"strip", strip_to_json(audio.strip)},
            };
        }
        j["has_override"] = !ch.inherit_pipeline && !ch.pipeline_override.empty();
    }
    return j;
}

nlohmann::json ChannelRegistry::list_json() const {
    std::lock_guard lock(mutex_);
    nlohmann::json channels = nlohmann::json::array();
    for (const auto& ch : channels_) {
        channels.push_back(channel_to_json(ch, true));
    }
    return {{"status", "ok"},
            {"default_channel_id", default_channel_id_},
            {"pipeline_default", pipeline_default_json()},
            {"channels", channels}};
}

nlohmann::json ChannelRegistry::get_json(const std::string& id) const {
    std::lock_guard lock(mutex_);
    for (const auto& ch : channels_) {
        if (ch.id == id) {
            return {{"status", "ok"}, {"channel", channel_to_json(ch, true)}};
        }
    }
    return {{"error", "channel not found"}};
}

bool ChannelRegistry::upsert_channel(const ChannelProfile& profile, std::string& error) {
    std::lock_guard lock(mutex_);
    if (profile.id.empty()) {
        error = "channel id required";
        return false;
    }
    ChannelProfile copy = profile;
    copy.updated_at = iso_now();
    bool found = false;
    for (auto& ch : channels_) {
        if (ch.id == copy.id) {
            ch = std::move(copy);
            found = true;
            break;
        }
    }
    if (!found) {
        channels_.push_back(std::move(copy));
    }
    return save_to_disk(error);
}

bool ChannelRegistry::delete_channel(const std::string& id, std::string& error) {
    std::lock_guard lock(mutex_);
    if (id == "default" || id == default_channel_id_) {
        error = "cannot delete default channel";
        return false;
    }
    const auto it = std::remove_if(
        channels_.begin(), channels_.end(), [&](const ChannelProfile& ch) { return ch.id == id; });
    if (it == channels_.end()) {
        error = "channel not found";
        return false;
    }
    channels_.erase(it, channels_.end());
    return save_to_disk(error);
}

bool ChannelRegistry::update_pipeline_default(const nlohmann::json& body, std::string& error) {
    std::lock_guard lock(mutex_);
    if (body.contains("audio")) {
        if (body["audio"].contains("enhancement")) {
            pipeline_default_overlay_["enhancement"] = body["audio"]["enhancement"];
        }
        if (body["audio"].contains("strip")) {
            pipeline_default_overlay_["strip"] = body["audio"]["strip"];
        }
    }
    if (body.contains("stt")) {
        pipeline_default_overlay_["stt"] = body["stt"];
    }
    audio::AudioProcessingConfig merged = base_audio_;
    apply_pipeline_overlay(merged, pipeline_default_overlay_);
    base_audio_ = merged;
    if (body.contains("stt") && body["stt"].is_object()) {
        const auto& s = body["stt"];
        if (s.contains("require_stt_ready")) {
            base_stt_.require_stt_ready = s["require_stt_ready"].get<bool>();
        }
        if (s.contains("diarization_focus_primary")) {
            base_stt_.diarization.focus_primary = s["diarization_focus_primary"].get<bool>();
        }
    }
    return save_to_disk(error);
}

}  // namespace voiceqas::config
