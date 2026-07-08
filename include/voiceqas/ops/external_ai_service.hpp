#pragma once

#include <cstdint>
#include <string>

namespace voiceqas::ops {

class ExternalAiService {
public:
    static ExternalAiService& instance();

    void on_stt_final(const std::string& session_id, const std::string& text, uint64_t transcript_bytes);

private:
    ExternalAiService() = default;
};

}  // namespace voiceqas::ops