#include "voiceqas/stt/session_manager.hpp"

#include "voiceqas/stt/audio_prepare.hpp"

namespace voiceqas::stt {

SttSessionManager::SttSessionManager(
    std::shared_ptr<SttEngine> engine,
    SttConfig config,
    int target_sample_rate)
    : engine_(std::move(engine)),
      config_(std::move(config)),
      target_sample_rate_(target_sample_rate) {
    default_options_.language = config_.language;
    default_options_.model = parse_model_choice(config_.default_model, SttModelChoice::Auto);
}

void SttSessionManager::bind_session_options(const std::string& session_id, TranscribeOptions options) {
    std::lock_guard lock(mutex_);
    auto& session = sessions_[session_id];
    session.options = std::move(options);
}

void SttSessionManager::set_default_options(TranscribeOptions options) {
    default_options_ = std::move(options);
}

void SttSessionManager::append_chunk(
    const std::string& session_id,
    AudioFormat format,
    std::span<const uint8_t> payload,
    int sample_rate) {
    auto prepared = prepare_audio_for_stt(format, payload, sample_rate, target_sample_rate_);
    std::lock_guard lock(mutex_);
    auto& session = sessions_[session_id];
    session.sample_rate = prepared.sample_rate;
    session.pcm.insert(session.pcm.end(), prepared.pcm.begin(), prepared.pcm.end());
    if (session.options.language.empty()) {
        session.options = default_options_;
    }
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

    if (options.language.empty() && session_options.language.empty()) {
        options.language = default_options_.language;
    } else if (options.language.empty()) {
        options.language = session_options.language;
    }
    if (options.model == SttModelChoice::Auto && session_options.model != SttModelChoice::Auto) {
        options.model = session_options.model;
    }

    return engine_->transcribe_pcm16(pcm, rate, options);
}

void SttSessionManager::remove_session(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    sessions_.erase(session_id);
}

TranscriptResult SttSessionManager::transcribe_batch(
    AudioFormat format,
    std::span<const uint8_t> payload,
    int sample_rate,
    TranscribeOptions options) {
    const auto prepared = prepare_audio_for_stt(format, payload, sample_rate, target_sample_rate_);
    if (options.language.empty()) {
        options.language = default_options_.language;
    }
    return engine_->transcribe_pcm16(prepared.pcm, prepared.sample_rate, options);
}

}  // namespace voiceqas::stt
