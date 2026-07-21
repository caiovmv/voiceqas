#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace voiceqas::stt {

struct DiarizationConfig {
    bool enabled = true;
    bool focus_primary = true;
    std::string primary_mode = "first_then_max_presence";
    int min_turn_ms = 300;
};

struct SpeechTurn {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    double rms_dbfs = -96.0;
    int speaker_id = 0;
    bool is_primary = false;
    std::string text;
};

struct DiarizationResult {
    std::vector<SpeechTurn> turns;
    int primary_speaker = 0;
    std::vector<int16_t> primary_pcm;
    bool ok = false;
};

/**
 * Turn-taking diarization from Silero speech segments (mono: alternate speakers).
 * primary_mode first_then_max_presence: speakers alternate by turn order;
 * primary = max total duration, tie → first speaker (id 0).
 */
DiarizationResult pick_primary_turns(
    const std::vector<SpeechTurn>& raw_turns,
    std::span<const int16_t> pcm_16k,
    const DiarizationConfig& config);

std::vector<int16_t> concatenate_primary_pcm(
    std::span<const int16_t> pcm_16k,
    const std::vector<SpeechTurn>& turns,
    int primary_speaker);

}  // namespace voiceqas::stt
