#include "voiceqas/stt/vad.hpp"

#include <algorithm>
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

std::vector<int16_t> SileroVad::extract_speech(std::span<const int16_t> pcm, int sample_rate) const {
    if (!detector_ || pcm.empty()) {
        return {pcm.begin(), pcm.end()};
    }

    std::vector<float> samples(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
    }

    if (sample_rate != 16000) {
        const auto resampled = audio::resample_sinc_pcm16(pcm, sample_rate, 16000);
        samples.resize(resampled.size());
        for (size_t i = 0; i < resampled.size(); ++i) {
            samples[i] = static_cast<float>(resampled[i]) / 32768.0f;
        }
        sample_rate = 16000;
    }

    (void)sample_rate;
    SherpaOnnxVoiceActivityDetectorReset(detector_);
    SherpaOnnxVoiceActivityDetectorAcceptWaveform(
        detector_, samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxVoiceActivityDetectorFlush(detector_);

    std::vector<int16_t> speech;
    while (!SherpaOnnxVoiceActivityDetectorEmpty(detector_)) {
        const auto* segment = SherpaOnnxVoiceActivityDetectorFront(detector_);
        if (segment && segment->samples && segment->n > 0) {
            const auto prev = speech.size();
            speech.resize(prev + static_cast<size_t>(segment->n));
            for (int32_t i = 0; i < segment->n; ++i) {
                const auto v = static_cast<int32_t>(segment->samples[i] * 32767.0f);
                speech[prev + static_cast<size_t>(i)] = static_cast<int16_t>(
                    std::clamp(v, -32768, 32767));
            }
        }
        SherpaOnnxVoiceActivityDetectorPop(detector_);
        if (segment) {
            SherpaOnnxDestroySpeechSegment(segment);
        }
    }

    if (speech.empty()) {
        return {};
    }
    return speech;
}

}  // namespace voiceqas::stt
