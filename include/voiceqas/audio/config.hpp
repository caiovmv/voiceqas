#pragma once

#include <string>
#include <vector>

namespace voiceqas::audio {

struct EqBandConfig {
    double freq_hz = 250.0;
    double gain_db = 0.0;
    double q = 1.0;
};

struct NrStageConfig {
    bool enabled = false;
    double wet_dry = 1.0;
};

struct HpfStageConfig {
    bool enabled = true;
    double cutoff_hz = 80.0;
};

struct EqStageConfig {
    bool enabled = true;
    std::vector<EqBandConfig> bands = {
        {250.0, -1.5, 1.0},
        {450.0, -1.0, 1.2},
        {2500.0, 1.0, 0.8},
        {3500.0, 1.0, 0.8},
    };
};

struct DeesserStageConfig {
    bool enabled = false;
    double center_hz = 6500.0;
    double bandwidth_hz = 2000.0;
    double threshold_db = -25.0;
    double ratio = 3.0;
    double attack_ms = 1.0;
    double release_ms = 40.0;
};

struct CompressorStageConfig {
    bool enabled = true;
    double threshold_db = -20.0;
    double ratio = 2.0;
    double attack_ms = 5.0;
    double release_ms = 80.0;
    double makeup_db = 0.0;
};

struct LimiterStageConfig {
    bool enabled = true;
    double ceiling_dbfs = -1.0;
};

struct AgcStageConfig {
    bool enabled = true;
    double target_rms_dbfs = -18.0;
    double max_gain_db = 24.0;
    double attack_ms = 5.0;
    double release_ms = 100.0;
};

struct ChannelStripConfig {
    NrStageConfig nr;
    HpfStageConfig hpf;
    EqStageConfig eq;
    DeesserStageConfig deesser;
    CompressorStageConfig compressor;
    LimiterStageConfig limiter;
    AgcStageConfig agc;
};

struct EnhancementConfig {
    bool enabled = false;
    /// Only "rnnoise" in this phase; ignored when VOICEQAS_HAS_RNNOISE is off.
    std::string backend = "rnnoise";
    /// 0 = dry, 1 = full denoise.
    double wet_dry = 1.0;
};

struct AudioProcessingConfig {
    /// Legacy mirrors of strip.agc / strip.nr / strip.limiter (kept for callers & YAML).
    bool normalize_enabled = true;
    double agc_target_rms_dbfs = -18.0;
    double agc_max_gain_db = 24.0;
    double agc_attack_ms = 5.0;
    double agc_release_ms = 100.0;
    double limiter_ceiling_dbfs = -1.0;
    EnhancementConfig enhancement;
    ChannelStripConfig strip;

    /** Copy strip stages into legacy flat fields (and enhancement). */
    void sync_legacy_from_strip();
    /** Copy legacy flat fields into strip (when strip YAML omitted / bool headers). */
    void sync_strip_from_legacy();
};

inline void AudioProcessingConfig::sync_legacy_from_strip() {
    normalize_enabled = strip.agc.enabled;
    agc_target_rms_dbfs = strip.agc.target_rms_dbfs;
    agc_max_gain_db = strip.agc.max_gain_db;
    agc_attack_ms = strip.agc.attack_ms;
    agc_release_ms = strip.agc.release_ms;
    limiter_ceiling_dbfs = strip.limiter.ceiling_dbfs;
    enhancement.enabled = strip.nr.enabled;
    enhancement.wet_dry = strip.nr.wet_dry;
}

inline void AudioProcessingConfig::sync_strip_from_legacy() {
    strip.agc.enabled = normalize_enabled;
    strip.agc.target_rms_dbfs = agc_target_rms_dbfs;
    strip.agc.max_gain_db = agc_max_gain_db;
    strip.agc.attack_ms = agc_attack_ms;
    strip.agc.release_ms = agc_release_ms;
    strip.limiter.ceiling_dbfs = limiter_ceiling_dbfs;
    strip.nr.enabled = enhancement.enabled;
    strip.nr.wet_dry = enhancement.wet_dry;
}

struct MediaRelayConfig {
    std::string rtp_addr = "0.0.0.0:10000";
    bool enabled = true;
    /// Codec RTP preferido na entrada (wideband). Autodetect continua ativo via PT.
    std::string preferred_ingress_codec = "rtp_g722";
    bool ingress_autodetect = true;
};

}  // namespace voiceqas::audio
