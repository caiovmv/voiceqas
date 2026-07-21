#pragma once

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "voiceqas/ops/pipeline_tracker.hpp"

namespace voiceqas::ops {

struct SankeyNodeDef {
    std::string name;
    std::string label;
    const char* direction;
};

struct SankeyLinkDef {
    std::string source;
    std::string target;
};

const std::vector<SankeyNodeDef>& sankey_node_defs();
const std::vector<SankeyLinkDef>& sankey_link_defs();

nlohmann::json stage_metrics_to_json(const PipelineStageMetrics& metrics);
nlohmann::json build_sankey_json(
    const std::unordered_map<std::string, PipelineStageMetrics>& stages_by_name,
    const std::vector<SankeyLinkDef>& links,
    const std::vector<SankeyNodeDef>& nodes);

nlohmann::json build_echarts_sankey_option(
    const nlohmann::json& sankey_nodes,
    const nlohmann::json& sankey_links);

}  // namespace voiceqas::ops
