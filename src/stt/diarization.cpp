#include "voiceqas/stt/diarization.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace voiceqas::stt {
namespace {

constexpr double kMinDb = -96.0;
constexpr int kEnergyHopMs = 20;
constexpr int kMinPieceMs = 350;
constexpr int kSplitLongerThanMs = 1800;

double rms_dbfs(std::span<const int16_t> samples) {
    if (samples.empty()) {
        return kMinDb;
    }
    double sum = 0.0;
    for (int16_t s : samples) {
        const double v = static_cast<double>(s);
        sum += v * v;
    }
    const double rms = std::sqrt(sum / static_cast<double>(samples.size()));
    if (rms <= 1e-10) {
        return kMinDb;
    }
    return std::max(kMinDb, 20.0 * std::log10(rms / 32768.0));
}

/** Split long VAD segments at energy valleys so mono turn-taking can invent S0/S1. */
std::vector<SpeechTurn> maybe_split_long_turns(
    const std::vector<SpeechTurn>& raw,
    std::span<const int16_t> pcm_16k) {
    std::vector<SpeechTurn> out;
    out.reserve(raw.size() * 2);
    const size_t hop = static_cast<size_t>(16000 * kEnergyHopMs / 1000);  // 320

    for (const auto& t : raw) {
        const auto dur = t.end_ms - t.start_ms;
        if (dur < kSplitLongerThanMs || pcm_16k.empty()) {
            out.push_back(t);
            continue;
        }

        const auto start = static_cast<size_t>(std::max<int64_t>(0, t.start_ms) * 16);
        const auto end = static_cast<size_t>(std::max<int64_t>(0, t.end_ms) * 16);
        if (start >= pcm_16k.size() || end <= start) {
            out.push_back(t);
            continue;
        }
        const auto clipped = std::min(end, pcm_16k.size());

        // Collect frame energies.
        std::vector<double> energies;
        std::vector<int64_t> frame_ms;
        for (size_t i = start; i + hop <= clipped; i += hop) {
            energies.push_back(rms_dbfs(pcm_16k.subspan(i, hop)));
            frame_ms.push_back(static_cast<int64_t>(i / 16));
        }
        if (energies.size() < 8) {
            out.push_back(t);
            continue;
        }

        double mean = 0.0;
        for (double e : energies) {
            mean += e;
        }
        mean /= static_cast<double>(energies.size());
        const double valley = mean - 8.0;  // dB below mean ≈ silence pocket

        std::vector<int64_t> cuts;
        cuts.push_back(t.start_ms);
        int64_t last_cut = t.start_ms;
        for (size_t i = 2; i + 2 < energies.size(); ++i) {
            const bool local_min =
                energies[i] <= energies[i - 1] && energies[i] <= energies[i + 1];
            if (!local_min || energies[i] > valley) {
                continue;
            }
            const int64_t at = frame_ms[i];
            if (at - last_cut < kMinPieceMs) {
                continue;
            }
            if (t.end_ms - at < kMinPieceMs) {
                continue;
            }
            cuts.push_back(at);
            last_cut = at;
        }
        cuts.push_back(t.end_ms);

        if (cuts.size() <= 2) {
            out.push_back(t);
            continue;
        }
        for (size_t i = 0; i + 1 < cuts.size(); ++i) {
            SpeechTurn piece = t;
            piece.start_ms = cuts[i];
            piece.end_ms = cuts[i + 1];
            out.push_back(piece);
        }
    }
    return out;
}

}  // namespace

std::vector<int16_t> concatenate_primary_pcm(
    std::span<const int16_t> pcm_16k,
    const std::vector<SpeechTurn>& turns,
    int primary_speaker) {
    std::vector<int16_t> out;
    for (const auto& turn : turns) {
        if (turn.speaker_id != primary_speaker) {
            continue;
        }
        const auto start = static_cast<size_t>(std::max<int64_t>(0, turn.start_ms) * 16);
        const auto end = static_cast<size_t>(std::max<int64_t>(0, turn.end_ms) * 16);
        if (start >= pcm_16k.size() || end <= start) {
            continue;
        }
        const auto clipped_end = std::min(end, pcm_16k.size());
        out.insert(out.end(), pcm_16k.begin() + static_cast<std::ptrdiff_t>(start),
                   pcm_16k.begin() + static_cast<std::ptrdiff_t>(clipped_end));
    }
    return out;
}

DiarizationResult pick_primary_turns(
    const std::vector<SpeechTurn>& raw_turns,
    std::span<const int16_t> pcm_16k,
    const DiarizationConfig& config) {
    DiarizationResult result;
    auto expanded = maybe_split_long_turns(raw_turns, pcm_16k);

    std::vector<SpeechTurn> turns;
    turns.reserve(expanded.size());
    for (const auto& t : expanded) {
        if ((t.end_ms - t.start_ms) < config.min_turn_ms) {
            continue;
        }
        turns.push_back(t);
    }
    if (turns.empty()) {
        return result;
    }

    // Mono turn-taking: alternate speaker ids by chronological turn order.
    for (size_t i = 0; i < turns.size(); ++i) {
        turns[i].speaker_id = static_cast<int>(i % 2);
        const auto start = static_cast<size_t>(std::max<int64_t>(0, turns[i].start_ms) * 16);
        const auto end = static_cast<size_t>(std::max<int64_t>(0, turns[i].end_ms) * 16);
        if (start < pcm_16k.size() && end > start) {
            turns[i].rms_dbfs = rms_dbfs(
                pcm_16k.subspan(start, std::min(end, pcm_16k.size()) - start));
        }
    }

    std::unordered_map<int, int64_t> presence_ms;
    for (const auto& t : turns) {
        presence_ms[t.speaker_id] += std::max<int64_t>(0, t.end_ms - t.start_ms);
    }

    int primary = turns.front().speaker_id;
    int64_t best = presence_ms[primary];
    for (const auto& [speaker, ms] : presence_ms) {
        if (ms > best || (ms == best && speaker < primary)) {
            best = ms;
            primary = speaker;
        }
    }
    // first_then_max_presence: if first speaker is within 10% of max, prefer first.
    if (config.primary_mode == "first_then_max_presence") {
        const int first = turns.front().speaker_id;
        const auto first_ms = presence_ms[first];
        if (first_ms * 10 >= best * 9) {
            primary = first;
        }
    }

    for (auto& t : turns) {
        t.is_primary = (t.speaker_id == primary);
    }

    result.turns = std::move(turns);
    result.primary_speaker = primary;
    result.primary_pcm = concatenate_primary_pcm(pcm_16k, result.turns, primary);
    result.ok = !result.primary_pcm.empty();
    return result;
}

}  // namespace voiceqas::stt
