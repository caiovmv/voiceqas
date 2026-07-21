#include "voiceqas/stt/vad_model.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace voiceqas::stt {

namespace fs = std::filesystem;

namespace {

struct VadCatalogEntry {
    std::string id;
    std::string filename;
    std::string name;
    int priority;
};

const std::vector<VadCatalogEntry>& vad_catalog() {
    static const std::vector<VadCatalogEntry> entries = {
        {"k2fsa", "silero_vad.onnx", "silero-vad k2-fsa (recommended)", 0},
        {"v5", "silero_vad_v5.onnx", "silero-vad v5", 1},
        {"v4", "silero_vad_v4.onnx", "silero-vad v4", 2},
        {"k2fsa-int8", "silero_vad.int8.onnx", "silero-vad k2-fsa int8", 3},
    };
    return entries;
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string join_models_path(const std::string& models_dir, const std::string& leaf) {
    return (fs::path(models_dir) / leaf).string();
}

const VadCatalogEntry* find_catalog_entry(const std::string& selector) {
    const auto key = lower_copy(selector);
    for (const auto& entry : vad_catalog()) {
        if (key == entry.id || key == entry.filename) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

ResolvedVadModel resolve_vad_model(
    const std::string& models_dir,
    const std::string& selector,
    const std::string& explicit_path) {
    if (!explicit_path.empty()) {
        if (fs::is_regular_file(explicit_path)) {
            const auto* entry = find_catalog_entry(fs::path(explicit_path).filename().string());
            return {
                entry ? entry->id : "custom",
                entry ? entry->name : fs::path(explicit_path).filename().string(),
                explicit_path,
            };
        }
        const auto joined = join_models_path(models_dir, explicit_path);
        if (fs::is_regular_file(joined)) {
            const auto* entry = find_catalog_entry(explicit_path);
            return {
                entry ? entry->id : "custom",
                entry ? entry->name : explicit_path,
                joined,
            };
        }
    }

    const auto key = lower_copy(selector.empty() ? "auto" : selector);
    if (key != "auto") {
        if (const auto* entry = find_catalog_entry(key)) {
            const auto path = join_models_path(models_dir, entry->filename);
            if (fs::is_regular_file(path)) {
                return {entry->id, entry->name, path};
            }
        }
        if (fs::is_regular_file(key)) {
            return {"custom", fs::path(key).filename().string(), key};
        }
        const auto joined = join_models_path(models_dir, selector);
        if (fs::is_regular_file(joined)) {
            return {"custom", selector, joined};
        }
        return {};
    }

    const auto& catalog = vad_catalog();
    auto ordered = catalog;
    std::sort(ordered.begin(), ordered.end(), [](const VadCatalogEntry& a, const VadCatalogEntry& b) {
        return a.priority < b.priority;
    });
    for (const auto& entry : ordered) {
        const auto path = join_models_path(models_dir, entry.filename);
        if (fs::is_regular_file(path)) {
            return {entry.id, entry.name, path};
        }
    }
    return {};
}

std::vector<VadModelInfo> list_vad_models(const std::string& models_dir, const std::string& active_id) {
    std::vector<VadModelInfo> out;
    out.reserve(vad_catalog().size());
    for (const auto& entry : vad_catalog()) {
        const auto path = join_models_path(models_dir, entry.filename);
        VadModelInfo info;
        info.id = entry.id;
        info.name = entry.name;
        info.path = path;
        info.present = fs::is_regular_file(path);
        info.active = !active_id.empty() && active_id == entry.id;
        out.push_back(std::move(info));
    }
    return out;
}

}  // namespace voiceqas::stt
