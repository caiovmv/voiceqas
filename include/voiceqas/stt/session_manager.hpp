#pragma once

#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "voiceqas/metrics.hpp"
#include "voiceqas/stt/client.hpp"

namespace voiceqas::stt {

class SttSessionManager {
public:
    explicit SttSessionManager(
        std::shared_ptr<SttEngine> engine,
        SttConfig config,
        int target_sample_rate = 16000);

    void append_chunk(
        const std::string& session_id,
        AudioFormat format,
        std::span<const uint8_t> payload,
        int sample_rate);

    TranscriptResult flush(const std::string& session_id, TranscribeOptions options = {});
    void remove_session(const std::string& session_id);

    TranscriptResult transcribe_batch(
        AudioFormat format,
        std::span<const uint8_t> payload,
        int sample_rate,
        TranscribeOptions options = {});

    void bind_session_options(const std::string& session_id, TranscribeOptions options);

    void set_default_options(TranscribeOptions options);
    const SttEngine& engine() const { return *engine_; }
    const SttConfig& config() const { return config_; }

private:
    struct SessionState {
        std::vector<int16_t> pcm;
        int sample_rate = 16000;
        TranscribeOptions options;
    };

    std::shared_ptr<SttEngine> engine_;
    SttConfig config_;
    TranscribeOptions default_options_;
    int target_sample_rate_;
    std::unordered_map<std::string, SessionState> sessions_;
    mutable std::mutex mutex_;
};

}  // namespace voiceqas::stt
