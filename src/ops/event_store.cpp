#include "voiceqas/ops/event_store.hpp"

#include "voiceqas/ops/config.hpp"

#include <chrono>
#include <fstream>
#include <mutex>

namespace voiceqas::ops {

namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

OpsEventStore& OpsEventStore::instance() {
    static OpsEventStore store;
    return store;
}

void OpsEventStore::append(const nlohmann::json& event) {
    const auto& cfg = global_ops_config();
    if (!cfg.persistence_enabled || cfg.storage_path.empty()) {
        return;
    }

    static std::mutex mutex;
    std::lock_guard lock(mutex);

    auto row = event;
    if (!row.contains("ts_ms")) {
        row["ts_ms"] = now_ms();
    }

    std::ofstream out(cfg.storage_path, std::ios::app);
    if (!out) {
        return;
    }
    out << row.dump() << '\n';
}

std::vector<nlohmann::json> OpsEventStore::query(
    const std::optional<std::string>& session_id,
    const std::optional<std::string>& type,
    int limit) const {
    const auto& cfg = global_ops_config();
    std::vector<nlohmann::json> rows;
    if (!cfg.persistence_enabled || cfg.storage_path.empty() || limit <= 0) {
        return rows;
    }

    std::ifstream in(cfg.storage_path);
    if (!in) {
        return rows;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            auto json = nlohmann::json::parse(line);
            if (session_id && json.value("session_id", std::string{}) != *session_id) {
                continue;
            }
            if (type && json.value("type", std::string{}) != *type) {
                continue;
            }
            rows.push_back(std::move(json));
        } catch (...) {
            continue;
        }
    }

    if (static_cast<int>(rows.size()) > limit) {
        rows.erase(rows.begin(), rows.end() - limit);
    }
    return rows;
}

}  // namespace voiceqas::ops
