#include "voiceqas/ops/transport_sankey.hpp"

#include "voiceqas/ops/pipeline_snapshot.hpp"

namespace voiceqas::ops {

namespace {

uint64_t flow_value(const PipelineStageMetrics& metrics) {
    if (metrics.bytes_per_sec > 0.0) {
        return static_cast<uint64_t>(metrics.bytes_per_sec);
    }
    if (metrics.bytes_out > 0) {
        return metrics.bytes_out;
    }
    return metrics.bytes_in;
}

nlohmann::json node_row(
    const char* name,
    const char* label,
    const char* direction,
    const PipelineStageMetrics& metrics) {
    return {
        {"name", name},
        {"label", label},
        {"direction", direction},
        {"metrics", stage_metrics_to_json(metrics)},
    };
}

void add_link(
    nlohmann::json& links,
    const char* source,
    const char* target,
    const PipelineStageMetrics& source_metrics) {
    const auto value = flow_value(source_metrics);
    if (value == 0) {
        return;
    }
    links.push_back({
        {"source", source},
        {"target", target},
        {"value", value},
        {"metrics",
         {
             {"latency_ms_p50", source_metrics.latency_ms_p50},
             {"latency_ms_p95", source_metrics.latency_ms_p95},
             {"jitter_ms", source_metrics.jitter_ms},
             {"bytes_per_sec", source_metrics.bytes_per_sec},
             {"processing_ms", source_metrics.processing_ms},
         }},
    });
}

}  // namespace

nlohmann::json build_transport_sankey(
    const std::unordered_map<std::string, PipelineStageMetrics>& transports,
    const char* direction) {
    const auto get = [&](const char* key) -> PipelineStageMetrics {
        const auto it = transports.find(key);
        return it != transports.end() ? it->second : PipelineStageMetrics{};
    };

    const auto sip = get(transport_node::kSipTrunk);
    const auto rest = get(transport_node::kRest);
    const auto ws = get(transport_node::kWebSocket);
    const auto grpc = get(transport_node::kGrpc);
    const auto media = get(transport_node::kMediaPipeline);
    const auto external = get(transport_node::kExternalAi);

    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json links = nlohmann::json::array();

    if (std::string(direction) == "inbound") {
        nodes.push_back(node_row(transport_node::kSipTrunk, "SIP Trunk", "inbound", sip));
        nodes.push_back(node_row(transport_node::kRest, "REST", "inbound", rest));
        nodes.push_back(node_row(transport_node::kWebSocket, "WebSocket", "inbound", ws));
        nodes.push_back(node_row(transport_node::kGrpc, "gRPC", "inbound", grpc));
        nodes.push_back(node_row(transport_node::kMediaPipeline, "Media Pipeline", "inbound", media));
        nodes.push_back(node_row(transport_node::kExternalAi, "AI Externa (Ollama)", "inbound", external));

        add_link(links, transport_node::kSipTrunk, transport_node::kMediaPipeline, sip);
        add_link(links, transport_node::kRest, transport_node::kMediaPipeline, rest);
        add_link(links, transport_node::kWebSocket, transport_node::kMediaPipeline, ws);
        add_link(links, transport_node::kGrpc, transport_node::kMediaPipeline, grpc);
        add_link(links, transport_node::kMediaPipeline, transport_node::kExternalAi, media);
    } else {
        nodes.push_back(node_row(transport_node::kExternalAi, "AI Externa (Ollama)", "outbound", external));
        nodes.push_back(node_row(transport_node::kMediaPipeline, "Media Pipeline", "outbound", media));
        nodes.push_back(node_row(transport_node::kSipTrunk, "SIP Trunk", "outbound", sip));
        nodes.push_back(node_row(transport_node::kRest, "REST", "outbound", rest));
        nodes.push_back(node_row(transport_node::kWebSocket, "WebSocket", "outbound", ws));
        nodes.push_back(node_row(transport_node::kGrpc, "gRPC", "outbound", grpc));

        add_link(links, transport_node::kExternalAi, transport_node::kMediaPipeline, external);
        add_link(links, transport_node::kMediaPipeline, transport_node::kSipTrunk, media);
        add_link(links, transport_node::kMediaPipeline, transport_node::kRest, media);
        add_link(links, transport_node::kMediaPipeline, transport_node::kWebSocket, media);
        add_link(links, transport_node::kMediaPipeline, transport_node::kGrpc, media);
    }

    return {
        {"direction", direction},
        {"nodes", std::move(nodes)},
        {"links", std::move(links)},
    };
}

nlohmann::json build_transport_sankey_pair(
    const std::unordered_map<std::string, PipelineStageMetrics>& transports) {
    auto inbound = build_transport_sankey(transports, "inbound");
    auto outbound = build_transport_sankey(transports, "outbound");
    inbound["echarts"] = build_echarts_sankey_option(inbound["nodes"], inbound["links"]);
    outbound["echarts"] = build_echarts_sankey_option(outbound["nodes"], outbound["links"]);
    return {
        {"inbound", std::move(inbound)},
        {"outbound", std::move(outbound)},
    };
}

}  // namespace voiceqas::ops
