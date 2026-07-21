#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace voiceqas::ops {

class OpsEventStore {
public:
    static OpsEventStore& instance();

    void append(const nlohmann::json& event);
    std::vector<nlohmann::json> query(
        const std::optional<std::string>& session_id,
        const std::optional<std::string>& type,
        int limit) const;

private:
    OpsEventStore() = default;
};

}  // namespace voiceqas::ops
