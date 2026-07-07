#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "voiceqas/metrics.hpp"
#include "voiceqas/rtp/depacketizer.hpp"

namespace voiceqas::media {

struct DecodedRtpFrame {
    std::vector<int16_t> pcm;
    int sample_rate = 8000;
    rtp::RtpStats stats{};
    AudioFormat format = AudioFormat::RtpPcmu;
    bool ok = false;
};

class RtpIngressProcessor {
public:
    std::optional<DecodedRtpFrame> decode(
        const std::string& session_id,
        AudioFormat format,
        std::span<const uint8_t> payload);

    void remove_session(const std::string& session_id);

private:
    struct SessionState {
        std::optional<rtp::RtpDepacketizer> depacketizer;
        AudioFormat format = AudioFormat::RtpPcmu;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionState> sessions_;
};

}  // namespace voiceqas::media
