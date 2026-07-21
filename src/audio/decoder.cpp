#include "voiceqas/audio/codec_registry.hpp"
#include "voiceqas/audio/decoder.hpp"

#include <cstring>

namespace voiceqas::audio {

bool is_rtp_format(AudioFormat format) {
    return format == AudioFormat::RtpPcmu || format == AudioFormat::RtpPcma
        || format == AudioFormat::RtpG722 || format == AudioFormat::RtpG729;
}

AudioFormat format_from_rtp_payload_type(uint8_t payload_type) {
    switch (payload_type) {
        case 8:
            return AudioFormat::RtpPcma;
        case 9:
            return AudioFormat::RtpG722;
        case 18:
            return AudioFormat::RtpG729;
        case 0:
        default:
            return AudioFormat::RtpPcmu;
    }
}

rtp::PayloadType payload_type_for_format(AudioFormat format) {
    return default_codec_registry().payload_type_for(format);
}

uint32_t rtp_timestamp_step_for_format(AudioFormat format, int frame_ms) {
    const int rate = sample_rate_for_format(format);
    return static_cast<uint32_t>(rate * frame_ms / 1000);
}

namespace {

size_t rtp_packet_length(std::span<const uint8_t> data, size_t offset, uint8_t expected_pt) {
    if (offset + 12 > data.size()) {
        return 0;
    }
    const auto hdr = rtp::parse_header(data.subspan(offset));
    if (!hdr || hdr->version != 2) {
        return 0;
    }
    const uint16_t seq = hdr->sequence;

    for (size_t i = offset + hdr->header_size + 1; i + 12 <= data.size(); ++i) {
        if ((data[i] & 0xC0) != 0x80) {
            continue;
        }
        const auto next = rtp::parse_header(data.subspan(i));
        if (!next || next->version != 2) {
            continue;
        }
        if ((next->payload_type & 0x7F) != expected_pt) {
            continue;
        }
        if (next->sequence == static_cast<uint16_t>(seq + 1)) {
            return i - offset;
        }
    }
    return data.size() - offset;
}

}  // namespace

DecodeResult decode_rtp_concatenated(
    AudioFormat format,
    std::span<const uint8_t> data,
    rtp::RtpDepacketizer& depacketizer) {
    DecodeResult result;
    const auto pt = payload_type_for_format(format);
    const uint8_t expected_pt = static_cast<uint8_t>(pt);

    size_t offset = 0;
    while (offset < data.size()) {
        if (offset + 12 > data.size()) {
            break;
        }
        const auto hdr = rtp::parse_header(data.subspan(offset));
        if (!hdr || hdr->version != 2) {
            ++offset;
            continue;
        }
        const size_t len = rtp_packet_length(data, offset, expected_pt);
        if (len < hdr->header_size) {
            break;
        }
        const auto decoded = depacketizer.decode_packet(data.subspan(offset, len), pt);
        result.pcm.insert(result.pcm.end(), decoded.pcm.begin(), decoded.pcm.end());
        result.rtp_stats = decoded.stats;
        offset += len;
    }

    result.ok = data.empty() || !result.pcm.empty();
    if (!result.ok) {
        result.error = "rtp decode produced no pcm";
    }
    return result;
}

DecodeResult decode_to_pcm(
    AudioFormat format,
    std::span<const uint8_t> payload,
    rtp::RtpDepacketizer* depacketizer) {
    DecodeResult result;

    if (is_rtp_format(format)) {
        if (!depacketizer) {
            result.error = "rtp depacketizer required";
            return result;
        }
        return decode_rtp_concatenated(format, payload, *depacketizer);
    }

    if (payload.size() % 2 != 0) {
        result.error = "pcm payload size must be even";
        return result;
    }
    result.pcm.resize(payload.size() / 2);
    std::memcpy(result.pcm.data(), payload.data(), payload.size());
    result.ok = true;
    return result;
}

}  // namespace voiceqas::audio
