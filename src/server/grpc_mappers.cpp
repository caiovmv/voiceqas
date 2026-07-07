#include "voiceqas/server/grpc_mappers.hpp"

namespace voiceqas::server {
namespace {

struct FormatProtoPair {
    AudioFormat format;
    voiceqas::v1::AudioFormat proto;
};

constexpr FormatProtoPair kFormatPairs[] = {
    {AudioFormat::PcmS16Le8k, voiceqas::v1::PCM_S16LE_8K},
    {AudioFormat::PcmS16Le16k, voiceqas::v1::PCM_S16LE_16K},
    {AudioFormat::RtpPcmu, voiceqas::v1::RTP_PCMU},
    {AudioFormat::RtpPcma, voiceqas::v1::RTP_PCMA},
    {AudioFormat::RtpG722, voiceqas::v1::RTP_G722},
    {AudioFormat::RtpG729, voiceqas::v1::RTP_G729},
};

}  // namespace

AudioFormat audio_format_from_proto(voiceqas::v1::AudioFormat format) {
    for (const auto& pair : kFormatPairs) {
        if (pair.proto == format) {
            return pair.format;
        }
    }
    return AudioFormat::PcmS16Le8k;
}

voiceqas::v1::AudioFormat audio_format_to_proto(AudioFormat format) {
    for (const auto& pair : kFormatPairs) {
        if (pair.format == format) {
            return pair.proto;
        }
    }
    return voiceqas::v1::AUDIO_FORMAT_UNSPECIFIED;
}

void fill_metrics(const WindowMetrics& src, voiceqas::v1::Metrics* dst) {
    dst->set_rms_dbfs(src.rms_dbfs);
    dst->set_peak_dbfs(src.peak_dbfs);
    dst->set_clipping_ratio(src.clipping_ratio);
    dst->set_snr_estimate_db(src.snr_estimate_db);
    dst->set_silence_ratio(src.silence_ratio);
    dst->set_spectral_flatness(src.spectral_flatness);
    dst->set_packet_loss_pct(src.packet_loss_pct);
    dst->set_jitter_ms(src.jitter_ms);
}

voiceqas::v1::QualityReport quality_report_to_proto(const std::string& session_id, const WindowMetrics& metrics) {
    voiceqas::v1::QualityReport report;
    report.set_session_id(session_id);
    report.set_window_start_ms(metrics.window_start_ms);
    report.set_composite_score(metrics.composite_score);
    report.set_stt_ready(metrics.stt_ready);
    fill_metrics(metrics, report.mutable_metrics());
    return report;
}

void fill_stt_response(const stt::TranscriptResult& src, voiceqas::v1::SttTranscribeResponse* dst) {
    dst->set_text(src.text);
    dst->set_model(src.model);
    dst->set_language(src.language);
    dst->set_duration_ms(src.duration_ms);
    dst->set_processing_ms(src.processing_ms);
    if (!src.error.empty()) {
        dst->set_error(src.error);
    }
    for (const auto& seg : src.segments) {
        auto* out = dst->add_segments();
        out->set_start_ms(seg.start_ms);
        out->set_end_ms(seg.end_ms);
        out->set_text(seg.text);
    }
}

void fill_stt_event(
    const std::string& session_id,
    voiceqas::v1::SttEventType type,
    const stt::TranscriptResult& src,
    voiceqas::v1::SttTranscriptEvent* dst) {
    dst->set_session_id(session_id);
    dst->set_type(type);
    dst->set_text(src.text);
    dst->set_model(src.model);
    dst->set_duration_ms(src.duration_ms);
    dst->set_processing_ms(src.processing_ms);
    if (!src.error.empty()) {
        dst->set_error(src.error);
    }
    for (const auto& seg : src.segments) {
        auto* out = dst->add_segments();
        out->set_start_ms(seg.start_ms);
        out->set_end_ms(seg.end_ms);
        out->set_text(seg.text);
    }
}

}  // namespace voiceqas::server
