#pragma once

#include <cstdint>
#include <string>

namespace voiceqas::ops {

struct ExternalAiConfig {
    bool enabled = false;
    std::string provider = "ollama";
    std::string base_url = "http://host.docker.internal:11434";
    std::string model = "cryptidbleh/gemma4-claude-sonnet-4.6";
    int timeout_ms = 60000;
};

void configure_external_ai(const ExternalAiConfig& config);
const ExternalAiConfig& external_ai_config();

}  // namespace voiceqas::ops