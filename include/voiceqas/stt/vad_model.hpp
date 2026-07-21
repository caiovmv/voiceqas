#pragma once

#include <string>
#include <vector>

namespace voiceqas::stt {

struct VadModelInfo {
    std::string id;
    std::string name;
    std::string path;
    bool present = false;
    bool active = false;
};

struct ResolvedVadModel {
    std::string id;
    std::string name;
    std::string path;
};

// Resolves VAD ONNX path. selector: auto | k2fsa | k2fsa-int8 | v4 | v5 | filename | absolute path.
ResolvedVadModel resolve_vad_model(
    const std::string& models_dir,
    const std::string& selector,
    const std::string& explicit_path = {});

std::vector<VadModelInfo> list_vad_models(
    const std::string& models_dir,
    const std::string& active_id = {});

}  // namespace voiceqas::stt
