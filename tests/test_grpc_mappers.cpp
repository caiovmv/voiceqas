#include <gtest/gtest.h>

#include "voiceqas/metrics.hpp"
#include "voiceqas/server/grpc_mappers.hpp"
#include "voice_quality.pb.h"

namespace voiceqas::server {
namespace {

TEST(GrpcMappersTest, RoundTripsAllAudioFormats) {
    const AudioFormat formats[] = {
        AudioFormat::PcmS16Le8k,
        AudioFormat::PcmS16Le16k,
        AudioFormat::RtpPcmu,
        AudioFormat::RtpPcma,
        AudioFormat::RtpG722,
        AudioFormat::RtpG729,
    };
    for (const auto format : formats) {
        const auto proto = audio_format_to_proto(format);
        EXPECT_NE(proto, voiceqas::v1::AUDIO_FORMAT_UNSPECIFIED);
        EXPECT_EQ(audio_format_from_proto(proto), format);
    }
}

TEST(GrpcMappersTest, UnknownProtoDefaultsToPcm8k) {
    EXPECT_EQ(audio_format_from_proto(voiceqas::v1::AUDIO_FORMAT_UNSPECIFIED), AudioFormat::PcmS16Le8k);
}

TEST(GrpcMappersTest, QualityReportMapsMetrics) {
    WindowMetrics metrics{};
    metrics.window_start_ms = 100;
    metrics.composite_score = 0.75;
    metrics.stt_ready = true;
    metrics.rms_dbfs = -18.0;
    metrics.peak_dbfs = -6.0;
    metrics.spectral_flatness = 0.3;

    const auto report = quality_report_to_proto("sess-1", metrics);
    EXPECT_EQ(report.session_id(), "sess-1");
    EXPECT_EQ(report.window_start_ms(), 100);
    EXPECT_DOUBLE_EQ(report.composite_score(), 0.75);
    EXPECT_TRUE(report.stt_ready());
    EXPECT_DOUBLE_EQ(report.metrics().rms_dbfs(), -18.0);
    EXPECT_DOUBLE_EQ(report.metrics().spectral_flatness(), 0.3);
}

TEST(GrpcMappersTest, SttResponseCopiesSegments) {
    stt::TranscriptResult src;
    src.text = "olá";
    src.model = "auto";
    src.language = "pt";
    src.duration_ms = 1200;
    src.processing_ms = 80;
    src.segments.push_back({.start_ms = 0, .end_ms = 500, .text = "olá"});

    voiceqas::v1::SttTranscribeResponse dst;
    fill_stt_response(src, &dst);
    EXPECT_EQ(dst.text(), "olá");
    ASSERT_EQ(dst.segments_size(), 1);
    EXPECT_EQ(dst.segments(0).text(), "olá");
}

}  // namespace
}  // namespace voiceqas::server
