#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sherpa-onnx/c-api/c-api.h"

namespace voiceqas::stt {

struct VadConfig {
    bool enabled = true;
    /// When true, Silero VAD strips non-speech before offline ASR (media relay). Off for batch/tester.
    bool apply_before_stt = false;
    std::string model = "auto";
    std::string model_path;
    float threshold = 0.45f;
    float min_speech_duration = 0.15f;
    float min_silence_duration = 0.30f;
    int num_threads = 1;
    /// ONNX Runtime provider for VAD. Empty = inherit stt.provider.
    std::string provider;
};

class SileroVad {
public:
    explicit SileroVad(VadConfig config);
    ~SileroVad();

    SileroVad(const SileroVad&) = delete;
    SileroVad& operator=(const SileroVad&) = delete;

    bool ready() const { return detector_ != nullptr; }

    std::vector<int16_t> extract_speech(std::span<const int16_t> pcm, int sample_rate) const;

private:
    VadConfig config_;
    const SherpaOnnxVoiceActivityDetector* detector_ = nullptr;
};

}  // namespace voiceqas::stt
