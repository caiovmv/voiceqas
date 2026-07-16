#pragma once

#include <cstdint>
#include <string>

namespace voiceqas::ops {

class ExternalAiService {
public:
    static ExternalAiService& instance();

    void on_stt_final(const std::string& session_id, const std::string& text, uint64_t transcript_bytes);

    /** Synchronous Ollama /api/chat. Returns assistant content or empty + error. */
    std::string chat(
        const std::string& system_prompt,
        const std::string& user_prompt,
        std::string* error_out = nullptr);

private:
    ExternalAiService() = default;
};

}  // namespace voiceqas::ops
