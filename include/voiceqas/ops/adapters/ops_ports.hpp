#pragma once

#include <memory>

#include "voiceqas/ports/metrics_publisher.hpp"
#include "voiceqas/ports/pipeline_telemetry.hpp"

namespace voiceqas::ops {

std::shared_ptr<ports::IMetricsPublisher> make_ops_metrics_publisher();
std::shared_ptr<ports::IPipelineTelemetry> make_ops_pipeline_telemetry();

}  // namespace voiceqas::ops
