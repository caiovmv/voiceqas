#pragma once

#include <memory>
#include <string>

#include "voiceqas/metrics.hpp"
#include "voiceqas/stt/client.hpp"

namespace voiceqas::ports {

class IMetricsPublisher {
public:
    virtual ~IMetricsPublisher() = default;

    virtual void publish_vqa(const std::string& session_id, const WindowMetrics& metrics) = 0;
    virtual void publish_stt(
        const std::string& session_id,
        const stt::TranscriptResult& result,
        bool partial) = 0;
};

std::shared_ptr<IMetricsPublisher> noop_metrics_publisher();

}  // namespace voiceqas::ports
