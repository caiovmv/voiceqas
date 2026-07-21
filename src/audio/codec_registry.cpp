#include "voiceqas/audio/codec_registry.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "voiceqas/rtp/g711_codec.hpp"
#include "voiceqas/rtp/g722_codec.hpp"
#include "voiceqas/rtp/g729_codec.hpp"

namespace voiceqas::audio {
namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void register_builtin_codecs(CodecRegistry& registry) {
    registry.register_codec(CodecDescriptor{
        .format = AudioFormat::PcmS16Le8k,
        .name = "pcm_s16le_8k",
        .sample_rate = 8000,
        .rtp_clock_rate = 8000,
        .is_rtp = false,
    });
    registry.register_codec(CodecDescriptor{
        .format = AudioFormat::PcmS16Le16k,
        .name = "pcm_s16le_16k",
        .sample_rate = 16000,
        .rtp_clock_rate = 16000,
        .is_rtp = false,
    });
    registry.register_codec(CodecDescriptor{
        .format = AudioFormat::RtpPcmu,
        .name = "rtp_pcmu",
        .sample_rate = 8000,
        .rtp_clock_rate = 8000,
        .rtp_payload_type = rtp::PayloadType::Pcmu,
        .encode_pcm = [](std::span<const int16_t> pcm) { return rtp::encode_g711_pcmu(pcm); },
    });
    registry.register_codec(CodecDescriptor{
        .format = AudioFormat::RtpPcma,
        .name = "rtp_pcma",
        .sample_rate = 8000,
        .rtp_clock_rate = 8000,
        .rtp_payload_type = rtp::PayloadType::Pcma,
        .encode_pcm = [](std::span<const int16_t> pcm) { return rtp::encode_g711_pcma(pcm); },
    });
    registry.register_codec(CodecDescriptor{
        .format = AudioFormat::RtpG722,
        .name = "rtp_g722",
        .sample_rate = 16000,
        .rtp_clock_rate = 8000,
        .rtp_payload_type = rtp::PayloadType::G722,
        .encode_pcm = [](std::span<const int16_t> pcm) { return rtp::encode_g722(pcm); },
    });
    registry.register_codec(CodecDescriptor{
        .format = AudioFormat::RtpG729,
        .name = "rtp_g729",
        .sample_rate = 8000,
        .rtp_clock_rate = 8000,
        .rtp_payload_type = rtp::PayloadType::G729,
        .encode_pcm = [](std::span<const int16_t> pcm) { return rtp::encode_g729(pcm); },
    });

    const std::pair<const char*, AudioFormat> aliases[] = {
        {"pcm_8k", AudioFormat::PcmS16Le8k},
        {"pcm_16k", AudioFormat::PcmS16Le16k},
        {"pcmu", AudioFormat::RtpPcmu},
        {"g711", AudioFormat::RtpPcmu},
        {"g711_ulaw", AudioFormat::RtpPcmu},
        {"pcma", AudioFormat::RtpPcma},
        {"g711_alaw", AudioFormat::RtpPcma},
        {"g722", AudioFormat::RtpG722},
        {"g729", AudioFormat::RtpG729},
    };
    for (const auto& [alias, format] : aliases) {
        registry.register_alias(alias, format);
    }
}

}  // namespace

void CodecRegistry::register_alias(const std::string& name, AudioFormat format) {
    name_index_[to_lower(name)] = format;
}

void CodecRegistry::register_codec(CodecDescriptor descriptor) {
    name_index_[to_lower(descriptor.name)] = descriptor.format;
    if (!codecs_.contains(descriptor.format) || descriptor.encode_pcm) {
        codecs_[descriptor.format] = std::move(descriptor);
    }
}

const CodecDescriptor* CodecRegistry::find(AudioFormat format) const {
    const auto it = codecs_.find(format);
    return it == codecs_.end() ? nullptr : &it->second;
}

std::optional<AudioFormat> CodecRegistry::format_from_string(const std::string& name) const {
    const auto it = name_index_.find(to_lower(name));
    if (it == name_index_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string CodecRegistry::format_to_string(AudioFormat format) const {
    if (const auto* desc = find(format)) {
        if (desc->name == "pcm_s16le_8k" || desc->name == "pcm_s16le_16k" || desc->name.rfind("rtp_", 0) == 0) {
            return desc->name;
        }
    }
    switch (format) {
        case AudioFormat::PcmS16Le8k:
            return "pcm_s16le_8k";
        case AudioFormat::PcmS16Le16k:
            return "pcm_s16le_16k";
        case AudioFormat::RtpPcmu:
            return "rtp_pcmu";
        case AudioFormat::RtpPcma:
            return "rtp_pcma";
        case AudioFormat::RtpG722:
            return "rtp_g722";
        case AudioFormat::RtpG729:
            return "rtp_g729";
    }
    return "unknown";
}

int CodecRegistry::sample_rate_for(AudioFormat format) const {
    if (const auto* desc = find(format)) {
        return desc->sample_rate;
    }
    return 8000;
}

int CodecRegistry::rtp_clock_rate_for(AudioFormat format) const {
    if (const auto* desc = find(format)) {
        return desc->rtp_clock_rate;
    }
    return sample_rate_for(format);
}

rtp::PayloadType CodecRegistry::payload_type_for(AudioFormat format) const {
    if (const auto* desc = find(format)) {
        return desc->rtp_payload_type;
    }
    return rtp::PayloadType::Pcmu;
}

std::vector<uint8_t> CodecRegistry::encode_pcm(AudioFormat format, std::span<const int16_t> pcm) const {
    if (const auto* desc = find(format)) {
        if (desc->encode_pcm) {
            return desc->encode_pcm(pcm);
        }
    }
    return {};
}

CodecRegistry& default_codec_registry() {
    static CodecRegistry registry;
    static bool initialized = false;
    if (!initialized) {
        register_builtin_codecs(registry);
        initialized = true;
    }
    return registry;
}

}  // namespace voiceqas::audio
