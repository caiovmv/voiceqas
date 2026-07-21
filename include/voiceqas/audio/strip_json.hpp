#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "voiceqas/audio/config.hpp"

namespace voiceqas::audio {

inline void merge_strip_json(ChannelStripConfig& strip, const nlohmann::json& j) {
    if (!j.is_object()) {
        return;
    }
    if (j.contains("nr") && j["nr"].is_object()) {
        const auto& n = j["nr"];
        if (n.contains("enabled")) strip.nr.enabled = n["enabled"].get<bool>();
        if (n.contains("wet_dry")) strip.nr.wet_dry = n["wet_dry"].get<double>();
    }
    if (j.contains("hpf") && j["hpf"].is_object()) {
        const auto& h = j["hpf"];
        if (h.contains("enabled")) strip.hpf.enabled = h["enabled"].get<bool>();
        if (h.contains("cutoff_hz")) strip.hpf.cutoff_hz = h["cutoff_hz"].get<double>();
    }
    if (j.contains("eq") && j["eq"].is_object()) {
        const auto& e = j["eq"];
        if (e.contains("enabled")) strip.eq.enabled = e["enabled"].get<bool>();
        if (e.contains("bands") && e["bands"].is_array()) {
            strip.eq.bands.clear();
            for (const auto& b : e["bands"]) {
                EqBandConfig band;
                if (b.contains("freq_hz")) band.freq_hz = b["freq_hz"].get<double>();
                if (b.contains("gain_db")) band.gain_db = b["gain_db"].get<double>();
                if (b.contains("q")) band.q = b["q"].get<double>();
                strip.eq.bands.push_back(band);
            }
        }
    }
    if (j.contains("deesser") && j["deesser"].is_object()) {
        const auto& d = j["deesser"];
        if (d.contains("enabled")) strip.deesser.enabled = d["enabled"].get<bool>();
        if (d.contains("center_hz")) strip.deesser.center_hz = d["center_hz"].get<double>();
        if (d.contains("bandwidth_hz")) strip.deesser.bandwidth_hz = d["bandwidth_hz"].get<double>();
        if (d.contains("threshold_db")) strip.deesser.threshold_db = d["threshold_db"].get<double>();
        if (d.contains("ratio")) strip.deesser.ratio = d["ratio"].get<double>();
        if (d.contains("attack_ms")) strip.deesser.attack_ms = d["attack_ms"].get<double>();
        if (d.contains("release_ms")) strip.deesser.release_ms = d["release_ms"].get<double>();
    }
    if (j.contains("compressor") && j["compressor"].is_object()) {
        const auto& c = j["compressor"];
        if (c.contains("enabled")) strip.compressor.enabled = c["enabled"].get<bool>();
        if (c.contains("threshold_db")) strip.compressor.threshold_db = c["threshold_db"].get<double>();
        if (c.contains("ratio")) strip.compressor.ratio = c["ratio"].get<double>();
        if (c.contains("attack_ms")) strip.compressor.attack_ms = c["attack_ms"].get<double>();
        if (c.contains("release_ms")) strip.compressor.release_ms = c["release_ms"].get<double>();
        if (c.contains("makeup_db")) strip.compressor.makeup_db = c["makeup_db"].get<double>();
    }
    if (j.contains("limiter") && j["limiter"].is_object()) {
        const auto& l = j["limiter"];
        if (l.contains("enabled")) strip.limiter.enabled = l["enabled"].get<bool>();
        if (l.contains("ceiling_dbfs")) strip.limiter.ceiling_dbfs = l["ceiling_dbfs"].get<double>();
    }
    if (j.contains("agc") && j["agc"].is_object()) {
        const auto& a = j["agc"];
        if (a.contains("enabled")) strip.agc.enabled = a["enabled"].get<bool>();
        if (a.contains("target_rms_dbfs")) strip.agc.target_rms_dbfs = a["target_rms_dbfs"].get<double>();
        if (a.contains("max_gain_db")) strip.agc.max_gain_db = a["max_gain_db"].get<double>();
        if (a.contains("attack_ms")) strip.agc.attack_ms = a["attack_ms"].get<double>();
        if (a.contains("release_ms")) strip.agc.release_ms = a["release_ms"].get<double>();
    }
}

inline bool merge_strip_json_string(ChannelStripConfig& strip, const std::string& raw) {
    if (raw.empty()) {
        return false;
    }
    try {
        merge_strip_json(strip, nlohmann::json::parse(raw));
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace voiceqas::audio
