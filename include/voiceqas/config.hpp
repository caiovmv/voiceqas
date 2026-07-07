#pragma once

#include <string>

#include "voiceqas/audio/config.hpp"
#include "voiceqas/metrics.hpp"
#include "voiceqas/stt/client.hpp"

#include "voiceqas/ops/config.hpp"

namespace voiceqas {

struct ServerConfig {
    std::string rest_addr = "0.0.0.0:8080";
    std::string grpc_addr = "0.0.0.0:50051";
    std::string ws_addr = "0.0.0.0:8081";
    std::string media_rtp_addr = "0.0.0.0:10000";
    std::string web_root = "web";
    std::string openapi_path = "openapi/voiceqas.yaml";
    std::string config_path;
};

struct AppConfig {
    ServerConfig server;
    AnalyzerConfig analyzer;
    audio::AudioProcessingConfig audio;
    audio::MediaRelayConfig media;
    stt::SttConfig stt;
    ops::OpsConfig ops;
};

AppConfig load_app_config(int argc, char** argv);
AppConfig load_app_config_from_file(const std::string& path);

[[deprecated("use load_app_config() and inject config")]]
AnalyzerConfig& global_analyzer_config();
[[deprecated("use load_app_config() and inject config")]]
audio::AudioProcessingConfig& global_audio_config();
[[deprecated("use load_app_config() and inject config")]]
audio::MediaRelayConfig& global_media_config();
[[deprecated("use load_app_config() and inject config")]]
stt::SttConfig& global_stt_config();
[[deprecated("use load_app_config() and inject config")]]
ops::OpsConfig& global_ops_config();

}  // namespace voiceqas
