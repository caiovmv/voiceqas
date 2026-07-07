#pragma once

#include <chrono>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "voiceqas/audio/config.hpp"
#include "voiceqas/metrics.hpp"
#include "voiceqas/ports/metrics_publisher.hpp"
#include "voiceqas/ports/pipeline_telemetry.hpp"
#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/ops/config.hpp"
#include "voiceqas/stt/client.hpp"

namespace voiceqas::stt {

class SttSessionManager {
public:
    SttSessionManager(
        std::shared_ptr<SttEngine> engine,
        SttConfig config,
        ops::OpsConfig ops_config,
        std::shared_ptr<ports::IMetricsPublisher> metrics,
        std::shared_ptr<ports::IPipelineTelemetry> telemetry,
        const audio::AudioProcessingConfig& audio_config,
        int target_sample_rate = 16000);

    explicit SttSessionManager(
        std::shared_ptr<SttEngine> engine,
        SttConfig config,
        int target_sample_rate = 16000);

    void append_chunk(
        const std::string& session_id,
        AudioFormat format,
        std::span<const uint8_t> payload,
        int sample_rate);

    void append_pcm(
        const std::string& session_id,
        std::span<const int16_t> pcm,
        int sample_rate);

    void ensure_session_bound(const std::string& session_id);
    void emit_partials_for_all_sessions();

    void update_stt_ready(const std::string& session_id, bool ready);
    bool reload_vad_model(const std::string& model_selector);

    TranscriptResult flush(const std::string& session_id, TranscribeOptions options = {});
    TranscriptResult flush_and_publish(
        const std::string& session_id,
        TranscribeOptions options = {},
        bool partial = false);
    std::optional<TranscriptResult> emit_partial_if_due(const std::string& session_id);
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
        std::chrono::steady_clock::time_point last_partial_at{};
        size_t last_partial_pcm_size = 0;
        bool stt_ready = false;
        std::optional<AudioFormat> rtp_format;
        std::optional<rtp::RtpDepacketizer> depacketizer;
    };

    bool should_accept_audio_unlocked(const SessionState* session, const std::string& session_id) const;

    std::optional<TranscriptResult> compute_partial_unlocked(
        const std::string& session_id,
        SessionState& session);

    std::shared_ptr<SttEngine> engine_;
    SttConfig config_;
    ops::OpsConfig ops_config_;
    audio::AudioProcessingConfig audio_config_;
    std::shared_ptr<ports::IMetricsPublisher> metrics_;
    std::shared_ptr<ports::IPipelineTelemetry> telemetry_;
    TranscribeOptions default_options_;
    int target_sample_rate_;
    std::unordered_map<std::string, SessionState> sessions_;
    std::unordered_map<std::string, bool> stt_ready_;
    std::unordered_set<std::string> bound_sessions_;
    mutable std::shared_mutex mutex_;
};

}  // namespace voiceqas::stt
