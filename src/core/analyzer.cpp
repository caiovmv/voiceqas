#include "voiceqas/analyzer.hpp"

#include "voiceqas/tracing/tracing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "voiceqas/audio/decoder.hpp"
#include "voiceqas/audio/dsp/channel_strip.hpp"
#include "voiceqas/rtp/depacketizer.hpp"
#include "voiceqas/stt_gate.hpp"

namespace voiceqas {

namespace {

constexpr double kMinDb = -96.0;
constexpr int16_t kClipThreshold = 32000;

double linear_to_dbfs(double linear) {
    if (linear <= 1e-10) {
        return kMinDb;
    }
    return std::max(kMinDb, 20.0 * std::log10(linear / 32768.0));
}

double frame_rms(std::span<const int16_t> samples) {
    if (samples.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (int16_t s : samples) {
        const double v = static_cast<double>(s);
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

double frame_peak(std::span<const int16_t> samples) {
    int16_t peak = 0;
    for (int16_t s : samples) {
        peak = std::max(peak, static_cast<int16_t>(std::abs(s)));
    }
    return static_cast<double>(peak);
}

double frame_clipping_ratio(std::span<const int16_t> samples) {
    if (samples.empty()) {
        return 0.0;
    }
    size_t clipped = 0;
    for (int16_t s : samples) {
        if (std::abs(s) >= kClipThreshold) {
            ++clipped;
        }
    }
    return static_cast<double>(clipped) / static_cast<double>(samples.size());
}

double speech_weight(const WindowMetrics& w) {
    return std::max(0.0, 1.0 - w.silence_ratio);
}

WindowMetrics aggregate_speech_windows(const std::vector<WindowMetrics>& speech) {
    WindowMetrics out;
    if (speech.empty()) {
        return out;
    }

    double total_w = 0.0;
    for (const auto& w : speech) {
        total_w += speech_weight(w);
    }
    const bool equal_weight = total_w < 1e-12;
    if (equal_weight) {
        total_w = static_cast<double>(speech.size());
    }

    double rms = 0.0;
    double peak = kMinDb;
    double clip = 0.0;
    double snr = 0.0;
    double silence = 0.0;
    double flatness = 0.0;
    double loss = 0.0;
    double jitter = 0.0;
    double score = 0.0;
    double ready_w = 0.0;

    for (const auto& w : speech) {
        const double wgt = equal_weight ? 1.0 : speech_weight(w);
        rms += w.rms_dbfs * wgt;
        peak = std::max(peak, w.peak_dbfs);
        clip += w.clipping_ratio * wgt;
        snr += w.snr_estimate_db * wgt;
        silence += w.silence_ratio * wgt;
        flatness += w.spectral_flatness * wgt;
        loss += w.packet_loss_pct * wgt;
        jitter += w.jitter_ms * wgt;
        score += w.speech_quality_score * wgt;
        if (w.stt_ready) {
            ready_w += wgt;
        }
    }

    out.window_start_ms = speech.front().window_start_ms;
    out.rms_dbfs = rms / total_w;
    out.peak_dbfs = peak;
    out.clipping_ratio = clip / total_w;
    out.snr_estimate_db = snr / total_w;
    out.silence_ratio = silence / total_w;
    out.spectral_flatness = flatness / total_w;
    out.packet_loss_pct = loss / total_w;
    out.jitter_ms = jitter / total_w;
    out.speech_quality_score = score / total_w;
    out.composite_score = out.speech_quality_score;
    out.stt_ready = (ready_w / total_w) >= 0.5;
    return out;
}

double estimate_stt_risk(const std::vector<WindowMetrics>& speech) {
    if (speech.empty()) {
        return 1.0;
    }
    double sum = 0.0;
    for (const auto& w : speech) {
        const double snr_term = std::clamp((20.0 - w.snr_estimate_db) / 20.0, 0.0, 1.0);
        const double clip_term = std::clamp(w.clipping_ratio / 0.05, 0.0, 1.0);
        const double quality_term =
            std::clamp((70.0 - w.speech_quality_score) / 70.0, 0.0, 1.0);
        sum += 0.45 * snr_term + 0.25 * clip_term + 0.30 * quality_term;
    }
    return std::clamp(sum / static_cast<double>(speech.size()), 0.0, 1.0);
}

double compute_snr_std(const std::vector<WindowMetrics>& speech) {
    if (speech.size() < 2) {
        return 0.0;
    }
    double mean = 0.0;
    for (const auto& w : speech) {
        mean += w.snr_estimate_db;
    }
    mean /= static_cast<double>(speech.size());
    double var = 0.0;
    for (const auto& w : speech) {
        const double d = w.snr_estimate_db - mean;
        var += d * d;
    }
    return std::sqrt(var / static_cast<double>(speech.size() - 1));
}

}  // namespace

VoiceAnalyzer::VoiceAnalyzer(AnalyzerConfig config)
    : config_(std::move(config)), gate_(config_) {
    const int frame_samples = config_.sample_rate * config_.frame_ms / 1000;
    window_buffer_.reserve(static_cast<size_t>(config_.sample_rate * config_.window_ms / 1000));
    frame_buffer_.reserve(static_cast<size_t>(config_.window_ms / config_.frame_ms));
    (void)frame_samples;
}

void VoiceAnalyzer::reset() {
    window_buffer_.clear();
    frame_buffer_.clear();
    elapsed_ms_ = 0;
    window_start_ms_ = 0;
    rtp_packet_loss_pct_ = 0.0;
    rtp_jitter_ms_ = 0.0;
    gate_.reset();
}

void VoiceAnalyzer::set_rtp_metrics(double packet_loss_pct, double jitter_ms) {
    rtp_packet_loss_pct_ = packet_loss_pct;
    rtp_jitter_ms_ = jitter_ms;
}

FrameMetrics VoiceAnalyzer::analyze_frame(std::span<const int16_t> frame) {
    FrameMetrics m;
    const double rms = frame_rms(frame);
    const double peak = frame_peak(frame);
    m.rms_dbfs = linear_to_dbfs(rms);
    m.peak_dbfs = linear_to_dbfs(peak);
    m.clipping_ratio = frame_clipping_ratio(frame);
    m.is_speech = m.rms_dbfs >= config_.speech_energy_threshold_dbfs && m.clipping_ratio < 0.5;
    return m;
}

double VoiceAnalyzer::compute_spectral_flatness(std::span<const int16_t> samples) {
    if (samples.size() < 64) {
        return 1.0;
    }

    constexpr size_t n = 64;
    std::vector<double> power(n / 2, 0.0);
    for (size_t k = 0; k < n / 2; ++k) {
        double real = 0.0;
        double imag = 0.0;
        for (size_t t = 0; t < n; ++t) {
            const double sample = t < samples.size() ? static_cast<double>(samples[t]) : 0.0;
            const double angle = 2.0 * M_PI * static_cast<double>(k) * static_cast<double>(t) / static_cast<double>(n);
            real += sample * std::cos(angle);
            imag -= sample * std::sin(angle);
        }
        power[k] = real * real + imag * imag + 1e-12;
    }

    double geo = 0.0;
    double arith = 0.0;
    for (double p : power) {
        geo += std::log(p);
        arith += p;
    }
    geo = std::exp(geo / static_cast<double>(power.size()));
    arith /= static_cast<double>(power.size());
    return std::clamp(geo / arith, 0.0, 1.0);
}

WindowMetrics VoiceAnalyzer::aggregate_window() {
    WindowMetrics w;
    w.window_start_ms = window_start_ms_;
    w.packet_loss_pct = rtp_packet_loss_pct_;
    w.jitter_ms = rtp_jitter_ms_;

    if (frame_buffer_.empty()) {
        w.silence_ratio = 1.0;
        gate_.evaluate(w);
        return w;
    }

    double rms_sum = 0.0;
    double peak = kMinDb;
    double clip_sum = 0.0;
    size_t speech_frames = 0;

    for (const auto& f : frame_buffer_) {
        rms_sum += f.rms_dbfs;
        peak = std::max(peak, f.peak_dbfs);
        clip_sum += f.clipping_ratio;
        if (f.is_speech) {
            ++speech_frames;
        }
    }

    const size_t n = frame_buffer_.size();
    w.rms_dbfs = rms_sum / static_cast<double>(n);
    w.peak_dbfs = peak;
    w.clipping_ratio = clip_sum / static_cast<double>(n);
    w.silence_ratio = 1.0 - static_cast<double>(speech_frames) / static_cast<double>(n);

    double noise_energy = 0.0;
    double speech_energy = 0.0;
    for (const auto& f : frame_buffer_) {
        const double lin = std::pow(10.0, f.rms_dbfs / 20.0);
        if (f.is_speech) {
            speech_energy += lin * lin;
        } else {
            noise_energy += lin * lin;
        }
    }
    if (noise_energy < 1e-12) {
        w.snr_estimate_db = speech_energy > 1e-12 ? 40.0 : 0.0;
    } else {
        w.snr_estimate_db = 10.0 * std::log10(speech_energy / noise_energy);
    }

    w.spectral_flatness = compute_spectral_flatness(window_buffer_);
    gate_.evaluate(w);

    return w;
}

std::optional<WindowMetrics> VoiceAnalyzer::push_pcm(std::span<const int16_t> samples, int64_t timestamp_ms) {
    if (window_start_ms_ == 0) {
        window_start_ms_ = timestamp_ms;
    }

    const int frame_samples = config_.sample_rate * config_.frame_ms / 1000;
    std::optional<WindowMetrics> result;

    for (size_t offset = 0; offset < samples.size(); offset += static_cast<size_t>(frame_samples)) {
        const size_t count = std::min(static_cast<size_t>(frame_samples), samples.size() - offset);
        if (count == 0) {
            break;
        }
        const auto frame = samples.subspan(offset, count);
        frame_buffer_.push_back(analyze_frame(frame));
        window_buffer_.insert(window_buffer_.end(), frame.begin(), frame.end());
        elapsed_ms_ += config_.frame_ms;

        const int frames_per_window = config_.window_ms / config_.frame_ms;
        if (static_cast<int>(frame_buffer_.size()) >= frames_per_window) {
            result = aggregate_window();
            frame_buffer_.clear();
            window_buffer_.clear();
            window_start_ms_ = timestamp_ms;
        }
    }

    return result;
}

BatchResult VoiceAnalyzer::analyze_pcm_batch(std::span<const int16_t> samples, int sample_rate) {
    AnalyzerConfig batch_config = config_;
    batch_config.sample_rate = sample_rate;
    VoiceAnalyzer batch_analyzer(batch_config);

    BatchResult result;
    int64_t ts = 0;
    const int frame_samples = sample_rate * batch_config.frame_ms / 1000;

    for (size_t offset = 0; offset < samples.size(); offset += static_cast<size_t>(frame_samples)) {
        const size_t count = std::min(static_cast<size_t>(frame_samples), samples.size() - offset);
        if (auto window = batch_analyzer.push_pcm(samples.subspan(offset, count), ts)) {
            result.windows.push_back(*window);
        }
        ts += batch_config.frame_ms;
    }

    if (result.windows.empty()) {
        return result;
    }

    result.aggregated = result.windows.back();

    for (const auto& w : result.windows) {
        if (w.silence_ratio <= batch_config.max_silence_ratio) {
            result.speech_windows.push_back(w);
        }
        if (w.stt_ready) {
            ++result.ready_window_count;
        }
    }
    result.speech_window_count = static_cast<int>(result.speech_windows.size());
    result.speech_aggregated = aggregate_speech_windows(result.speech_windows);
    // Session composite = speech-only quality (silence no longer pulls the average down).
    result.composite_score = result.speech_window_count > 0
                                 ? result.speech_aggregated.composite_score
                                 : 0.0;
    result.ready_ratio = static_cast<double>(result.ready_window_count) /
                         static_cast<double>(result.windows.size());
    result.snr_std = compute_snr_std(result.speech_windows);
    result.stt_risk = estimate_stt_risk(result.speech_windows);

    int64_t seg_start = -1;
    for (const auto& w : result.windows) {
        if (w.stt_ready && seg_start < 0) {
            seg_start = w.window_start_ms;
        } else if (!w.stt_ready && seg_start >= 0) {
            result.stt_ready_segments.emplace_back(seg_start, w.window_start_ms);
            seg_start = -1;
        }
    }
    if (seg_start >= 0) {
        result.stt_ready_segments.emplace_back(seg_start, ts);
    }

    result.stt_ready = !result.stt_ready_segments.empty();
    return result;
}

VqaSessionManager::VqaSessionManager(
    AnalyzerConfig default_config,
    audio::AudioProcessingConfig audio_config,
    std::shared_ptr<ports::IMetricsPublisher> metrics,
    std::shared_ptr<ports::IPipelineTelemetry> telemetry)
    : default_config_(std::move(default_config)),
      audio_config_(std::move(audio_config)),
      metrics_(std::move(metrics)),
      telemetry_(std::move(telemetry)) {}

VqaSessionManager::VqaSessionManager(AnalyzerConfig default_config, audio::AudioProcessingConfig audio_config)
    : VqaSessionManager(
          std::move(default_config),
          std::move(audio_config),
          ports::noop_metrics_publisher(),
          ports::noop_pipeline_telemetry()) {}

std::optional<WindowMetrics> VqaSessionManager::push_frame(
    const std::string& session_id,
    AudioFormat format,
    std::span<const uint8_t> payload,
    int64_t timestamp_ms) {
    std::unique_lock lock(mutex_);
    auto& session = sessions_[session_id];
    const int rate = sample_rate_for_format(format);
    if (!session.analyzer || session.analyzer->config().sample_rate != rate) {
        AnalyzerConfig cfg = default_config_;
        cfg.sample_rate = rate;
        session.analyzer = VoiceAnalyzer(cfg);
        session.rtp = rtp::RtpDepacketizer(rtp_clock_rate_for_format(format));
        session.strip = audio::VoiceChannelStrip(audio_config_);
    }

    const auto decode_started = std::chrono::steady_clock::now();
    tracing::StageSpan decode_span("decode_vqa", "Decode VQA", session_id);
    decode_span.set_bytes_in(payload.size());
    const auto decoded = audio::decode_to_pcm(format, payload, &(*session.rtp));
    const auto decode_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - decode_started)
                               .count();
    decode_span.set_metric("voiceqas.duration_ms", decode_ms);
    if (!decoded.ok) {
        return std::nullopt;
    }
    decode_span.set_bytes_out(decoded.pcm.size() * sizeof(int16_t));

    auto pcm = std::move(decoded.pcm);
    double agc_ms = 0.0;
    double enhancement_ms = 0.0;
    if (!pcm.empty()) {
        tracing::StageSpan strip_span("dsp_strip_vqa", "Channel strip VQA", session_id);
        strip_span.set_bytes_in(pcm.size() * sizeof(int16_t));
        const auto timings = session.strip.process_inplace(pcm, rate);
        agc_ms = timings.agc_ms;
        enhancement_ms = timings.nr_ms;
        strip_span.set_metric("voiceqas.duration_ms", agc_ms + enhancement_ms);
        strip_span.set_bytes_out(pcm.size() * sizeof(int16_t));
    }
    telemetry_->record_rtp_ingress(
        session_id,
        payload.size(),
        decoded.rtp_stats.jitter_ms,
        decoded.rtp_stats.packet_loss_pct);
    telemetry_->record_vqa_path(
        session_id,
        payload.size(),
        pcm.size() * sizeof(int16_t),
        decode_ms,
        agc_ms,
        enhancement_ms,
        decoded.rtp_stats.jitter_ms,
        decoded.rtp_stats.packet_loss_pct);
    session.analyzer->set_rtp_metrics(decoded.rtp_stats.packet_loss_pct, decoded.rtp_stats.jitter_ms);

    tracing::StageSpan vqa_span("vqa", "VQA Analyzer", session_id);
    vqa_span.set_bytes_in(pcm.size() * sizeof(int16_t));
    if (auto report = session.analyzer->push_pcm(pcm, timestamp_ms)) {
        vqa_span.set_metric("voiceqas.composite_score", report->composite_score);
        vqa_span.set_metric("voiceqas.stt_ready", report->stt_ready ? int64_t{1} : int64_t{0});
        metrics_->publish_vqa(session_id, *report);
        return report;
    }
    return std::nullopt;
}

std::optional<WindowMetrics> VqaSessionManager::push_pcm(
    const std::string& session_id,
    std::span<const int16_t> pcm,
    int64_t timestamp_ms,
    int sample_rate,
    bool apply_agc) {
    std::unique_lock lock(mutex_);
    auto& session = sessions_[session_id];
    if (!session.analyzer || session.analyzer->config().sample_rate != sample_rate) {
        AnalyzerConfig cfg = default_config_;
        cfg.sample_rate = sample_rate;
        session.analyzer = VoiceAnalyzer(cfg);
        session.strip = audio::VoiceChannelStrip(audio_config_);
    }
    std::vector<int16_t> copy(pcm.begin(), pcm.end());
    // Shared ingress already ran the channel strip when apply_agc=false.
    if (apply_agc && !copy.empty()) {
        session.strip.process_inplace(copy, sample_rate);
    }
    if (auto report = session.analyzer->push_pcm(copy, timestamp_ms)) {
        metrics_->publish_vqa(session_id, *report);
        return report;
    }
    return std::nullopt;
}

BatchResult VqaSessionManager::analyze_batch(
    AudioFormat format,
    std::span<const uint8_t> payload,
    int sample_rate,
    const std::optional<std::string>& telemetry_session_id,
    const std::optional<audio::AudioProcessingConfig>& audio_override) {
    const std::string session_id = telemetry_session_id.value_or(
        "vqa-batch-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    AnalyzerConfig cfg = default_config_;
    cfg.sample_rate = sample_rate > 0 ? sample_rate : sample_rate_for_format(format);
    VoiceAnalyzer analyzer(cfg);

    const audio::AudioProcessingConfig& audio_cfg =
        audio_override.has_value() ? *audio_override : audio_config_;

    rtp::RtpDepacketizer depacketizer(rtp_clock_rate_for_format(format));
    const auto decoded = audio::decode_to_pcm(format, payload, &depacketizer);
    if (!decoded.ok) {
        return {};
    }

    telemetry_->set_session_codec(session_id, audio_format_to_string(format));
    telemetry_->record_rtp_ingress(
        session_id,
        payload.size(),
        decoded.rtp_stats.jitter_ms,
        decoded.rtp_stats.packet_loss_pct);

    auto pcm = decoded.pcm;
    const auto pcm_bytes = pcm.size() * sizeof(int16_t);
    double agc_ms = 0.0;
    double enhancement_ms = 0.0;
    if (!pcm.empty()) {
        audio::AudioProcessingConfig strip_cfg = audio_cfg;
        strip_cfg.sync_legacy_from_strip();
        audio::VoiceChannelStrip strip(strip_cfg);
        const auto timings = strip.process_inplace(pcm, cfg.sample_rate);
        agc_ms = timings.agc_ms;
        enhancement_ms = timings.nr_ms;
    }
    telemetry_->record_vqa_path(
        session_id,
        payload.size(),
        pcm_bytes,
        0.0,
        agc_ms,
        enhancement_ms,
        decoded.rtp_stats.jitter_ms,
        decoded.rtp_stats.packet_loss_pct);
    analyzer.set_rtp_metrics(decoded.rtp_stats.packet_loss_pct, decoded.rtp_stats.jitter_ms);

    auto result = analyzer.analyze_pcm_batch(pcm, cfg.sample_rate);
    if (!result.windows.empty()) {
        telemetry_->record_vqa_window(session_id, result.aggregated);
    }
    telemetry_->finish_session(session_id, "batch");
    return result;
}

void VqaSessionManager::remove_session(const std::string& session_id) {
    std::unique_lock lock(mutex_);
    sessions_.erase(session_id);
    telemetry_->on_session_removed(session_id);
    telemetry_->remove_session(session_id);
}

}  // namespace voiceqas
