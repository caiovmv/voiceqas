#include "voiceqas/stt/vad.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#include "voiceqas/audio/resampler.hpp"

namespace voiceqas::stt {

namespace fs = std::filesystem;

SileroVad::SileroVad(VadConfig config) : config_(std::move(config)) {
    if (!config_.enabled || config_.model_path.empty() || !fs::exists(config_.model_path)) {
        return;
    }

    SherpaOnnxVadModelConfig vad_config{};
    vad_config.silero_vad.model = config_.model_path.c_str();
    vad_config.silero_vad.threshold = config_.threshold;
    vad_config.silero_vad.min_speech_duration = config_.min_speech_duration;
    vad_config.silero_vad.min_silence_duration = config_.min_silence_duration;
    vad_config.silero_vad.window_size = 512;
    vad_config.silero_vad.max_speech_duration = 30.0f;
    vad_config.sample_rate = 16000;
    vad_config.num_threads = config_.num_threads;
    const std::string provider = config_.provider.empty() ? "cpu" : config_.provider;
    vad_config.provider = provider.c_str();
    vad_config.debug = 0;

    detector_ = SherpaOnnxCreateVoiceActivityDetector(&vad_config, 60.0f);
}

SileroVad::~SileroVad() {
    if (detector_) {
        SherpaOnnxDestroyVoiceActivityDetector(detector_);
    }
}

namespace {

std::vector<float> to_float_16k(std::span<const int16_t> pcm, int sample_rate) {
    if (sample_rate != 16000) {
        const auto resampled = audio::resample_sinc_pcm16(pcm, sample_rate, 16000);
        std::vector<float> samples(resampled.size());
        for (size_t i = 0; i < resampled.size(); ++i) {
            samples[i] = static_cast<float>(resampled[i]) / 32768.0f;
        }
        return samples;
    }
    std::vector<float> samples(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
    }
    return samples;
}

std::vector<int16_t> float_to_pcm16(const float* data, int32_t n) {
    std::vector<int16_t> out(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        const auto v = static_cast<int32_t>(data[i] * 32767.0f);
        out[static_cast<size_t>(i)] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
    }
    return out;
}

}  // namespace

std::vector<int16_t> SileroVad::extract_speech(std::span<const int16_t> pcm, int sample_rate) const {
    if (!detector_ || pcm.empty()) {
        return {pcm.begin(), pcm.end()};
    }

    std::lock_guard lock(mutex_);
    auto samples = to_float_16k(pcm, sample_rate);

    SherpaOnnxVoiceActivityDetectorReset(detector_);
    SherpaOnnxVoiceActivityDetectorAcceptWaveform(
        detector_, samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxVoiceActivityDetectorFlush(detector_);

    std::vector<int16_t> speech;
    while (!SherpaOnnxVoiceActivityDetectorEmpty(detector_)) {
        const auto* segment = SherpaOnnxVoiceActivityDetectorFront(detector_);
        if (segment && segment->samples && segment->n > 0) {
            auto chunk = float_to_pcm16(segment->samples, segment->n);
            speech.insert(speech.end(), chunk.begin(), chunk.end());
        }
        SherpaOnnxVoiceActivityDetectorPop(detector_);
        if (segment) {
            SherpaOnnxDestroySpeechSegment(segment);
        }
    }

    return speech;
}

std::vector<SpeechTurn> SileroVad::detect_turns(std::span<const int16_t> pcm, int sample_rate) const {
    std::vector<SpeechTurn> turns;
    if (!detector_ || pcm.empty()) {
        return turns;
    }

    std::lock_guard lock(mutex_);
    auto samples = to_float_16k(pcm, sample_rate);

    SherpaOnnxVoiceActivityDetectorReset(detector_);
    SherpaOnnxVoiceActivityDetectorAcceptWaveform(
        detector_, samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxVoiceActivityDetectorFlush(detector_);

    while (!SherpaOnnxVoiceActivityDetectorEmpty(detector_)) {
        const auto* segment = SherpaOnnxVoiceActivityDetectorFront(detector_);
        if (segment && segment->samples && segment->n > 0) {
            SpeechTurn turn;
            turn.start_ms = static_cast<int64_t>(segment->start) * 1000 / 16000;
            turn.end_ms = turn.start_ms + static_cast<int64_t>(segment->n) * 1000 / 16000;
            turns.push_back(turn);
        }
        SherpaOnnxVoiceActivityDetectorPop(detector_);
        if (segment) {
            SherpaOnnxDestroySpeechSegment(segment);
        }
    }
    return turns;
}

DiarizationResult SileroVad::diarize(
    std::span<const int16_t> pcm,
    int sample_rate,
    const DiarizationConfig& config) const {
    DiarizationResult result;
    if (!ready() || pcm.empty()) {
        return result;
    }

    std::vector<int16_t> pcm16;
    std::span<const int16_t> view = pcm;
    if (sample_rate != 16000) {
        pcm16 = audio::resample_sinc_pcm16(pcm, sample_rate, 16000);
        view = pcm16;
    }

    const auto raw = detect_turns(view, 16000);
    // Always assign speaker_id / is_primary (mono turn-taking). Previously the
    // !focus_primary path returned raw turns all as speaker 0 — Lab then showed
    // a single interlocutor.
    result = pick_primary_turns(raw, view, config);
    if (!config.focus_primary) {
        // Engine does per-turn ASR on full timeline; keep labeled turns and a
        // speech extract for callers that still want primary_pcm as "all speech".
        result.primary_pcm = extract_speech(view, 16000);
        result.ok = !result.turns.empty() || !result.primary_pcm.empty();
    }
    return result;
}

}  // namespace voiceqas::stt
