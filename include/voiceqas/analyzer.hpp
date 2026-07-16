#pragma once

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "voiceqas/metrics.hpp"
#include "voiceqas/ports/metrics_publisher.hpp"
#include "voiceqas/ports/pipeline_telemetry.hpp"
#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/stt_gate.hpp"
#include "voiceqas/audio/dsp/channel_strip.hpp"

namespace voiceqas {

class VoiceAnalyzer {
public:
    explicit VoiceAnalyzer(AnalyzerConfig config);

    const AnalyzerConfig& config() const { return config_; }

    std::optional<WindowMetrics> push_pcm(std::span<const int16_t> samples, int64_t timestamp_ms);
    BatchResult analyze_pcm_batch(std::span<const int16_t> samples, int sample_rate);
    void reset();

    void set_rtp_metrics(double packet_loss_pct, double jitter_ms);
    int64_t elapsed_ms() const { return elapsed_ms_; }

private:
    AnalyzerConfig config_;
    SttGate gate_;
    std::vector<int16_t> window_buffer_;
    std::vector<FrameMetrics> frame_buffer_;
    int64_t elapsed_ms_ = 0;
    int64_t window_start_ms_ = 0;
    double rtp_packet_loss_pct_ = 0.0;
    double rtp_jitter_ms_ = 0.0;

    FrameMetrics analyze_frame(std::span<const int16_t> frame);
    WindowMetrics aggregate_window();
    double compute_spectral_flatness(std::span<const int16_t> samples);
};

class VqaSessionManager {
public:
    VqaSessionManager(
        AnalyzerConfig default_config,
        audio::AudioProcessingConfig audio_config,
        std::shared_ptr<ports::IMetricsPublisher> metrics,
        std::shared_ptr<ports::IPipelineTelemetry> telemetry);

    VqaSessionManager(AnalyzerConfig default_config, audio::AudioProcessingConfig audio_config = {});

    const audio::AudioProcessingConfig& audio_config() const { return audio_config_; }

    std::optional<WindowMetrics> push_frame(
        const std::string& session_id,
        AudioFormat format,
        std::span<const uint8_t> payload,
        int64_t timestamp_ms);

    /**
     * @param apply_agc When false, PCM is assumed already normalized (e.g. shared
     *   RTP ingress AGC + RNNoise). When true (default), per-session AGC+enhancement run.
     */
    std::optional<WindowMetrics> push_pcm(
        const std::string& session_id,
        std::span<const int16_t> pcm,
        int64_t timestamp_ms,
        int sample_rate,
        bool apply_agc = true);

    BatchResult analyze_batch(
        AudioFormat format,
        std::span<const uint8_t> payload,
        int sample_rate,
        const std::optional<std::string>& telemetry_session_id = std::nullopt,
        const std::optional<audio::AudioProcessingConfig>& audio_override = std::nullopt);

    void remove_session(const std::string& session_id);

private:
    struct SessionState {
        std::optional<VoiceAnalyzer> analyzer;
        std::optional<rtp::RtpDepacketizer> rtp;
        audio::VoiceChannelStrip strip;
    };

    AnalyzerConfig default_config_;
    audio::AudioProcessingConfig audio_config_;
    std::shared_ptr<ports::IMetricsPublisher> metrics_;
    std::shared_ptr<ports::IPipelineTelemetry> telemetry_;
    std::unordered_map<std::string, SessionState> sessions_;
    mutable std::shared_mutex mutex_;
};

}  // namespace voiceqas
