#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace voiceqas::stt {

struct TranscriptSegment {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::string text;
};

struct TranscriptResult {
    std::string text;
    std::string model;
    std::string language;
    int64_t duration_ms = 0;
    int64_t processing_ms = 0;
    std::vector<TranscriptSegment> segments;
    bool ok = false;
    std::string error;
};

enum class SttModelChoice {
    Parakeet,
    Whisper,
    Auto,
};

struct TranscribeOptions {
    SttModelChoice model = SttModelChoice::Auto;
    std::string language;
};

struct SttConfig {
    bool enabled = true;
    std::string language = "pt";
    int target_sample_rate = 16000;
    std::string default_model = "auto";
    int num_threads = 2;
    std::string models_dir = "/models/stt";
    std::string parakeet_dir;
    std::string whisper_dir;
    std::string whisper_callcenter_dir;
};

struct SttReadyStatus {
    bool parakeet_ready = false;
    bool whisper_ready = false;
    std::string parakeet_model;
    std::string whisper_model;

    bool ready() const { return parakeet_ready || whisper_ready; }
};

SttModelChoice parse_model_choice(const std::string& value, SttModelChoice fallback = SttModelChoice::Auto);

class SttEngine {
public:
    explicit SttEngine(SttConfig config);
    ~SttEngine();

    SttEngine(const SttEngine&) = delete;
    SttEngine& operator=(const SttEngine&) = delete;

    SttReadyStatus ready_status() const;
    bool ready() const { return ready_status().ready(); }

    TranscriptResult transcribe_pcm16(
        std::span<const int16_t> pcm,
        int sample_rate,
        TranscribeOptions options = {}) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SttConfig config_;
    mutable std::mutex mutex_;
};

}  // namespace voiceqas::stt
