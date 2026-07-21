#include "voiceqas/ports/metrics_publisher.hpp"
#include "voiceqas/ports/pipeline_telemetry.hpp"

namespace voiceqas::ports {
namespace {

class NoopMetricsPublisher final : public IMetricsPublisher {
public:
    void publish_vqa(const std::string&, const WindowMetrics&) override {}
    void publish_stt(const std::string&, const stt::TranscriptResult&, bool) override {}
};

class NoopPipelineTelemetry final : public IPipelineTelemetry {
public:
    void record_rtp_ingress(
        const std::string&,
        uint64_t,
        double,
        double,
        bool) override {}
    void record_vqa_path(
        const std::string&,
        uint64_t,
        uint64_t,
        double,
        double,
        double,
        double,
        double) override {}
    void record_vqa_window(const std::string&, const WindowMetrics&) override {}
    void record_stt_gate_drop(const std::string&, uint64_t) override {}
    void record_stt_prepare(const std::string&, const SttPrepareTimings&) override {}
    void record_stt_buffer(const std::string&, int, size_t) override {}
    void set_session_codec(const std::string&, const std::string&) override {}
    void finish_session(const std::string&, const std::string&) override {}
    void remove_session(const std::string&) override {}
    void on_session_removed(const std::string&) override {}
    void record_vad(const std::string&, double, uint64_t, uint64_t) override {}
    void record_outbound(const std::string&, const OutboundTimings&) override {}
};

}  // namespace

std::shared_ptr<IMetricsPublisher> noop_metrics_publisher() {
    static auto instance = std::make_shared<NoopMetricsPublisher>();
    return instance;
}

std::shared_ptr<IPipelineTelemetry> noop_pipeline_telemetry() {
    static auto instance = std::make_shared<NoopPipelineTelemetry>();
    return instance;
}

}  // namespace voiceqas::ports
