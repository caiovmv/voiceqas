#pragma once

#include <nlohmann/json.hpp>

#include "voiceqas/stt/client.hpp"

namespace voiceqas {

nlohmann::json transcript_to_json(const stt::TranscriptResult& result);

}  // namespace voiceqas
