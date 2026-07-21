#pragma once

#include <string>

namespace voiceqas {

namespace ops {

struct OpsConfig {
    bool persistence_enabled = true;
    std::string storage_path = "/tmp/voiceqas-ops.jsonl";

    std::string webhook_url;
    double alert_score_threshold = 0.45;
    int stt_not_ready_alert_ms = 30000;

    std::string admin_token;
    std::string read_token;
    std::string write_token;

    int partial_stt_interval_ms = 3000;
    int partial_stt_window_ms = 5000;
    int partial_stt_min_buffer_ms = 3000;
};

}  // namespace ops

ops::OpsConfig& global_ops_config();

namespace ops {

inline OpsConfig& global_ops_config() {
    return ::voiceqas::global_ops_config();
}

}  // namespace ops

}  // namespace voiceqas
