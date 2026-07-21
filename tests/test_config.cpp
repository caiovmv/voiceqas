#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "voiceqas/config.hpp"

namespace voiceqas {
namespace {

std::string write_temp_yaml(const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / "voiceqas_test_config.yaml";
    std::ofstream out(path);
    out << content;
    return path.string();
}

}  // namespace

TEST(ConfigTest, LoadsAnalyzerThresholdFromYaml) {
    const auto path = write_temp_yaml(R"(
analyzer:
  stt_ready_threshold: 72.5
  window_ms: 600
audio:
  normalize_enabled: false
)");
    const auto cfg = load_app_config_from_file(path);
    EXPECT_DOUBLE_EQ(cfg.analyzer.stt_ready_threshold, 72.5);
    EXPECT_EQ(cfg.analyzer.window_ms, 600);
    EXPECT_FALSE(cfg.audio.normalize_enabled);
}

TEST(ConfigTest, DefaultValuesWhenFileMissing) {
    const auto cfg = load_app_config_from_file("/nonexistent/voiceqas_missing.yaml");
    EXPECT_EQ(cfg.server.rest_addr, "0.0.0.0:8080");
    EXPECT_TRUE(cfg.audio.normalize_enabled);
}

}  // namespace voiceqas
