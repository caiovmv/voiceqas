#include "voiceqas/audio/ingress_codec.hpp"

namespace voiceqas::audio {

AudioFormat preferred_ingress_format(const MediaRelayConfig& config) {
    try {
        return audio_format_from_string(config.preferred_ingress_codec);
    } catch (...) {
        return AudioFormat::RtpG722;
    }
}

IngressCodecResolution resolve_ingress_codec(
    const MediaRelayConfig& config,
    const std::optional<AudioFormat>& registered_format,
    const std::optional<AudioFormat>& detected_from_rtp) {
    IngressCodecResolution result;
    const auto preferred = preferred_ingress_format(config);
    result.registered = registered_format;

    if (registered_format) {
        result.format = *registered_format;
        if (detected_from_rtp && *detected_from_rtp != *registered_format) {
            result.detected = detected_from_rtp;
            result.suboptimal = true;
        } else if (*registered_format != preferred) {
            result.suboptimal = true;
        }
        return result;
    }

    if (config.ingress_autodetect && detected_from_rtp) {
        result.format = *detected_from_rtp;
        result.detected = detected_from_rtp;
        result.autodetected = true;
        result.suboptimal = *detected_from_rtp != preferred;
        return result;
    }

    result.format = preferred;
    if (detected_from_rtp) {
        result.detected = detected_from_rtp;
        result.suboptimal = *detected_from_rtp != preferred;
    }
    return result;
}

}  // namespace voiceqas::audio
