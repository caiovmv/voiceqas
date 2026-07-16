#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "voiceqas/audio/config.hpp"
#include "voiceqas/stt/client.hpp"

namespace voiceqas::config {

enum class ChannelTransport {
    SipTrunk,
    WebSocket,
    WebRtc,
    Rest,
    Grpc,
};

ChannelTransport transport_from_string(const std::string& s);
std::string transport_to_string(ChannelTransport t);

struct ChannelMediaConfig {
    std::string preferred_ingress_codec;
    std::string remote_host;
    uint16_t remote_port = 0;
};

struct ChannelSttOverride {
    std::optional<bool> require_stt_ready;
    std::optional<bool> diarization_focus_primary;
};

struct ChannelProfile {
    std::string id;
    std::string label;
    ChannelTransport transport = ChannelTransport::Rest;
    bool enabled = true;
    bool inherit_pipeline = true;
    ChannelMediaConfig media;
    nlohmann::json pipeline_override = nlohmann::json::object();
    ChannelSttOverride stt_override;
    std::string updated_at;
};

/** Persistent channel profiles + pipeline default overlay (YAML on disk). */
class ChannelRegistry {
public:
    ChannelRegistry(
        std::string path,
        audio::AudioProcessingConfig base_audio,
        stt::SttConfig base_stt);

    nlohmann::json list_json() const;
    nlohmann::json get_json(const std::string& id) const;
    nlohmann::json pipeline_default_json() const;

    audio::AudioProcessingConfig resolve_audio(const std::string& channel_id) const;
    stt::SttConfig resolve_stt(const std::string& channel_id) const;

    bool upsert_channel(const ChannelProfile& profile, std::string& error);
    bool delete_channel(const std::string& id, std::string& error);
    bool update_pipeline_default(const nlohmann::json& body, std::string& error);

    const std::string& path() const { return path_; }

private:
    void load_from_disk();
    bool save_to_disk(std::string& error) const;
    nlohmann::json channel_to_json(const ChannelProfile& ch, bool include_effective) const;
    std::optional<ChannelProfile> find_channel(const std::string& id) const;

    mutable std::recursive_mutex mutex_;
    std::string path_;
    audio::AudioProcessingConfig base_audio_;
    stt::SttConfig base_stt_;
    nlohmann::json pipeline_default_overlay_ = nlohmann::json::object();
    std::string default_channel_id_ = "default";
    std::vector<ChannelProfile> channels_;
};

}  // namespace voiceqas::config
