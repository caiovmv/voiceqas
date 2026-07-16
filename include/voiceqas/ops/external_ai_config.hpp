#pragma once

#include <cstdint>
#include <string>

namespace voiceqas::ops {

struct ExternalAiConfig {
    bool enabled = false;
    std::string provider = "ollama";
    std::string base_url = "http://host.docker.internal:11434";
    std::string model = "gemma4:e4b";
    int timeout_ms = 60000;
};

void configure_external_ai(const ExternalAiConfig& config);
const ExternalAiConfig& external_ai_config();

}  // namespace voiceqas::ops