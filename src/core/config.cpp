#include "voiceqas/config.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

#include <yaml-cpp/yaml.h>

namespace voiceqas {

namespace {

void apply_env_overrides(ServerConfig& server, AnalyzerConfig& analyzer) {
    if (const char* v = std::getenv("VOICEQAS_REST_ADDR")) {
        server.rest_addr = v;
    }
    if (const char* v = std::getenv("VOICEQAS_GRPC_ADDR")) {
        server.grpc_addr = v;
    }
    if (const char* v = std::getenv("VOICEQAS_WS_ADDR")) {
        server.ws_addr = v;
    }
    if (const char* v = std::getenv("VOICEQAS_WEB_ROOT")) {
        server.web_root = v;
    }
    if (const char* v = std::getenv("VOICEQAS_OPENAPI_PATH")) {
        server.openapi_path = v;
    }
    if (const char* v = std::getenv("VOICEQAS_STT_THRESHOLD")) {
        analyzer.stt_ready_threshold = std::stod(v);
    }
}

void apply_stt_env(stt::SttConfig& stt) {
    if (const char* v = std::getenv("VOICEQAS_STT_ENABLED")) {
        stt.enabled = std::string(v) != "0" && std::string(v) != "false";
    }
    if (const char* v = std::getenv("VOICEQAS_STT_LANGUAGE")) {
        stt.language = v;
    }
    if (const char* v = std::getenv("VOICEQAS_STT_DEFAULT_MODEL")) {
        stt.default_model = v;
    }
    if (const char* v = std::getenv("VOICEQAS_STT_MODELS_DIR")) {
        stt.models_dir = v;
    }
    if (const char* v = std::getenv("VOICEQAS_STT_PARAKEET_DIR")) {
        stt.parakeet_dir = v;
    }
    if (const char* v = std::getenv("VOICEQAS_STT_WHISPER_DIR")) {
        stt.whisper_dir = v;
    }
    if (const char* v = std::getenv("VOICEQAS_STT_WHISPER_CALLCENTER_DIR")) {
        stt.whisper_callcenter_dir = v;
    }
    if (const char* v = std::getenv("VOICEQAS_STT_THREADS")) {
        stt.num_threads = std::stoi(v);
    }
}

void load_yaml_file(
    const std::string& path,
    ServerConfig& server,
    AnalyzerConfig& analyzer,
    stt::SttConfig& stt_cfg) {
    const YAML::Node root = YAML::LoadFile(path);
    if (root["server"]) {
        const auto s = root["server"];
        if (s["rest_addr"]) server.rest_addr = s["rest_addr"].as<std::string>();
        if (s["grpc_addr"]) server.grpc_addr = s["grpc_addr"].as<std::string>();
        if (s["ws_addr"]) server.ws_addr = s["ws_addr"].as<std::string>();
        if (s["web_root"]) server.web_root = s["web_root"].as<std::string>();
        if (s["openapi_path"]) server.openapi_path = s["openapi_path"].as<std::string>();
    }
    if (root["analyzer"]) {
        const auto a = root["analyzer"];
        if (a["frame_ms"]) analyzer.frame_ms = a["frame_ms"].as<int>();
        if (a["window_ms"]) analyzer.window_ms = a["window_ms"].as<int>();
        if (a["stt_ready_threshold"]) analyzer.stt_ready_threshold = a["stt_ready_threshold"].as<double>();
        if (a["min_snr_db"]) analyzer.min_snr_db = a["min_snr_db"].as<double>();
        if (a["max_clipping_ratio"]) analyzer.max_clipping_ratio = a["max_clipping_ratio"].as<double>();
        if (a["max_silence_ratio"]) analyzer.max_silence_ratio = a["max_silence_ratio"].as<double>();
    }
    if (root["stt"]) {
        const auto s = root["stt"];
        if (s["enabled"]) stt_cfg.enabled = s["enabled"].as<bool>();
        if (s["language"]) stt_cfg.language = s["language"].as<std::string>();
        if (s["target_sample_rate"]) stt_cfg.target_sample_rate = s["target_sample_rate"].as<int>();
        if (s["default_model"]) stt_cfg.default_model = s["default_model"].as<std::string>();
        if (s["num_threads"]) stt_cfg.num_threads = s["num_threads"].as<int>();
        if (s["models_dir"]) stt_cfg.models_dir = s["models_dir"].as<std::string>();
        if (s["parakeet_dir"]) stt_cfg.parakeet_dir = s["parakeet_dir"].as<std::string>();
        if (s["whisper_dir"]) stt_cfg.whisper_dir = s["whisper_dir"].as<std::string>();
        if (s["whisper_callcenter_dir"]) {
            stt_cfg.whisper_callcenter_dir = s["whisper_callcenter_dir"].as<std::string>();
        }
    }
}

}  // namespace

struct LoadedConfig {
    ServerConfig server;
    AnalyzerConfig analyzer;
    stt::SttConfig stt;
};

LoadedConfig g_config;

ServerConfig load_config(int argc, char** argv) {
    g_config.server.config_path = "config/voiceqas.example.yaml";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            g_config.server.config_path = argv[++i];
        }
    }

    std::ifstream test(g_config.server.config_path);
    if (test.good()) {
        try {
            load_yaml_file(g_config.server.config_path, g_config.server, g_config.analyzer, g_config.stt);
        } catch (const std::exception& e) {
            std::cerr << "warning: failed to load config: " << e.what() << '\n';
        }
    }

    apply_env_overrides(g_config.server, g_config.analyzer);
    apply_stt_env(g_config.stt);
    return g_config.server;
}

AnalyzerConfig& global_analyzer_config() {
    return g_config.analyzer;
}

stt::SttConfig& global_stt_config() {
    return g_config.stt;
}

}  // namespace voiceqas
