#include "voiceqas/config.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

#include <yaml-cpp/yaml.h>

#include "voiceqas/ops/config.hpp"

namespace voiceqas {

namespace {

void apply_env_overrides(ServerConfig& server, AnalyzerConfig& analyzer, audio::AudioProcessingConfig& audio) {
    if (const char* v = std::getenv("VOICEQAS_REST_ADDR")) {
        server.rest_addr = v;
    }
    if (const char* v = std::getenv("VOICEQAS_GRPC_ADDR")) {
        server.grpc_addr = v;
    }
    if (const char* v = std::getenv("VOICEQAS_WS_ADDR")) {
        server.ws_addr = v;
    }
    if (const char* v = std::getenv("VOICEQAS_MEDIA_RTP_ADDR")) {
        server.media_rtp_addr = v;
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
    if (const char* v = std::getenv("VOICEQAS_AUDIO_NORMALIZE")) {
        audio.normalize_enabled = std::string(v) != "0" && std::string(v) != "false";
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
    if (const char* v = std::getenv("VOICEQAS_STT_PROVIDER")) {
        stt.provider = v;
    }
    if (const char* v = std::getenv("VOICEQAS_STT_VAD_ENABLED")) {
        stt.vad.enabled = std::string(v) != "0" && std::string(v) != "false";
    }
    if (const char* v = std::getenv("VOICEQAS_STT_VAD_MODEL")) {
        const std::string value = v;
        if (value.find('/') != std::string::npos || value.find('\\') != std::string::npos ||
            value.find('.') != std::string::npos) {
            stt.vad.model_path = value;
        } else {
            stt.vad.model = value;
        }
    }
    if (const char* v = std::getenv("VOICEQAS_STT_VAD_THRESHOLD")) {
        stt.vad.threshold = static_cast<float>(std::stod(v));
    }
    if (const char* v = std::getenv("VOICEQAS_STT_VAD_APPLY_BEFORE_STT")) {
        stt.vad.apply_before_stt = std::string(v) != "0" && std::string(v) != "false";
    }
    if (const char* v = std::getenv("VOICEQAS_STT_REQUIRE_STT_READY")) {
        stt.require_stt_ready = std::string(v) != "0" && std::string(v) != "false";
    }
}

void apply_media_env(audio::MediaRelayConfig& media) {
    if (const char* v = std::getenv("VOICEQAS_MEDIA_PREFERRED_INGRESS_CODEC")) {
        media.preferred_ingress_codec = v;
    }
    if (const char* v = std::getenv("VOICEQAS_MEDIA_INGRESS_AUTODETECT")) {
        media.ingress_autodetect = std::string(v) != "0" && std::string(v) != "false";
    }
}

void apply_ops_env(ops::OpsConfig& ops) {
    if (const char* v = std::getenv("VOICEQAS_OPS_WEBHOOK_URL")) {
        ops.webhook_url = v;
    }
    if (const char* v = std::getenv("VOICEQAS_OPS_STORAGE_PATH")) {
        ops.storage_path = v;
    }
    if (const char* v = std::getenv("VOICEQAS_OPS_ADMIN_TOKEN")) {
        ops.admin_token = v;
    }
    if (const char* v = std::getenv("VOICEQAS_OPS_READ_TOKEN")) {
        ops.read_token = v;
    }
    if (const char* v = std::getenv("VOICEQAS_OPS_WRITE_TOKEN")) {
        ops.write_token = v;
    }
    if (const char* v = std::getenv("VOICEQAS_OPS_TOKEN")) {
        ops.admin_token = v;
    }
}

void load_yaml_file(
    const std::string& path,
    ServerConfig& server,
    AnalyzerConfig& analyzer,
    audio::AudioProcessingConfig& audio,
    audio::MediaRelayConfig& media,
    stt::SttConfig& stt_cfg,
    ops::OpsConfig& ops_cfg) {
    const YAML::Node root = YAML::LoadFile(path);
    if (root["server"]) {
        const auto s = root["server"];
        if (s["rest_addr"]) server.rest_addr = s["rest_addr"].as<std::string>();
        if (s["grpc_addr"]) server.grpc_addr = s["grpc_addr"].as<std::string>();
        if (s["ws_addr"]) server.ws_addr = s["ws_addr"].as<std::string>();
        if (s["media_rtp_addr"]) server.media_rtp_addr = s["media_rtp_addr"].as<std::string>();
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
    if (root["audio"]) {
        const auto a = root["audio"];
        if (a["normalize_enabled"]) audio.normalize_enabled = a["normalize_enabled"].as<bool>();
        if (a["agc_target_rms_dbfs"]) audio.agc_target_rms_dbfs = a["agc_target_rms_dbfs"].as<double>();
        if (a["agc_max_gain_db"]) audio.agc_max_gain_db = a["agc_max_gain_db"].as<double>();
        if (a["agc_attack_ms"]) audio.agc_attack_ms = a["agc_attack_ms"].as<double>();
        if (a["agc_release_ms"]) audio.agc_release_ms = a["agc_release_ms"].as<double>();
        if (a["limiter_ceiling_dbfs"]) audio.limiter_ceiling_dbfs = a["limiter_ceiling_dbfs"].as<double>();
    }
    if (root["media"]) {
        const auto m = root["media"];
        if (m["enabled"]) media.enabled = m["enabled"].as<bool>();
        if (m["rtp_addr"]) media.rtp_addr = m["rtp_addr"].as<std::string>();
        if (m["preferred_ingress_codec"]) {
            media.preferred_ingress_codec = m["preferred_ingress_codec"].as<std::string>();
        }
        if (m["ingress_autodetect"]) {
            media.ingress_autodetect = m["ingress_autodetect"].as<bool>();
        }
    }
    if (root["stt"]) {
        const auto s = root["stt"];
        if (s["enabled"]) stt_cfg.enabled = s["enabled"].as<bool>();
        if (s["language"]) stt_cfg.language = s["language"].as<std::string>();
        if (s["target_sample_rate"]) stt_cfg.target_sample_rate = s["target_sample_rate"].as<int>();
        if (s["default_model"]) stt_cfg.default_model = s["default_model"].as<std::string>();
        if (s["num_threads"]) stt_cfg.num_threads = s["num_threads"].as<int>();
        if (s["provider"]) stt_cfg.provider = s["provider"].as<std::string>();
        if (s["models_dir"]) stt_cfg.models_dir = s["models_dir"].as<std::string>();
        if (s["parakeet_dir"]) stt_cfg.parakeet_dir = s["parakeet_dir"].as<std::string>();
        if (s["whisper_dir"]) stt_cfg.whisper_dir = s["whisper_dir"].as<std::string>();
        if (s["whisper_callcenter_dir"]) {
            stt_cfg.whisper_callcenter_dir = s["whisper_callcenter_dir"].as<std::string>();
        }
        if (s["require_stt_ready"]) {
            stt_cfg.require_stt_ready = s["require_stt_ready"].as<bool>();
        }
        if (s["vad"]) {
            const auto v = s["vad"];
            if (v["enabled"]) stt_cfg.vad.enabled = v["enabled"].as<bool>();
            if (v["apply_before_stt"]) {
                stt_cfg.vad.apply_before_stt = v["apply_before_stt"].as<bool>();
            }
            if (v["model"]) stt_cfg.vad.model = v["model"].as<std::string>();
            if (v["model_path"]) stt_cfg.vad.model_path = v["model_path"].as<std::string>();
            if (v["threshold"]) stt_cfg.vad.threshold = v["threshold"].as<float>();
            if (v["min_speech_ms"]) {
                stt_cfg.vad.min_speech_duration = v["min_speech_ms"].as<float>() / 1000.0f;
            }
            if (v["min_silence_ms"]) {
                stt_cfg.vad.min_silence_duration = v["min_silence_ms"].as<float>() / 1000.0f;
            }
            if (v["num_threads"]) stt_cfg.vad.num_threads = v["num_threads"].as<int>();
            if (v["provider"]) stt_cfg.vad.provider = v["provider"].as<std::string>();
        }
    }
    if (root["ops"]) {
        const auto o = root["ops"];
        if (o["persistence_enabled"]) ops_cfg.persistence_enabled = o["persistence_enabled"].as<bool>();
        if (o["storage_path"]) ops_cfg.storage_path = o["storage_path"].as<std::string>();
        if (o["webhook_url"]) ops_cfg.webhook_url = o["webhook_url"].as<std::string>();
        if (o["alert_score_threshold"]) {
            ops_cfg.alert_score_threshold = o["alert_score_threshold"].as<double>();
        }
        if (o["stt_not_ready_alert_ms"]) {
            ops_cfg.stt_not_ready_alert_ms = o["stt_not_ready_alert_ms"].as<int>();
        }
        if (o["admin_token"]) ops_cfg.admin_token = o["admin_token"].as<std::string>();
        if (o["read_token"]) ops_cfg.read_token = o["read_token"].as<std::string>();
        if (o["write_token"]) ops_cfg.write_token = o["write_token"].as<std::string>();
        if (o["partial_stt_interval_ms"]) {
            ops_cfg.partial_stt_interval_ms = o["partial_stt_interval_ms"].as<int>();
        }
        if (o["partial_stt_window_ms"]) {
            ops_cfg.partial_stt_window_ms = o["partial_stt_window_ms"].as<int>();
        }
        if (o["partial_stt_min_buffer_ms"]) {
            ops_cfg.partial_stt_min_buffer_ms = o["partial_stt_min_buffer_ms"].as<int>();
        }
    }
}

}  // namespace

struct LoadedConfig {
    ServerConfig server;
    AnalyzerConfig analyzer;
    audio::AudioProcessingConfig audio;
    audio::MediaRelayConfig media;
    stt::SttConfig stt;
    ops::OpsConfig ops;
};

LoadedConfig g_config;

AppConfig load_app_config_from_file(const std::string& path) {
    AppConfig cfg;
    cfg.server.config_path = path;
    std::ifstream test(path);
    if (test.good()) {
        load_yaml_file(
            path,
            cfg.server,
            cfg.analyzer,
            cfg.audio,
            cfg.media,
            cfg.stt,
            cfg.ops);
    }
    apply_env_overrides(cfg.server, cfg.analyzer, cfg.audio);
    apply_stt_env(cfg.stt);
    apply_media_env(cfg.media);
    apply_ops_env(cfg.ops);
    if (!cfg.server.media_rtp_addr.empty()) {
        cfg.media.rtp_addr = cfg.server.media_rtp_addr;
    }
    g_config = LoadedConfig{
        cfg.server,
        cfg.analyzer,
        cfg.audio,
        cfg.media,
        cfg.stt,
        cfg.ops,
    };
    return cfg;
}

AppConfig load_app_config(int argc, char** argv) {
    AppConfig cfg;
    cfg.server.config_path = "config/voiceqas.example.yaml";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            cfg.server.config_path = argv[++i];
        }
    }

    std::ifstream test(cfg.server.config_path);
    if (test.good()) {
        try {
            load_yaml_file(
                cfg.server.config_path,
                cfg.server,
                cfg.analyzer,
                cfg.audio,
                cfg.media,
                cfg.stt,
                cfg.ops);
        } catch (const std::exception& e) {
            std::cerr << "warning: failed to load config: " << e.what() << '\n';
        }
    }

    apply_env_overrides(cfg.server, cfg.analyzer, cfg.audio);
    apply_stt_env(cfg.stt);
    apply_media_env(cfg.media);
    apply_ops_env(cfg.ops);
    if (!cfg.server.media_rtp_addr.empty()) {
        cfg.media.rtp_addr = cfg.server.media_rtp_addr;
    }
    g_config = LoadedConfig{
        cfg.server,
        cfg.analyzer,
        cfg.audio,
        cfg.media,
        cfg.stt,
        cfg.ops,
    };
    return cfg;
}

AnalyzerConfig& global_analyzer_config() {
    return g_config.analyzer;
}

audio::AudioProcessingConfig& global_audio_config() {
    return g_config.audio;
}

audio::MediaRelayConfig& global_media_config() {
    return g_config.media;
}

stt::SttConfig& global_stt_config() {
    return g_config.stt;
}

ops::OpsConfig& global_ops_config() {
    return g_config.ops;
}

}  // namespace voiceqas
