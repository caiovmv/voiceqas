#pragma once

#include <string>

#include "voiceqas/metrics.hpp"
#include "voiceqas/stt/client.hpp"

namespace voiceqas {

struct ServerConfig {
    std::string rest_addr = "0.0.0.0:8080";
    std::string grpc_addr = "0.0.0.0:50051";
    std::string ws_addr = "0.0.0.0:8081";
    std::string web_root = "web";
    std::string openapi_path = "openapi/voiceqas.yaml";
    std::string config_path;
};

ServerConfig load_config(int argc, char** argv);
AnalyzerConfig& global_analyzer_config();
stt::SttConfig& global_stt_config();

}  // namespace voiceqas
