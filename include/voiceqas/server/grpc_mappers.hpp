#pragma once

#include <string>

#include "voiceqas/metrics.hpp"
#include "voiceqas/stt/client.hpp"
#include "stt.pb.h"
#include "voice_quality.pb.h"

namespace voiceqas::server {

AudioFormat audio_format_from_proto(voiceqas::v1::AudioFormat format);
voiceqas::v1::AudioFormat audio_format_to_proto(AudioFormat format);

void fill_metrics(const WindowMetrics& src, voiceqas::v1::Metrics* dst);
voiceqas::v1::QualityReport quality_report_to_proto(const std::string& session_id, const WindowMetrics& metrics);

void fill_stt_response(const stt::TranscriptResult& src, voiceqas::v1::SttTranscribeResponse* dst);
void fill_stt_event(
    const std::string& session_id,
    voiceqas::v1::SttEventType type,
    const stt::TranscriptResult& src,
    voiceqas::v1::SttTranscriptEvent* dst);

}  // namespace voiceqas::server
