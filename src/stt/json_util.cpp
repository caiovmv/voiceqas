#include "voiceqas/stt/json_util.hpp"

namespace voiceqas {

nlohmann::json transcript_to_json(const stt::TranscriptResult& result) {
    nlohmann::json segments = nlohmann::json::array();
    for (const auto& seg : result.segments) {
        segments.push_back({
            {"start_ms", seg.start_ms},
            {"end_ms", seg.end_ms},
            {"text", seg.text},
        });
    }

    nlohmann::json json = {
        {"text", result.text},
        {"model", result.model},
        {"language", result.language},
        {"duration_ms", result.duration_ms},
        {"processing_ms", result.processing_ms},
        {"segments", segments},
        {"ok", result.ok},
    };
    if (!result.error.empty()) {
        json["error"] = result.error;
    }
    return json;
}

}  // namespace voiceqas
