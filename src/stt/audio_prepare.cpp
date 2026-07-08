#include "voiceqas/stt/audio_prepare.hpp"

#include "voiceqas/tracing/tracing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "voiceqas/audio/decoder.hpp"
#include "voiceqas/audio/normalizer.hpp"
#include "voiceqas/audio/resampler.hpp"
#include "voiceqas/rtp/depacketizer.hpp"

namespace voiceqas::stt {

std::vector<int16_t> resample_pcm16(
    std::span<const int16_t> input,
    int from_rate,
    int to_rate) {
    return audio::resample_sinc_pcm16(input, from_rate, to_rate);
}

PreparedAudio prepare_audio_for_stt(
    AudioFormat format,
    std::span<const uint8_t> payload,
    int sample_rate,
    int target_sample_rate,
    const audio::AudioProcessingConfig& audio_config,
    rtp::RtpDepacketizer* persistent_depacketizer) {
    PreparedAudio prepared;
    prepared.payload_bytes = payload.size();
    const int rate = sample_rate > 0 ? sample_rate : sample_rate_for_format(format);

    rtp::RtpDepacketizer local_depacketizer(rtp_clock_rate_for_format(format));
    rtp::RtpDepacketizer* depacketizer_ptr = nullptr;
    if (audio::is_rtp_format(format)) {
        depacketizer_ptr = persistent_depacketizer ? persistent_depacketizer : &local_depacketizer;
    }

    const auto decode_started = std::chrono::steady_clock::now();
    tracing::StageSpan decode_span("decode_stt", "Decode STT");
    decode_span.set_bytes_in(payload.size());
    const auto decoded = audio::decode_to_pcm(format, payload, depacketizer_ptr);
    prepared.decode_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - decode_started)
                             .count();
    decode_span.set_metric("voiceqas.duration_ms", prepared.decode_ms);
    if (!decoded.ok) {
        return prepared;
    }
    decode_span.set_bytes_out(decoded.pcm.size() * sizeof(int16_t));

    auto pcm = decoded.pcm;
    if (audio_config.normalize_enabled && !pcm.empty()) {
        tracing::StageSpan agc_span("agc_stt", "AGC STT");
        agc_span.set_bytes_in(pcm.size() * sizeof(int16_t));
        const auto agc_started = std::chrono::steady_clock::now();
        audio::AgcState agc(audio_config);
        agc.process_inplace(pcm, rate);
        prepared.agc_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - agc_started)
                              .count();
        agc_span.set_metric("voiceqas.duration_ms", prepared.agc_ms);
        agc_span.set_bytes_out(pcm.size() * sizeof(int16_t));
    }

    tracing::StageSpan resample_span("resample_16k", "Resample 16 kHz");
    resample_span.set_bytes_in(pcm.size() * sizeof(int16_t));
    const auto resample_started = std::chrono::steady_clock::now();
    prepared.sample_rate = target_sample_rate;
    prepared.pcm = resample_pcm16(pcm, rate, target_sample_rate);
    prepared.resample_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - resample_started)
                               .count();
    resample_span.set_metric("voiceqas.duration_ms", prepared.resample_ms);
    resample_span.set_bytes_out(prepared.pcm.size() * sizeof(int16_t));
    return prepared;
}

}  // namespace voiceqas::stt
