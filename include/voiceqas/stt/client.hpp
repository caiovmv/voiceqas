#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "voiceqas/audio/config.hpp"
#include "voiceqas/stt/diarization.hpp"
#include "voiceqas/stt/vad.hpp"
#include "voiceqas/ports/pipeline_telemetry.hpp"

namespace voiceqas::stt {

struct TranscriptSegment {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::string text;
    int speaker_id = -1;
};

struct TranscriptResult {
    std::string text;
    std::string model;
    std::string language;
    int64_t duration_ms = 0;
    int64_t processing_ms = 0;
    std::vector<TranscriptSegment> segments;
    std::vector<SpeechTurn> diarization_turns;
    int primary_speaker = -1;
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
    /// When set, overrides vad.apply_before_stt for this request.
    std::optional<bool> apply_vad;
    /// When set, overrides diarization.enabled for this request.
    std::optional<bool> diarization_enabled;
    /// When set, overrides diarization.focus_primary for this request.
    std::optional<bool> focus_primary;
    /// When set, overrides audio.normalize_enabled for prepare path.
    std::optional<bool> normalize_enabled;
    /// When set, overrides audio.enhancement.enabled for prepare path.
    std::optional<bool> enhancement_enabled;
    /// When set, merges into audio.strip for prepare path (Analysis Lab knobs).
    std::optional<audio::ChannelStripConfig> strip;
    /// When set, pipeline telemetry is recorded for this session.
    std::optional<std::string> telemetry_session_id;
    /// When set, overrides stt.provider for this request (cpu|cuda).
    std::optional<std::string> provider;
};

struct SttConfig {
    bool enabled = true;
    std::string language = "pt";
    int target_sample_rate = 16000;
    std::string default_model = "auto";
    int num_threads = 2;
    /// ONNX Runtime provider: cpu or cuda (cuda requires VOICEQAS_STT_CUDA build).
    std::string provider = "cpu";
    std::string models_dir = "/models/stt";
    std::string parakeet_dir;
    std::string whisper_dir;
    std::string whisper_callcenter_dir;
    /// When true, streaming STT drops audio until VQA reports stt_ready (hysteresis).
    bool require_stt_ready = true;
    VadConfig vad;
    DiarizationConfig diarization;
};

struct SttReadyStatus {
    bool parakeet_ready = false;
    bool whisper_ready = false;
    std::string parakeet_model;
    std::string whisper_model;
    bool vad_ready = false;
    std::string vad_model;
    std::string vad_model_id;
    std::string provider;
    std::vector<std::string> providers_available;
    std::vector<std::string> loaded_providers;
    bool cuda_compiled = false;

    bool ready() const { return parakeet_ready || whisper_ready; }
};

SttModelChoice parse_model_choice(const std::string& value, SttModelChoice fallback = SttModelChoice::Auto);

class SttEngine {
public:
    explicit SttEngine(
        SttConfig config,
        std::shared_ptr<ports::IPipelineTelemetry> telemetry = ports::noop_pipeline_telemetry());
    ~SttEngine();

    SttEngine(const SttEngine&) = delete;
    SttEngine& operator=(const SttEngine&) = delete;

    SttReadyStatus ready_status() const;
    bool ready() const { return ready_status().ready(); }

    TranscriptResult transcribe_pcm16(
        std::span<const int16_t> pcm,
        int sample_rate,
        TranscribeOptions options = {});

    bool reload_vad(const std::string& model_selector);
    std::string active_vad_model_id() const;

private:
    struct Impl;
    struct RecognizerBundle;
    RecognizerBundle& ensure_provider_locked(const std::string& provider) const;
    void load_provider_locked(const std::string& provider) const;
    std::unique_ptr<Impl> impl_;
    SttConfig config_;
    std::shared_ptr<ports::IPipelineTelemetry> telemetry_;
    mutable std::mutex mutex_;
};

}  // namespace voiceqas::stt
