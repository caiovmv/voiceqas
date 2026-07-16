#include "voiceqas/stt/session_manager.hpp"

#include <chrono>
#include <shared_mutex>

#include "voiceqas/audio/decoder.hpp"
#include "voiceqas/metrics.hpp"
#include "voiceqas/stt/audio_prepare.hpp"

namespace voiceqas::stt {

SttSessionManager::SttSessionManager(
    std::shared_ptr<SttEngine> engine,
    SttConfig config,
    ops::OpsConfig ops_config,
    std::shared_ptr<ports::IMetricsPublisher> metrics,
    std::shared_ptr<ports::IPipelineTelemetry> telemetry,
    const audio::AudioProcessingConfig& audio_config,
    int target_sample_rate)
    : engine_(std::move(engine)),
      config_(std::move(config)),
      ops_config_(std::move(ops_config)),
      audio_config_(audio_config),
      metrics_(std::move(metrics)),
      telemetry_(std::move(telemetry)),
      target_sample_rate_(target_sample_rate) {
    default_options_.language = config_.language;
    default_options_.model = parse_model_choice(config_.default_model, SttModelChoice::Auto);
}

SttSessionManager::SttSessionManager(
    std::shared_ptr<SttEngine> engine,
    SttConfig config,
    int target_sample_rate)
    : SttSessionManager(
          std::move(engine),
          std::move(config),
          ops::OpsConfig{},
          ports::noop_metrics_publisher(),
          ports::noop_pipeline_telemetry(),
          audio::AudioProcessingConfig{},
          target_sample_rate) {}

void SttSessionManager::bind_session_options(const std::string& session_id, TranscribeOptions options) {
    std::lock_guard lock(mutex_);
    auto& session = sessions_[session_id];
    session.options = std::move(options);
}

void SttSessionManager::set_default_options(TranscribeOptions options) {
    default_options_ = std::move(options);
}

bool SttSessionManager::should_accept_audio_unlocked(
    const SessionState* session,
    const std::string& session_id) const {
    if (!config_.require_stt_ready) {
        return true;
    }
    if (session) {
        return session->stt_ready;
    }
    const auto it = stt_ready_.find(session_id);
    return it != stt_ready_.end() && it->second;
}

void SttSessionManager::update_stt_ready(const std::string& session_id, bool ready) {
    std::lock_guard lock(mutex_);
    stt_ready_[session_id] = ready;
    if (auto it = sessions_.find(session_id); it != sessions_.end()) {
        it->second.stt_ready = ready;
    }
}

bool SttSessionManager::reload_vad_model(const std::string& model_selector) {
    config_.vad.model = model_selector;
    return engine_->reload_vad(model_selector);
}

void SttSessionManager::emit_partials_for_all_sessions() {
    std::vector<std::string> session_ids;
    {
        std::shared_lock lock(mutex_);
        session_ids.reserve(sessions_.size());
        for (const auto& [session_id, _] : sessions_) {
            session_ids.push_back(session_id);
        }
    }
    for (const auto& session_id : session_ids) {
        emit_partial_if_due(session_id);
    }
}

void SttSessionManager::ensure_session_bound(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    if (!bound_sessions_.insert(session_id).second) {
        return;
    }
    stt::TranscribeOptions opts = default_options_;
    // Focus-primary implies Silero turn filter; else optional silence strip.
    opts.focus_primary = config_.diarization.enabled && config_.diarization.focus_primary;
    opts.apply_vad = config_.vad.enabled && config_.vad.apply_before_stt && !opts.focus_primary.value_or(false);
    sessions_[session_id].options = opts;
}

void SttSessionManager::append_pcm(
    const std::string& session_id,
    std::span<const int16_t> pcm,
    int sample_rate,
    double shared_agc_ms,
    double shared_enhancement_ms) {
    {
        std::lock_guard lock(mutex_);
        const auto it = sessions_.find(session_id);
        if (!should_accept_audio_unlocked(it == sessions_.end() ? nullptr : &it->second, session_id)) {
            telemetry_->record_stt_gate_drop(session_id, pcm.size() * sizeof(int16_t));
            return;
        }
    }

    const auto resample_started = std::chrono::steady_clock::now();
    auto resampled = resample_pcm16(pcm, sample_rate, target_sample_rate_);
    const double resample_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - resample_started)
                                   .count();

    ports::SttPrepareTimings timings{
        .decode_ms = 0.0,
        .agc_ms = shared_agc_ms,
        .enhancement_ms = shared_enhancement_ms,
        .resample_ms = resample_ms,
        .pcm_bytes = resampled.size() * sizeof(int16_t),
        .payload_bytes = pcm.size() * sizeof(int16_t),
    };
    telemetry_->record_stt_prepare(session_id, timings);

    std::lock_guard lock(mutex_);
    auto& session = sessions_[session_id];
    session.sample_rate = target_sample_rate_;
    session.pcm.insert(session.pcm.end(), resampled.begin(), resampled.end());
    telemetry_->record_stt_buffer(session_id, session.sample_rate, session.pcm.size());
    if (session.options.language.empty()) {
        session.options = default_options_;
    }
}

void SttSessionManager::append_chunk(
    const std::string& session_id,
    AudioFormat format,
    std::span<const uint8_t> payload,
    int sample_rate) {
    rtp::RtpDepacketizer* depacketizer_ptr = nullptr;
    {
        std::lock_guard lock(mutex_);
        const auto it = sessions_.find(session_id);
        if (!should_accept_audio_unlocked(it == sessions_.end() ? nullptr : &it->second, session_id)) {
            telemetry_->record_stt_gate_drop(session_id, payload.size());
            return;
        }
        if (audio::is_rtp_format(format)) {
            auto& session = sessions_[session_id];
            if (!session.depacketizer || session.rtp_format != format) {
                session.rtp_format = format;
                session.depacketizer.emplace(rtp_clock_rate_for_format(format));
            }
            depacketizer_ptr = &(*session.depacketizer);
        }
    }
    auto prepared = prepare_audio_for_stt(
        format, payload, sample_rate, target_sample_rate_, audio_config_, depacketizer_ptr);
    telemetry_->record_rtp_ingress(session_id, payload.size(), 0.0, 0.0);
    ports::SttPrepareTimings timings{
        .decode_ms = prepared.decode_ms,
        .agc_ms = prepared.agc_ms,
        .enhancement_ms = prepared.enhancement_ms,
        .resample_ms = prepared.resample_ms,
        .pcm_bytes = prepared.pcm.size() * sizeof(int16_t),
        .payload_bytes = prepared.payload_bytes,
    };
    telemetry_->record_stt_prepare(session_id, timings);
    std::lock_guard lock(mutex_);
    auto& session = sessions_[session_id];
    session.sample_rate = prepared.sample_rate;
    session.pcm.insert(session.pcm.end(), prepared.pcm.begin(), prepared.pcm.end());
    telemetry_->record_stt_buffer(session_id, session.sample_rate, session.pcm.size());
    if (session.options.language.empty()) {
        session.options = default_options_;
    }
}

std::optional<TranscriptResult> SttSessionManager::compute_partial_unlocked(
    const std::string& session_id,
    SessionState& session) {
    const auto& cfg = ops_config_;
    const auto min_samples =
        static_cast<size_t>(session.sample_rate) * cfg.partial_stt_min_buffer_ms / 1000;
    if (session.pcm.size() < min_samples) {
        return std::nullopt;
    }

    const auto now = std::chrono::steady_clock::now();
    if (session.last_partial_at.time_since_epoch().count() != 0) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - session.last_partial_at)
                                 .count();
        if (elapsed < cfg.partial_stt_interval_ms) {
            return std::nullopt;
        }
    }
    if (session.pcm.size() == session.last_partial_pcm_size) {
        return std::nullopt;
    }

    const auto window_samples =
        static_cast<size_t>(session.sample_rate) * cfg.partial_stt_window_ms / 1000;
    const auto start = session.pcm.size() > window_samples ? session.pcm.end() - window_samples : session.pcm.begin();
    std::vector<int16_t> tail(start, session.pcm.end());

    auto options = session.options.language.empty() ? default_options_ : session.options;
    options.telemetry_session_id = session_id;
    return engine_->transcribe_pcm16(tail, session.sample_rate, options);
}

std::optional<TranscriptResult> SttSessionManager::emit_partial_if_due(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    if (auto result = compute_partial_unlocked(session_id, it->second)) {
        metrics_->publish_stt(session_id, *result, true);
        it->second.last_partial_at = std::chrono::steady_clock::now();
        it->second.last_partial_pcm_size = it->second.pcm.size();
        return result;
    }
    return std::nullopt;
}

TranscriptResult SttSessionManager::flush(const std::string& session_id, TranscribeOptions options) {
    std::vector<int16_t> pcm;
    int rate = target_sample_rate_;
    TranscribeOptions session_options = default_options_;
    {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            TranscriptResult empty;
            empty.error = "unknown session";
            return empty;
        }
        pcm = std::move(it->second.pcm);
        rate = it->second.sample_rate;
        session_options = it->second.options;
        sessions_.erase(it);
    }

    if (pcm.empty()) {
        TranscriptResult empty;
        empty.ok = false;
        empty.error = "no audio buffered for flush";
        return empty;
    }

    if (options.language.empty() && session_options.language.empty()) {
        options.language = default_options_.language;
    } else if (options.language.empty()) {
        options.language = session_options.language;
    }
    if (options.model == SttModelChoice::Auto && session_options.model != SttModelChoice::Auto) {
        options.model = session_options.model;
    }
    if (!options.provider && session_options.provider) {
        options.provider = session_options.provider;
    }
    options.telemetry_session_id = session_id;

    return engine_->transcribe_pcm16(pcm, rate, options);
}

TranscriptResult SttSessionManager::flush_and_publish(
    const std::string& session_id,
    TranscribeOptions options,
    bool partial) {
    auto result = flush(session_id, std::move(options));
    metrics_->publish_stt(session_id, result, partial);
    if (!partial) {
        telemetry_->finish_session(session_id, "flush");
    }
    return result;
}

void SttSessionManager::remove_session(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    sessions_.erase(session_id);
    stt_ready_.erase(session_id);
    bound_sessions_.erase(session_id);
    telemetry_->remove_session(session_id);
}

TranscriptResult SttSessionManager::transcribe_batch(
    AudioFormat format,
    std::span<const uint8_t> payload,
    int sample_rate,
    TranscribeOptions options) {
    if (!options.telemetry_session_id) {
        options.telemetry_session_id = "stt-batch-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }
    const std::string& session_id = *options.telemetry_session_id;

    telemetry_->set_session_codec(session_id, audio_format_to_string(format));
    telemetry_->record_rtp_ingress(session_id, payload.size(), 0.0, 0.0);

    audio::AudioProcessingConfig prep_cfg = audio_config_;
    if (options.strip.has_value()) {
        prep_cfg.strip = *options.strip;
        prep_cfg.sync_legacy_from_strip();
    }
    if (options.normalize_enabled.has_value()) {
        prep_cfg.normalize_enabled = *options.normalize_enabled;
        prep_cfg.strip.agc.enabled = *options.normalize_enabled;
    }
    if (options.enhancement_enabled.has_value()) {
        prep_cfg.enhancement.enabled = *options.enhancement_enabled;
        prep_cfg.strip.nr.enabled = *options.enhancement_enabled;
    }
    prep_cfg.sync_legacy_from_strip();

    const auto prepared = prepare_audio_for_stt(
        format, payload, sample_rate, target_sample_rate_, prep_cfg);
    ports::SttPrepareTimings timings{
        .decode_ms = prepared.decode_ms,
        .agc_ms = prepared.agc_ms,
        .enhancement_ms = prepared.enhancement_ms,
        .resample_ms = prepared.resample_ms,
        .pcm_bytes = prepared.pcm.size() * sizeof(int16_t),
        .payload_bytes = prepared.payload_bytes,
    };
    telemetry_->record_stt_prepare(session_id, timings);
    telemetry_->record_stt_buffer(session_id, prepared.sample_rate, prepared.pcm.size());

    if (options.language.empty()) {
        options.language = default_options_.language;
    }
    auto result = engine_->transcribe_pcm16(prepared.pcm, prepared.sample_rate, options);
    metrics_->publish_stt(session_id, result, false);
    telemetry_->finish_session(session_id, "batch");
    return result;
}

}  // namespace voiceqas::stt
