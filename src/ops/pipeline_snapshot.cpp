#include "voiceqas/ops/pipeline_snapshot.hpp"

#include <algorithm>

namespace voiceqas::ops {

const std::vector<SankeyNodeDef>& sankey_node_defs() {
    static const std::vector<SankeyNodeDef> nodes = {
        {pipeline_stage::kSipIn, "SIP Trunk (in)", kPipelineDirectionInbound},
        {pipeline_stage::kRtpIngress, "RTP Ingress", kPipelineDirectionInbound},
        {pipeline_stage::kDecodeVqa, "Decode VQA", kPipelineDirectionInbound},
        {pipeline_stage::kAgcVqa, "AGC Normalize", kPipelineDirectionInbound},
        {pipeline_stage::kEnhancement, "Audio Enhancement", kPipelineDirectionInbound},
        {pipeline_stage::kVqa, "VQA Analyzer", kPipelineDirectionInbound},
        {pipeline_stage::kSttGate, "STT Gate", kPipelineDirectionInbound},
        {pipeline_stage::kDecodeStt, "Decode STT", kPipelineDirectionInbound},
        {pipeline_stage::kAgcStt, "AGC STT", kPipelineDirectionInbound},
        {pipeline_stage::kResample16k, "Resample 16 kHz", kPipelineDirectionInbound},
        {pipeline_stage::kSttBuffer, "STT Buffer", kPipelineDirectionInbound},
        {pipeline_stage::kVad, "Silero VAD", kPipelineDirectionInbound},
        {pipeline_stage::kAsr, "ASR", kPipelineDirectionInbound},
        {pipeline_stage::kAiAgent, "AI Agent", kPipelineDirectionInbound},
        {pipeline_stage::kSttDropped, "STT Dropped", kPipelineDirectionInbound},
        {pipeline_stage::kAgentPcmIn, "Agent PCM In", kPipelineDirectionOutbound},
        {pipeline_stage::kResampleOut, "Resample Out", kPipelineDirectionOutbound},
        {pipeline_stage::kPeakLimit, "Peak Limiter", kPipelineDirectionOutbound},
        {pipeline_stage::kEncode, "Encode", kPipelineDirectionOutbound},
        {pipeline_stage::kRtpPacketize, "RTP Packetize", kPipelineDirectionOutbound},
        {pipeline_stage::kRtpEgress, "RTP Egress", kPipelineDirectionOutbound},
        {pipeline_stage::kSipOut, "SIP Trunk (out)", kPipelineDirectionOutbound},
    };
    return nodes;
}

const std::vector<SankeyLinkDef>& sankey_link_defs() {
    static const std::vector<SankeyLinkDef> links = {
        {pipeline_stage::kSipIn, pipeline_stage::kRtpIngress},
        {pipeline_stage::kRtpIngress, pipeline_stage::kDecodeVqa},
        {pipeline_stage::kRtpIngress, pipeline_stage::kDecodeStt},
        {pipeline_stage::kRtpIngress, pipeline_stage::kSttDropped},
        {pipeline_stage::kDecodeVqa, pipeline_stage::kAgcVqa},
        {pipeline_stage::kAgcVqa, pipeline_stage::kEnhancement},
        {pipeline_stage::kEnhancement, pipeline_stage::kVqa},
        {pipeline_stage::kVqa, pipeline_stage::kSttGate},
        {pipeline_stage::kSttGate, pipeline_stage::kDecodeStt},
        {pipeline_stage::kDecodeStt, pipeline_stage::kAgcStt},
        {pipeline_stage::kAgcStt, pipeline_stage::kResample16k},
        {pipeline_stage::kResample16k, pipeline_stage::kSttBuffer},
        {pipeline_stage::kSttBuffer, pipeline_stage::kVad},
        {pipeline_stage::kVad, pipeline_stage::kAsr},
        {pipeline_stage::kAsr, pipeline_stage::kAiAgent},
        {pipeline_stage::kAiAgent, pipeline_stage::kAgentPcmIn},
        {pipeline_stage::kAgentPcmIn, pipeline_stage::kResampleOut},
        {pipeline_stage::kResampleOut, pipeline_stage::kPeakLimit},
        {pipeline_stage::kPeakLimit, pipeline_stage::kEncode},
        {pipeline_stage::kEncode, pipeline_stage::kRtpPacketize},
        {pipeline_stage::kRtpPacketize, pipeline_stage::kRtpEgress},
        {pipeline_stage::kRtpEgress, pipeline_stage::kSipOut},
    };
    return links;
}

nlohmann::json stage_metrics_to_json(const PipelineStageMetrics& metrics) {
    return nlohmann::json{
        {"bytes_in", metrics.bytes_in},
        {"bytes_out", metrics.bytes_out},
        {"packets_in", metrics.packets_in},
        {"packets_out", metrics.packets_out},
        {"dropped_bytes", metrics.dropped_bytes},
        {"dropped_packets", metrics.dropped_packets},
        {"latency_ms_p50", metrics.latency_ms_p50},
        {"latency_ms_p95", metrics.latency_ms_p95},
        {"bytes_per_sec", metrics.bytes_per_sec},
        {"jitter_ms", metrics.jitter_ms},
        {"packet_loss_pct", metrics.packet_loss_pct},
        {"composite_score", metrics.composite_score},
        {"snr_db", metrics.snr_db},
        {"rms_dbfs", metrics.rms_dbfs},
        {"stt_ready", metrics.stt_ready},
        {"processing_ms", metrics.processing_ms},
        {"buffer_ms", metrics.buffer_ms},
        {"enabled", metrics.enabled},
    };
}

static uint64_t link_value_for(
    const std::string& source,
    const std::string& target,
    const std::unordered_map<std::string, PipelineStageMetrics>& stages) {
    const auto source_it = stages.find(source);
    if (source_it == stages.end()) {
        return 0;
    }
    if (source == pipeline_stage::kRtpIngress && target == pipeline_stage::kSttDropped) {
        const auto gate_it = stages.find(pipeline_stage::kSttGate);
        if (gate_it != stages.end() && gate_it->second.dropped_bytes > 0) {
            return gate_it->second.dropped_bytes;
        }
        return source_it->second.dropped_bytes;
    }
    if (source_it->second.bytes_out > 0) {
        return source_it->second.bytes_out;
    }
    const auto target_it = stages.find(target);
    if (target_it != stages.end() && target_it->second.bytes_in > 0) {
        return target_it->second.bytes_in;
    }
    return source_it->second.bytes_in;
}

nlohmann::json build_sankey_json(
    const std::unordered_map<std::string, PipelineStageMetrics>& stages_by_name,
    const std::vector<SankeyLinkDef>& links,
    const std::vector<SankeyNodeDef>& nodes) {
    nlohmann::json node_array = nlohmann::json::array();
    for (const auto& node : nodes) {
        const auto it = stages_by_name.find(node.name);
        const auto& metrics = it != stages_by_name.end() ? it->second : PipelineStageMetrics{};
        node_array.push_back({
            {"name", node.name},
            {"label", node.label},
            {"direction", node.direction},
            {"metrics", stage_metrics_to_json(metrics)},
        });
    }

    nlohmann::json link_array = nlohmann::json::array();
    for (const auto& link : links) {
        const auto value = link_value_for(link.source, link.target, stages_by_name);
        if (value == 0) {
            continue;
        }
        const auto source_it = stages_by_name.find(link.source);
        nlohmann::json link_metrics = nlohmann::json::object();
        if (source_it != stages_by_name.end()) {
            link_metrics = {
                {"latency_ms_p50", source_it->second.latency_ms_p50},
                {"latency_ms_p95", source_it->second.latency_ms_p95},
                {"jitter_ms", source_it->second.jitter_ms},
                {"packet_loss_pct", source_it->second.packet_loss_pct},
                {"composite_score", source_it->second.composite_score},
                {"bytes_per_sec", source_it->second.bytes_per_sec},
            };
        }
        link_array.push_back({
            {"source", link.source},
            {"target", link.target},
            {"value", value},
            {"metrics", std::move(link_metrics)},
        });
    }

    return {
        {"nodes", std::move(node_array)},
        {"links", std::move(link_array)},
    };
}

nlohmann::json build_echarts_sankey_option(
    const nlohmann::json& sankey_nodes,
    const nlohmann::json& sankey_links) {
    nlohmann::json echarts_nodes = nlohmann::json::array();
    for (const auto& node : sankey_nodes) {
        const auto& metrics = node.value("metrics", nlohmann::json::object());
        const auto score = metrics.value("composite_score", 0.0);
        echarts_nodes.push_back({
            {"name", node.value("name", "")},
            {"value", metrics.value("bytes_out", metrics.value("bytes_in", 1))},
            {"itemStyle",
             {{"color",
               score > 0.0 ? (score >= 65.0 ? "#3fb950" : (score >= 45.0 ? "#d29922" : "#f85149"))
                           : (node.value("direction", "") == "outbound" ? "#58a6ff" : "#8b949e")}}},
            {"label", node.value("label", node.value("name", ""))},
        });
    }

    nlohmann::json echarts_links = nlohmann::json::array();
    for (const auto& link : sankey_links) {
        echarts_links.push_back({
            {"source", link.value("source", "")},
            {"target", link.value("target", "")},
            {"value", link.value("value", 1)},
        });
    }

    return {
        {"tooltip", {{"trigger", "item"}}},
        {"series",
         {{{"type", "sankey"},
           {"emphasis", {{"focus", "adjacency"}}},
           {"lineStyle", {{"color", "gradient"}, {"curveness", 0.5}}},
           {"data", std::move(echarts_nodes)},
           {"links", std::move(echarts_links)}}}},
    };
}

}  // namespace voiceqas::ops
