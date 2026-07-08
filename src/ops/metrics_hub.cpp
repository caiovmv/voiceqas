#include "voiceqas/ops/metrics_hub.hpp"

#include <chrono>

#include "voiceqas/json_util.hpp"
#include "voiceqas/ops/event_store.hpp"
#include "voiceqas/ops/external_ai_service.hpp"
#include "voiceqas/ops/pipeline_tracker.hpp"
#include "voiceqas/ops/prometheus.hpp"
#include "voiceqas/ops/transport_sankey.hpp"
#include "voiceqas/ops/webhook.hpp"
#include "voiceqas/stt/client.hpp"
#include "voiceqas/stt/json_util.hpp"

#include <vector>

namespace voiceqas::ops {

namespace {

int64_t event_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

OpsMetricsHub& OpsMetricsHub::instance() {
    static OpsMetricsHub hub;
    return hub;
}

OpsMetricsHub::ListenerId OpsMetricsHub::subscribe(SubscribeOptions options, JsonCallback callback) {
    std::lock_guard lock(mutex_);
    const auto id = next_id_++;
    listeners_.emplace(id, Listener{std::move(options), std::move(callback)});
    return id;
}

void OpsMetricsHub::unsubscribe(ListenerId id) {
    std::lock_guard lock(mutex_);
    listeners_.erase(id);
}

std::size_t OpsMetricsHub::listener_count() {
    std::lock_guard lock(mutex_);
    return listeners_.size();
}

void OpsMetricsHub::publish_event(nlohmann::json event) {
    if (!event.contains("type") || !event.contains("session_id")) {
        return;
    }

    if (!event.contains("ts_ms")) {
        event["ts_ms"] = event_timestamp_ms();
    }

    OpsEventStore::instance().append(event);

    const auto session_id = event["session_id"].get<std::string>();
    const auto type = event["type"].get<std::string>();
    if (type == "vqa_window") {
        PrometheusMetrics::instance().on_vqa_window(
            session_id,
            event.value("stt_ready", false),
            event.value("composite_score", 0.0),
            event.value("snr_estimate_db", 0.0),
            event.value("silence_ratio", 1.0));
        if (auto alert = evaluate_vqa_alert(session_id, event)) {
            publish_event(*alert);
        }
    } else if (type == "stt_partial" || type == "stt_final") {
        const auto processing_ms = event.value("processing_ms", int64_t{0});
        const auto duration_ms = event.value("duration_ms", int64_t{0});
        const auto bytes = static_cast<uint64_t>(std::max<int64_t>(duration_ms, 0)) * 32;
        const auto transport = event.value("transport", std::string{"websocket"});
        const auto latency_ms = static_cast<double>(processing_ms);
        PipelineTracker::instance().record_asr(session_id, processing_ms, bytes);
        PipelineTracker::instance().record_ai_agent_inbound(session_id, bytes);
        PipelineTracker::instance().record_transport_ingress(session_id, transport, bytes, latency_ms, 0.0);
        PipelineTracker::instance().record_media_pipeline(
            session_id, bytes, latency_ms, kPipelineDirectionInbound);
        if (type == "stt_final") {
            const auto text = event.value("text", std::string{});
            ExternalAiService::instance().on_stt_final(session_id, text, bytes);
        }
    }

    std::vector<JsonCallback> targets;
    {
        std::lock_guard lock(mutex_);
        targets.reserve(listeners_.size());
        for (const auto& [id, listener] : listeners_) {
            (void)id;
            if (listener.options.filter_session_id &&
                *listener.options.filter_session_id != session_id) {
                continue;
            }
            if (type == "vqa_window" && !listener.options.vqa) {
                continue;
            }
            if ((type == "stt_final" || type == "stt_partial") && !listener.options.stt) {
                continue;
            }
            if (type == "alert" && !listener.options.alerts) {
                continue;
            }
            if (type == "pipeline_snapshot" && !listener.options.pipeline) {
                continue;
            }
            targets.push_back(listener.callback);
        }
    }
    for (const auto& cb : targets) {
        cb(event);
    }
}

void OpsMetricsHub::publish(const std::string& session_id, const WindowMetrics& metrics) {
    PipelineTracker::instance().record_vqa_window(session_id, metrics);
    auto json = window_metrics_to_json(metrics);
    json["type"] = "vqa_window";
    json["session_id"] = session_id;
    publish_event(std::move(json));
    publish_pipeline_snapshot(session_id);
}

void publish_stt_result(
    const std::string& session_id,
    const stt::TranscriptResult& result,
    bool partial,
    const std::optional<std::string>& transport) {
    auto json = transcript_to_json(result);
    json["type"] = partial ? "stt_partial" : "stt_final";
    json["session_id"] = session_id;
    if (transport) {
        json["transport"] = *transport;
    }
    OpsMetricsHub::instance().publish_event(std::move(json));
}

}  // namespace voiceqas::ops
