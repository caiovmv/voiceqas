#include <gtest/gtest.h>

#include "test_helpers.hpp"
#include "voiceqas/audio/decoder.hpp"
#include "voiceqas/audio/encoder.hpp"
#include "voiceqas/audio/normalizer.hpp"
#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/rtp/g711_codec.hpp"

namespace voiceqas {
namespace {

TEST(AudioPipelineTest, DecodeEncodeRoundTripG711) {
    const auto pcm = test::make_sine(8000, 440.0, 0.05);
    const auto encoded = audio::default_codec_registry().encode_pcm(AudioFormat::RtpPcmu, pcm);
    rtp::RtpDepacketizer depacketizer(8000);
    const auto decoded = audio::decode_to_pcm(AudioFormat::RtpPcmu, encoded, &depacketizer);
    ASSERT_TRUE(decoded.ok);
    ASSERT_EQ(decoded.pcm.size(), pcm.size());
}

TEST(AudioPipelineTest, AgcDoesNotCrashOnSilence) {
    audio::AudioProcessingConfig cfg;
    cfg.normalize_enabled = true;
    audio::AgcState agc(cfg);
    auto silence = std::vector<int16_t>(160, 0);
    agc.process_inplace(silence, 8000);
    SUCCEED();
}

}  // namespace
}  // namespace voiceqas
