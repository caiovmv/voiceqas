#include "voiceqas/metrics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace voiceqas {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

int sample_rate_for_format(AudioFormat format) {
    switch (format) {
        case AudioFormat::PcmS16Le8k:
        case AudioFormat::RtpPcmu:
        case AudioFormat::RtpPcma:
        case AudioFormat::RtpG729:
            return 8000;
        case AudioFormat::PcmS16Le16k:
        case AudioFormat::RtpG722:
            return 16000;
    }
    return 8000;
}

int rtp_clock_rate_for_format(AudioFormat format) {
    switch (format) {
        case AudioFormat::RtpG722:
            return 8000;
        case AudioFormat::RtpPcmu:
        case AudioFormat::RtpPcma:
        case AudioFormat::RtpG729:
            return 8000;
        default:
            return sample_rate_for_format(format);
    }
}

AudioFormat audio_format_from_string(const std::string& s) {
    const auto key = to_lower(s);
    if (key == "pcm_s16le_8k" || key == "pcm_8k") {
        return AudioFormat::PcmS16Le8k;
    }
    if (key == "pcm_s16le_16k" || key == "pcm_16k") {
        return AudioFormat::PcmS16Le16k;
    }
    if (key == "rtp_pcmu" || key == "pcmu" || key == "g711" || key == "g711_ulaw") {
        return AudioFormat::RtpPcmu;
    }
    if (key == "rtp_pcma" || key == "pcma" || key == "g711_alaw") {
        return AudioFormat::RtpPcma;
    }
    if (key == "rtp_g722" || key == "g722") {
        return AudioFormat::RtpG722;
    }
    if (key == "rtp_g729" || key == "g729") {
        return AudioFormat::RtpG729;
    }
    throw std::invalid_argument("unknown audio format: " + s);
}

}  // namespace voiceqas
