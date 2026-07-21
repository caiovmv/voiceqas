#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sherpa-onnx/c-api/c-api.h"

#include "voiceqas/stt/diarization.hpp"

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

    /** Speech turns at 16 kHz timeline (start/end ms relative to resampled buffer). */
    std::vector<SpeechTurn> detect_turns(std::span<const int16_t> pcm, int sample_rate) const;

    /**
     * Turn diarization + optional primary filter. Returns primary PCM at 16 kHz
     * (or all speech if focus_primary is false).
     */
    DiarizationResult diarize(
        std::span<const int16_t> pcm,
        int sample_rate,
        const DiarizationConfig& config) const;

private:
    VadConfig config_;
    const SherpaOnnxVoiceActivityDetector* detector_ = nullptr;
    mutable std::recursive_mutex mutex_;
};

}  // namespace voiceqas::stt
