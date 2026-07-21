#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "voiceqas/audio/decoder.hpp"
#include "voiceqas/audio/encoder.hpp"
#include "voiceqas/audio/config.hpp"
#include "voiceqas/audio/strip_json.hpp"
#include "voiceqas/metrics.hpp"
#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/rtp/packetizer.hpp"

namespace voiceqas::routes {

inline bool is_fleet_session_filter(const std::string& session_id) {
    return session_id.empty() || session_id == "$__all" || session_id == "all" || session_id == "*";
}

inline std::optional<std::string> parse_pipeline_session_param(const httplib::Request& req) {
    if (!req.has_param("session_id")) {
        return std::nullopt;
    }
    const auto session_id = req.get_param_value("session_id");
    if (is_fleet_session_filter(session_id)) {
        return std::nullopt;
    }
    return session_id;
}

inline std::optional<std::string> telemetry_session_id_from_request(const httplib::Request& req) {
    const auto header = req.get_header_value("X-Session-Id");
    if (!header.empty()) {
        return header;
    }
    return std::nullopt;
}

inline std::optional<bool> bool_header(const httplib::Request& req, const char* name) {
    if (!req.has_header(name)) {
        return std::nullopt;
    }
    const auto v = req.get_header_value(name);
    if (v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "yes") {
        return true;
    }
    if (v == "0" || v == "false" || v == "FALSE" || v == "off" || v == "no") {
        return false;
    }
    return std::nullopt;
}

inline audio::AudioProcessingConfig audio_config_from_request(
    const httplib::Request& req,
    audio::AudioProcessingConfig base) {
    if (req.has_header("X-Audio-Strip")) {
        audio::merge_strip_json_string(base.strip, req.get_header_value("X-Audio-Strip"));
        base.sync_legacy_from_strip();
    }
    if (const auto v = bool_header(req, "X-Audio-AGC")) {
        base.normalize_enabled = *v;
        base.strip.agc.enabled = *v;
    }
    if (const auto v = bool_header(req, "X-Audio-Enhancement")) {
        base.enhancement.enabled = *v;
        base.strip.nr.enabled = *v;
    }
    base.sync_legacy_from_strip();
    return base;
}

inline AudioFormat format_from_header(const httplib::Request& req) {
    if (req.has_header("X-Audio-Format")) {
        return audio_format_from_string(req.get_header_value("X-Audio-Format"));
    }
    if (req.get_header_value("Content-Type").find("wav") != std::string::npos) {
        return AudioFormat::PcmS16Le8k;
    }
    return AudioFormat::PcmS16Le8k;
}

inline int sample_rate_from_header(const httplib::Request& req, int fallback) {
    if (req.has_header("X-Sample-Rate")) {
        return std::stoi(req.get_header_value("X-Sample-Rate"));
    }
    return fallback;
}

inline AudioFormat format_from_json(const nlohmann::json& body) {
    if (body.contains("format")) {
        if (body["format"].is_string()) {
            return audio_format_from_string(body["format"].get<std::string>());
        }
        const int v = body["format"].get<int>();
        switch (v) {
            case 1: return AudioFormat::PcmS16Le8k;
            case 2: return AudioFormat::PcmS16Le16k;
            case 3: return AudioFormat::RtpPcmu;
            case 4: return AudioFormat::RtpPcma;
            case 5: return AudioFormat::RtpG722;
            case 6: return AudioFormat::RtpG729;
            default: return AudioFormat::PcmS16Le8k;
        }
    }
    return AudioFormat::PcmS16Le8k;
}

inline std::vector<uint8_t> decode_payload_json(const nlohmann::json& body) {
    std::vector<uint8_t> payload;
    if (body.contains("pcm_bytes")) {
        for (const auto& v : body["pcm_bytes"]) {
            payload.push_back(static_cast<uint8_t>(v.get<int>()));
        }
        return payload;
    }
    if (body.contains("payload_bytes")) {
        for (const auto& v : body["payload_bytes"]) {
            payload.push_back(static_cast<uint8_t>(v.get<int>()));
        }
        return payload;
    }
    if (body.contains("payload_base64")) {
        const auto& b64 = body["payload_base64"].get<std::string>();
        payload.assign(b64.begin(), b64.end());
    }
    return payload;
}

inline std::string read_file_or_empty(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline nlohmann::json grpc_ready_json() {
    return {
        {"status", "ready"},
        {"service", "voiceqas"},
        {"note", "HTTP playground bridge for VoiceQualityService.Ready"},
    };
}

inline std::vector<int16_t> pcm_from_bytes(const std::vector<uint8_t>& bytes) {
    std::vector<int16_t> pcm(bytes.size() / 2);
    std::memcpy(pcm.data(), bytes.data(), bytes.size());
    return pcm;
}

inline std::vector<uint8_t> pcm_to_bytes(std::span<const int16_t> pcm) {
    std::vector<uint8_t> bytes(pcm.size() * 2);
    std::memcpy(bytes.data(), pcm.data(), bytes.size());
    return bytes;
}

inline std::vector<uint8_t> build_rtp_packet(uint16_t seq, uint32_t ts, uint8_t pt, std::span<const uint8_t> payload) {
    std::vector<uint8_t> packet(12 + payload.size());
    packet[0] = 0x80;
    packet[1] = pt & 0x7F;
    packet[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(seq & 0xFF);
    packet[4] = static_cast<uint8_t>((ts >> 24) & 0xFF);
    packet[5] = static_cast<uint8_t>((ts >> 16) & 0xFF);
    packet[6] = static_cast<uint8_t>((ts >> 8) & 0xFF);
    packet[7] = static_cast<uint8_t>(ts & 0xFF);
    packet[8] = 0x12;
    packet[9] = 0x34;
    packet[10] = 0x56;
    packet[11] = 0x78;
    std::memcpy(packet.data() + 12, payload.data(), payload.size());
    return packet;
}

inline nlohmann::json pack_rtp_frames(
    AudioFormat format,
    std::span<const int16_t> pcm,
    int frame_ms) {
    const int sample_rate = sample_rate_for_format(format);
    const auto pt = static_cast<uint8_t>(audio::payload_type_for_format(format));
    const auto ts_step = audio::rtp_timestamp_step_for_format(format, frame_ms);
    rtp::RtpPacketizer packetizer(pt, ts_step);

    const auto encoded = audio::encode_from_pcm(format, pcm, &packetizer, frame_ms);
    std::vector<nlohmann::json> frames;
    for (const auto& packet : encoded.rtp_packets) {
        nlohmann::json arr = nlohmann::json::array();
        for (uint8_t b : packet) {
            arr.push_back(b);
        }
        frames.push_back(std::move(arr));
    }

    return {
        {"frames", frames},
        {"sample_rate", sample_rate},
        {"format", static_cast<int>(format)},
    };
}

inline nlohmann::json decode_rtp_frames(AudioFormat format, const nlohmann::json& frames_json) {
    std::vector<int16_t> pcm;
    rtp::RtpDepacketizer depacketizer(rtp_clock_rate_for_format(format));
    rtp::PayloadType pt = rtp::PayloadType::Pcmu;
    switch (format) {
        case AudioFormat::RtpPcma: pt = rtp::PayloadType::Pcma; break;
        case AudioFormat::RtpG722: pt = rtp::PayloadType::G722; break;
        case AudioFormat::RtpG729: pt = rtp::PayloadType::G729; break;
        default: break;
    }
    for (const auto& frame : frames_json) {
        std::vector<uint8_t> packet;
        for (const auto& v : frame) {
            packet.push_back(static_cast<uint8_t>(v.get<int>()));
        }
        auto decoded = depacketizer.decode_packet(packet, pt);
        pcm.insert(pcm.end(), decoded.pcm.begin(), decoded.pcm.end());
    }
    const auto bytes = pcm_to_bytes(pcm);
    nlohmann::json arr = nlohmann::json::array();
    for (uint8_t b : bytes) {
        arr.push_back(b);
    }
    return {
        {"pcm_bytes", arr},
        {"sample_rate", sample_rate_for_format(format)},
    };
}

}  // namespace voiceqas::routes
