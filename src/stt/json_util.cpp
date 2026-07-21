#include "voiceqas/stt/json_util.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace voiceqas {

nlohmann::json transcript_to_json(const stt::TranscriptResult& result) {
    nlohmann::json segments = nlohmann::json::array();
    for (const auto& seg : result.segments) {
        segments.push_back({
            {"start_ms", seg.start_ms},
            {"end_ms", seg.end_ms},
            {"text", seg.text},
            {"speaker_id", seg.speaker_id},
        });
    }

    nlohmann::json turns = nlohmann::json::array();
    for (const auto& turn : result.diarization_turns) {
        turns.push_back({
            {"start_ms", turn.start_ms},
            {"end_ms", turn.end_ms},
            {"rms_dbfs", turn.rms_dbfs},
            {"speaker_id", turn.speaker_id},
            {"is_primary", turn.is_primary},
            {"text", turn.text},
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
    if (!result.diarization_turns.empty()) {
        std::unordered_map<int, int64_t> duration_ms;
        std::unordered_map<int, int> turn_count;
        std::unordered_set<int> speakers;
        for (const auto& t : result.diarization_turns) {
            speakers.insert(t.speaker_id);
            duration_ms[t.speaker_id] += std::max<int64_t>(0, t.end_ms - t.start_ms);
            turn_count[t.speaker_id] += 1;
        }
        nlohmann::json speakers_json = nlohmann::json::array();
        for (int id : speakers) {
            speakers_json.push_back({
                {"speaker_id", id},
                {"is_primary", id == result.primary_speaker},
                {"duration_ms", duration_ms[id]},
                {"turn_count", turn_count[id]},
                {"role", id == result.primary_speaker ? "primary" : "secondary"},
            });
        }
        json["diarization"] = {
            {"turns", turns},
            {"primary_speaker", result.primary_speaker},
            {"speaker_count", static_cast<int>(speakers.size())},
            {"speakers", speakers_json},
        };
    }
    if (!result.error.empty()) {
        json["error"] = result.error;
    }
    return json;
}

}  // namespace voiceqas
