#pragma once

#include <string>
#include <vector>

#include "voiceqas/metrics.hpp"
#include "voiceqas/ports/metrics_publisher.hpp"
#include "voiceqas/ports/pipeline_telemetry.hpp"
#include "voiceqas/stt/client.hpp"

namespace voiceqas::test {

class MockMetricsPublisher final : public ports::IMetricsPublisher {
public:
    std::vector<std::string> vqa_sessions;
    std::vector<std::string> stt_sessions;

    void publish_vqa(const std::string& session_id, const WindowMetrics&) override {
        vqa_sessions.push_back(session_id);
    }

    void publish_stt(const std::string& session_id, const stt::TranscriptResult&, bool) override {
        stt_sessions.push_back(session_id);
    }
};

class MockPipelineTelemetry final : public ports::IPipelineTelemetry {
public:
    uint64_t vqa_path_calls = 0;
    uint64_t rtp_ingress_calls = 0;
    uint64_t gate_drop_calls = 0;

    void record_rtp_ingress(
        const std::string&,
        uint64_t,
        double,
        double,
        bool) override {
        ++rtp_ingress_calls;
    }

    void record_vqa_path(
        const std::string&,
        uint64_t,
        uint64_t,
        double,
        double,
        double,
        double) override {
        ++vqa_path_calls;
    }

    void record_vqa_window(const std::string&, const WindowMetrics&) override {}
    void record_stt_gate_drop(const std::string&, uint64_t) override { ++gate_drop_calls; }
    void record_stt_prepare(const std::string&, const ports::SttPrepareTimings&) override {}
    void record_stt_buffer(const std::string&, int, size_t) override {}
    void set_session_codec(const std::string&, const std::string&) override {}
    void finish_session(const std::string&, const std::string&) override {}
    void remove_session(const std::string&) override {}
    void on_session_removed(const std::string&) override {}
    void record_vad(const std::string&, double, uint64_t, uint64_t) override {}
    void record_outbound(const std::string&, const ports::OutboundTimings&) override {}
};

}  // namespace voiceqas::test
