#include "voiceqas/ops/webhook.hpp"

#include "voiceqas/metrics.hpp"
#include "voiceqas/ops/config.hpp"
#include "voiceqas/ops/metrics_hub.hpp"

#include <httplib.h>

#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace voiceqas::ops {

namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void post_async(const std::string& url, nlohmann::json body) {
    std::thread([url, body = std::move(body)]() mutable {
        const auto scheme_end = url.find("://");
        if (scheme_end == std::string::npos) {
            return;
        }
        const auto host_start = scheme_end + 3;
        const auto path_start = url.find('/', host_start);
        const std::string host = path_start == std::string::npos
            ? url.substr(host_start)
            : url.substr(host_start, path_start - host_start);
        const std::string path =
            path_start == std::string::npos ? "/" : url.substr(path_start);
        const bool tls = url.rfind("https://", 0) == 0;

        httplib::Client cli(tls ? ("https://" + host) : ("http://" + host));
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(5, 0);
        cli.Post(path, body.dump(), "application/json");
    }).detach();
}

}  // namespace

void dispatch_webhook(const std::string& kind, const nlohmann::json& payload) {
    const auto& cfg = global_ops_config();
    if (cfg.webhook_url.empty()) {
        return;
    }

    nlohmann::json body = {
        {"kind", kind},
        {"ts_ms", now_ms()},
        {"payload", payload},
    };
    post_async(cfg.webhook_url, std::move(body));
}

std::optional<nlohmann::json> evaluate_vqa_alert(
    const std::string& session_id,
    const nlohmann::json& event) {
    const auto& cfg = global_ops_config();
    if (!event.contains("composite_score")) {
        return std::nullopt;
    }

    const double score = event["composite_score"].get<double>();
    static std::mutex mutex;
    static std::unordered_map<std::string, int64_t> last_low_score_ms;
    static std::unordered_map<std::string, int64_t> stt_not_ready_since;

    const auto now = now_ms();
    std::optional<nlohmann::json> alert;

    {
        std::lock_guard lock(mutex);
        if (score < cfg.alert_score_threshold) {
            const auto& last = last_low_score_ms[session_id];
            if (now - last > 60'000) {
                last_low_score_ms[session_id] = now;
                alert = nlohmann::json{
                    {"type", "alert"},
                    {"alert_kind", "low_score"},
                    {"session_id", session_id},
                    {"composite_score", score},
                    {"threshold", cfg.alert_score_threshold},
                    {"ts_ms", now},
                };
            }
        }

        if (event.contains("stt_ready") && !event["stt_ready"].get<bool>()) {
            if (!stt_not_ready_since.contains(session_id)) {
                stt_not_ready_since[session_id] = now;
            } else if (now - stt_not_ready_since[session_id] >= cfg.stt_not_ready_alert_ms &&
                       now - stt_not_ready_since[session_id] < cfg.stt_not_ready_alert_ms + 5'000) {
                alert = nlohmann::json{
                    {"type", "alert"},
                    {"alert_kind", "stt_not_ready"},
                    {"session_id", session_id},
                    {"since_ms", stt_not_ready_since[session_id]},
                    {"ts_ms", now},
                };
            }
        } else {
            stt_not_ready_since.erase(session_id);
        }
    }

    if (alert) {
        dispatch_webhook(alert->value("alert_kind", "alert"), *alert);
    }
    return alert;
}

void publish_codec_mismatch_alert(
    const std::string& session_id,
    AudioFormat preferred,
    AudioFormat actual,
    const std::string& source) {
    if (preferred == actual) {
        return;
    }

    static std::mutex mutex;
    static std::unordered_set<std::string> alerted_sessions;
    const auto now = now_ms();

    {
        std::lock_guard lock(mutex);
        if (!alerted_sessions.insert(session_id).second) {
            return;
        }
    }

    const auto preferred_name = audio_format_to_string(preferred);
    const auto actual_name = audio_format_to_string(actual);

    std::cerr << "voiceqas codec: session=" << session_id << " preferred=" << preferred_name
              << " actual=" << actual_name << " source=" << source << '\n';

    nlohmann::json alert = {
        {"type", "alert"},
        {"alert_kind", "codec_suboptimal"},
        {"session_id", session_id},
        {"preferred_codec", preferred_name},
        {"actual_codec", actual_name},
        {"source", source},
        {"ts_ms", now},
        {"message",
         "Codec ideal não selecionado: esperado " + preferred_name + ", recebido " + actual_name},
    };

    OpsMetricsHub::instance().publish_event(alert);
    dispatch_webhook("codec_suboptimal", alert);
}

}  // namespace voiceqas::ops
