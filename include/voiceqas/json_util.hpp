#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "voiceqas/metrics.hpp"

namespace voiceqas {

nlohmann::json window_metrics_to_json(const WindowMetrics& m);
nlohmann::json batch_result_to_json(const BatchResult& r);

}  // namespace voiceqas
