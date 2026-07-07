#include "voiceqas/stt/provider.hpp"

#include <algorithm>
#include <cctype>

namespace voiceqas::stt {

bool stt_cuda_compiled() {
#if defined(VOICEQAS_STT_CUDA) && VOICEQAS_STT_CUDA
    return true;
#else
    return false;
#endif
}

std::vector<std::string> available_stt_providers() {
    if (stt_cuda_compiled()) {
        return {"cpu", "cuda"};
    }
    return {"cpu"};
}

std::string normalize_stt_provider(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "cuda" || lower == "gpu") {
        return "cuda";
    }
    return "cpu";
}

std::string validate_stt_provider(const std::string& provider) {
    const auto normalized = normalize_stt_provider(provider);
    if (normalized == "cuda" && !stt_cuda_compiled()) {
        return "CUDA provider requested but voiceqas was built without VOICEQAS_STT_CUDA";
    }
    if (normalized != "cpu" && normalized != "cuda") {
        return "unsupported STT provider: " + provider;
    }
    return {};
}

}  // namespace voiceqas::stt
