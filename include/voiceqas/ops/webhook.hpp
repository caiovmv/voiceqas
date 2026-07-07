#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "voiceqas/metrics.hpp"

namespace voiceqas::ops {

void dispatch_webhook(const std::string& kind, const nlohmann::json& payload);
std::optional<nlohmann::json> evaluate_vqa_alert(const std::string& session_id, const nlohmann::json& event);
void publish_codec_mismatch_alert(
    const std::string& session_id,
    AudioFormat preferred,
    AudioFormat actual,
    const std::string& source);

}  // namespace voiceqas::ops
