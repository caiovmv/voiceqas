#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "voiceqas/stt/client.hpp"
#include "voiceqas/metrics.hpp"

namespace voiceqas::ops {

class OpsMetricsHub {
public:
    using ListenerId = uint64_t;
    using JsonCallback = std::function<void(const nlohmann::json& event)>;

    struct SubscribeOptions {
        std::optional<std::string> filter_session_id;
        bool vqa = true;
        bool stt = true;
        bool alerts = true;
        bool pipeline = true;
    };

    static OpsMetricsHub& instance();

    ListenerId subscribe(SubscribeOptions options, JsonCallback callback);
    void unsubscribe(ListenerId id);
    std::size_t listener_count();

    void publish(const std::string& session_id, const WindowMetrics& metrics);
    void publish_event(nlohmann::json event);

private:
    OpsMetricsHub() = default;

    struct Listener {
        SubscribeOptions options;
        JsonCallback callback;
    };

    std::mutex mutex_;
    ListenerId next_id_ = 1;
    std::unordered_map<ListenerId, Listener> listeners_;
};

void publish_stt_result(const std::string& session_id, const stt::TranscriptResult& result, bool partial);

}  // namespace voiceqas::ops
