#pragma once

#include <string>

namespace voiceqas::audio {

struct AudioProcessingConfig {
    bool normalize_enabled = true;
    double agc_target_rms_dbfs = -20.0;
    double agc_max_gain_db = 24.0;
    double agc_attack_ms = 5.0;
    double agc_release_ms = 100.0;
    double limiter_ceiling_dbfs = -3.0;
};

struct MediaRelayConfig {
    std::string rtp_addr = "0.0.0.0:10000";
    bool enabled = true;
    /// Codec RTP preferido na entrada (wideband). Autodetect continua ativo via PT.
    std::string preferred_ingress_codec = "rtp_g722";
    bool ingress_autodetect = true;
};

}  // namespace voiceqas::audio
