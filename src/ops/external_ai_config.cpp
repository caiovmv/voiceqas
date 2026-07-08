#include "voiceqas/ops/external_ai_config.hpp"

namespace voiceqas::ops {

namespace {

ExternalAiConfig g_external_ai_config;

}  // namespace

void configure_external_ai(const ExternalAiConfig& config) {
    g_external_ai_config = config;
}

const ExternalAiConfig& external_ai_config() {
    return g_external_ai_config;
}

}  // namespace voiceqas::ops