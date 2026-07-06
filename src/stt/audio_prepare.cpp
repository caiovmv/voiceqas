#include "voiceqas/stt/audio_prepare.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "voiceqas/rtp/depacketizer.hpp"

namespace voiceqas::stt {

namespace {

std::vector<int16_t> decode_payload_to_pcm(
    AudioFormat format,
    std::span<const uint8_t> payload) {
    if (format == AudioFormat::RtpPcmu || format == AudioFormat::RtpPcma
        || format == AudioFormat::RtpG722 || format == AudioFormat::RtpG729) {
        rtp::RtpDepacketizer depacketizer(rtp_clock_rate_for_format(format));
        rtp::PayloadType pt = rtp::PayloadType::Pcmu;
        switch (format) {
            case AudioFormat::RtpPcma: pt = rtp::PayloadType::Pcma; break;
            case AudioFormat::RtpG722: pt = rtp::PayloadType::G722; break;
            case AudioFormat::RtpG729: pt = rtp::PayloadType::G729; break;
            default: break;
        }
        return depacketizer.decode_packet(payload, pt).pcm;
    }

    std::vector<int16_t> pcm(payload.size() / 2);
    std::memcpy(pcm.data(), payload.data(), payload.size());
    return pcm;
}

}  // namespace

std::vector<int16_t> resample_pcm16(
    std::span<const int16_t> input,
    int from_rate,
    int to_rate) {
    if (from_rate == to_rate || input.empty()) {
        return {input.begin(), input.end()};
    }

    const double ratio = static_cast<double>(from_rate) / static_cast<double>(to_rate);
    const size_t out_len = static_cast<size_t>(std::floor(static_cast<double>(input.size()) / ratio));
    std::vector<int16_t> out(out_len);

    for (size_t i = 0; i < out_len; ++i) {
        const double src = static_cast<double>(i) * ratio;
        const size_t idx = static_cast<size_t>(src);
        const double frac = src - static_cast<double>(idx);
        const int16_t a = input[std::min(idx, input.size() - 1)];
        const int16_t b = input[std::min(idx + 1, input.size() - 1)];
        out[i] = static_cast<int16_t>(std::lround(static_cast<double>(a) + (static_cast<double>(b) - a) * frac));
    }
    return out;
}

PreparedAudio prepare_audio_for_stt(
    AudioFormat format,
    std::span<const uint8_t> payload,
    int sample_rate,
    int target_sample_rate) {
    PreparedAudio prepared;
    const int rate = sample_rate > 0 ? sample_rate : sample_rate_for_format(format);
    auto pcm = decode_payload_to_pcm(format, payload);
    prepared.sample_rate = target_sample_rate;
    prepared.pcm = resample_pcm16(pcm, rate, target_sample_rate);
    return prepared;
}

}  // namespace voiceqas::stt
