#pragma once

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "voiceqas/ops/pipeline_tracker.hpp"

namespace voiceqas::ops {

namespace transport_node {

inline constexpr const char* kSipTrunk = "sip_trunk";
inline constexpr const char* kRest = "rest";
inline constexpr const char* kWebSocket = "websocket";
inline constexpr const char* kGrpc = "grpc";
inline constexpr const char* kMediaPipeline = "media_pipeline";
inline constexpr const char* kExternalAi = "external_ai";

}  // namespace transport_node

nlohmann::json build_transport_sankey(
    const std::unordered_map<std::string, PipelineStageMetrics>& transports,
    const char* direction);

nlohmann::json build_transport_sankey_pair(
    const std::unordered_map<std::string, PipelineStageMetrics>& transports);

}  // namespace voiceqas::ops