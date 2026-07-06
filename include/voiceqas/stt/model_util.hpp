#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "voiceqas/stt/client.hpp"

namespace voiceqas::stt {

TranscribeOptions transcribe_options_from_json(
    const nlohmann::json& body,
    const SttConfig& config);

TranscribeOptions transcribe_options_from_request(
    const httplib::Request& req,
    const SttConfig& config,
    const nlohmann::json* body = nullptr);

}  // namespace voiceqas::stt
