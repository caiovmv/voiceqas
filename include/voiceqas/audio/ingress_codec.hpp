#pragma once

#include <optional>
#include <string>

#include "voiceqas/audio/config.hpp"
#include "voiceqas/metrics.hpp"

namespace voiceqas::audio {

AudioFormat preferred_ingress_format(const MediaRelayConfig& config);

struct IngressCodecResolution {
    AudioFormat format = AudioFormat::RtpG722;
    std::optional<AudioFormat> detected;
    std::optional<AudioFormat> registered;
    bool autodetected = false;
    bool suboptimal = false;
};

IngressCodecResolution resolve_ingress_codec(
    const MediaRelayConfig& config,
    const std::optional<AudioFormat>& registered_format,
    const std::optional<AudioFormat>& detected_from_rtp);

}  // namespace voiceqas::audio
