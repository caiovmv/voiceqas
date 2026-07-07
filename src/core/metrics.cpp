#include "voiceqas/metrics.hpp"

#include <stdexcept>

#include "voiceqas/audio/codec_registry.hpp"

namespace voiceqas {

int sample_rate_for_format(AudioFormat format) {
    return audio::default_codec_registry().sample_rate_for(format);
}

int rtp_clock_rate_for_format(AudioFormat format) {
    return audio::default_codec_registry().rtp_clock_rate_for(format);
}

AudioFormat audio_format_from_string(const std::string& s) {
    if (auto format = audio::default_codec_registry().format_from_string(s)) {
        return *format;
    }
    throw std::invalid_argument("unknown audio format: " + s);
}

std::string audio_format_to_string(AudioFormat format) {
    return audio::default_codec_registry().format_to_string(format);
}

}  // namespace voiceqas
