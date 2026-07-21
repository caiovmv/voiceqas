#include "voiceqas/audio/encoder.hpp"

#include <cstring>

#include "voiceqas/audio/codec_registry.hpp"
#include "voiceqas/audio/normalizer.hpp"
#include "voiceqas/stt/audio_prepare.hpp"

namespace voiceqas::audio {

void apply_peak_limiter_inplace(std::span<int16_t> samples, double ceiling_dbfs) {
    AudioProcessingConfig cfg;
    cfg.normalize_enabled = true;
    cfg.limiter_ceiling_dbfs = ceiling_dbfs;
    AgcState limiter(cfg);
    limiter.apply_peak_limiter_inplace(samples);
}

EncodeResult encode_from_pcm(
    AudioFormat format,
    std::span<const int16_t> pcm,
    rtp::RtpPacketizer* packetizer,
    int frame_ms) {
    EncodeResult result;
    if (pcm.empty()) {
        result.ok = true;
        return result;
    }

    const int sample_rate = sample_rate_for_format(format);
    const int frame_samples = sample_rate * frame_ms / 1000;
    if (frame_samples <= 0) {
        result.error = "invalid frame_ms";
        return result;
    }

    std::vector<uint8_t> encoded;
    if (format == AudioFormat::PcmS16Le8k || format == AudioFormat::PcmS16Le16k) {
        result.encoded.resize(pcm.size() * 2);
        std::memcpy(result.encoded.data(), pcm.data(), result.encoded.size());
        result.ok = true;
        return result;
    }

    encoded = default_codec_registry().encode_pcm(format, pcm);
    if (encoded.empty() && !pcm.empty()) {
        result.error = "unsupported encode format";
        return result;
    }

    result.encoded = std::move(encoded);
    if (packetizer) {
        const size_t bytes_per_frame = format == AudioFormat::RtpG729
            ? static_cast<size_t>(frame_ms == 20 ? 20 : 10)
            : (format == AudioFormat::RtpG722
                ? static_cast<size_t>(frame_samples / 2)
                : static_cast<size_t>(frame_samples));

        for (size_t i = 0; i < result.encoded.size(); i += bytes_per_frame) {
            const size_t n = std::min(bytes_per_frame, result.encoded.size() - i);
            result.rtp_packets.push_back(
                packetizer->pack_payload({result.encoded.data() + i, n}));
        }
    }

    result.ok = true;
    return result;
}

}  // namespace voiceqas::audio
