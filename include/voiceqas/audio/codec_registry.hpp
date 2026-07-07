#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "voiceqas/metrics.hpp"
#include "voiceqas/rtp/depacketizer.hpp"

namespace voiceqas::audio {

using PcmEncoderFn = std::function<std::vector<uint8_t>(std::span<const int16_t>)>;

struct CodecDescriptor {
    AudioFormat format = AudioFormat::RtpPcmu;
    std::string name;
    int sample_rate = 8000;
    int rtp_clock_rate = 8000;
    rtp::PayloadType rtp_payload_type = rtp::PayloadType::Pcmu;
    bool is_rtp = true;
    PcmEncoderFn encode_pcm;
};

class CodecRegistry {
public:
    void register_codec(CodecDescriptor descriptor);
    void register_alias(const std::string& name, AudioFormat format);
    const CodecDescriptor* find(AudioFormat format) const;
    std::optional<AudioFormat> format_from_string(const std::string& name) const;
    std::string format_to_string(AudioFormat format) const;
    int sample_rate_for(AudioFormat format) const;
    int rtp_clock_rate_for(AudioFormat format) const;
    rtp::PayloadType payload_type_for(AudioFormat format) const;
    std::vector<uint8_t> encode_pcm(AudioFormat format, std::span<const int16_t> pcm) const;

private:
    std::unordered_map<AudioFormat, CodecDescriptor> codecs_;
    std::unordered_map<std::string, AudioFormat> name_index_;
};

CodecRegistry& default_codec_registry();

}  // namespace voiceqas::audio
