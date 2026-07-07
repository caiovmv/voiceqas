#include <gtest/gtest.h>

#include <fstream>

#include "voiceqas/stt/vad_model.hpp"

#include <filesystem>

namespace fs = std::filesystem;

TEST(VadModelResolve, AutoPicksK2FsaFirst) {
    const auto dir = fs::temp_directory_path() / "voiceqas_vad_test";
    fs::create_directories(dir);
    const auto k2 = dir / "silero_vad.onnx";
    const auto v5 = dir / "silero_vad_v5.onnx";
    std::ofstream(k2).put('x');
    std::ofstream(v5).put('y');

    const auto resolved = voiceqas::stt::resolve_vad_model(dir.string(), "auto", {});
    EXPECT_EQ(resolved.id, "k2fsa");
    EXPECT_TRUE(fs::exists(resolved.path));

    fs::remove(k2);
    const auto fallback = voiceqas::stt::resolve_vad_model(dir.string(), "auto", {});
    EXPECT_EQ(fallback.id, "v5");

    fs::remove_all(dir);
}

TEST(VadModelResolve, ExplicitSelector) {
    const auto dir = fs::temp_directory_path() / "voiceqas_vad_explicit";
    fs::create_directories(dir);
    const auto int8 = dir / "silero_vad.int8.onnx";
    std::ofstream(int8).put('z');

    const auto resolved = voiceqas::stt::resolve_vad_model(dir.string(), "k2fsa-int8", {});
    EXPECT_EQ(resolved.id, "k2fsa-int8");
    fs::remove_all(dir);
}
