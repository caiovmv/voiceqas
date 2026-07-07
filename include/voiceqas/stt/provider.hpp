#pragma once

#include <string>
#include <vector>

namespace voiceqas::stt {

bool stt_cuda_compiled();
std::vector<std::string> available_stt_providers();

/// Normalizes user input. Returns "cpu" or "cuda" (cuda only if compiled in).
std::string normalize_stt_provider(const std::string& value);

/// Returns empty if provider is usable; otherwise an error message.
std::string validate_stt_provider(const std::string& provider);

}  // namespace voiceqas::stt
