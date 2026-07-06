#include "voiceqas/server/grpc_server.hpp"

#include <grpcpp/grpcpp.h>

#include <iostream>
#include <memory>

#include "voiceqas/metrics.hpp"
#include "voiceqas/stt/json_util.hpp"
#include "voiceqas/stt/model_util.hpp"
#include "stt.grpc.pb.h"
#include "stt.pb.h"
#include "voice_quality.grpc.pb.h"
#include "voice_quality.pb.h"

namespace voiceqas {

namespace {

AudioFormat from_proto(voiceqas::v1::AudioFormat format) {
    switch (format) {
        case voiceqas::v1::PCM_S16LE_8K: return AudioFormat::PcmS16Le8k;
        case voiceqas::v1::PCM_S16LE_16K: return AudioFormat::PcmS16Le16k;
        case voiceqas::v1::RTP_PCMU: return AudioFormat::RtpPcmu;
        case voiceqas::v1::RTP_PCMA: return AudioFormat::RtpPcma;
        case voiceqas::v1::RTP_G722: return AudioFormat::RtpG722;
        case voiceqas::v1::RTP_G729: return AudioFormat::RtpG729;
        default: return AudioFormat::PcmS16Le8k;
    }
}

voiceqas::v1::AudioFormat to_proto(AudioFormat format) {
    switch (format) {
        case AudioFormat::PcmS16Le8k: return voiceqas::v1::PCM_S16LE_8K;
        case AudioFormat::PcmS16Le16k: return voiceqas::v1::PCM_S16LE_16K;
        case AudioFormat::RtpPcmu: return voiceqas::v1::RTP_PCMU;
        case AudioFormat::RtpPcma: return voiceqas::v1::RTP_PCMA;
        case AudioFormat::RtpG722: return voiceqas::v1::RTP_G722;
        case AudioFormat::RtpG729: return voiceqas::v1::RTP_G729;
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

voiceqas::v1::QualityReport to_proto_report(const std::string& session_id, const WindowMetrics& w) {
    voiceqas::v1::QualityReport report;
    report.set_session_id(session_id);
    report.set_window_start_ms(w.window_start_ms);
    report.set_composite_score(w.composite_score);
    report.set_stt_ready(w.stt_ready);
    fill_metrics(w, report.mutable_metrics());
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

class VoiceQualityServiceImpl final : public voiceqas::v1::VoiceQualityService::Service {
public:
    explicit VoiceQualityServiceImpl(std::shared_ptr<SessionManager> sessions)
        : sessions_(std::move(sessions)) {}

    grpc::Status Ready(grpc::ServerContext*, const voiceqas::v1::ReadyRequest*,
                       voiceqas::v1::ReadyResponse* response) override {
        response->set_status("ready");
        response->set_service("voiceqas");
        return grpc::Status::OK;
    }

    grpc::Status AnalyzeStream(grpc::ServerContext* context,
                               grpc::ServerReaderWriter<voiceqas::v1::QualityReport,
                                                        voiceqas::v1::AudioFrame>* stream) override {
        voiceqas::v1::AudioFrame frame;
        while (stream->Read(&frame)) {
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
            const auto payload = frame.payload();
            const auto format = from_proto(frame.format());
            if (auto report = sessions_->push_frame(
                    frame.session_id(),
                    format,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()),
                    frame.timestamp_ms())) {
                stream->Write(to_proto_report(frame.session_id(), *report));
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status AnalyzeBatch(grpc::ServerContext*,
                              const voiceqas::v1::BatchRequest* request,
                              voiceqas::v1::BatchResponse* response) override {
        const auto format = from_proto(request->format());
        const int sample_rate = request->sample_rate() > 0
            ? request->sample_rate()
            : sample_rate_for_format(format);
        const auto& payload = request->payload();
        const auto result = sessions_->analyze_batch(
            format,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()),
            sample_rate);

        response->set_composite_score(result.composite_score);
        response->set_stt_ready(result.stt_ready);
        for (const auto& w : result.windows) {
            *response->add_windows() = to_proto_report("batch", w);
        }
        for (const auto& [start, end] : result.stt_ready_segments) {
            auto* seg = response->add_stt_ready_segments();
            seg->set_start_ms(start);
            seg->set_end_ms(end);
        }
        return grpc::Status::OK;
    }

private:
    std::shared_ptr<SessionManager> sessions_;
};

class SpeechToTextServiceImpl final : public voiceqas::v1::SpeechToTextService::Service {
public:
    explicit SpeechToTextServiceImpl(std::shared_ptr<stt::SttSessionManager> stt_sessions)
        : stt_sessions_(std::move(stt_sessions)) {}

    grpc::Status Ready(grpc::ServerContext*, const voiceqas::v1::SttReadyRequest*,
                       voiceqas::v1::SttReadyResponse* response) override {
        const auto ready = stt_sessions_ ? stt_sessions_->engine().ready_status() : stt::SttReadyStatus{};
        response->set_status(ready.ready() ? "ready" : "unavailable");
        response->set_service("voiceqas-stt");
        response->set_model(ready.parakeet_ready ? ready.parakeet_model : ready.whisper_model);
        response->set_language(stt_sessions_ ? stt_sessions_->config().language : "pt");
        return grpc::Status::OK;
    }

    grpc::Status Transcribe(grpc::ServerContext*,
                            const voiceqas::v1::SttTranscribeRequest* request,
                            voiceqas::v1::SttTranscribeResponse* response) override {
        if (!stt_sessions_) {
            response->set_error("STT not configured");
            return grpc::Status::OK;
        }
        const auto format = from_proto(request->format());
        const int sample_rate = request->sample_rate() > 0
            ? request->sample_rate()
            : sample_rate_for_format(format);
        const auto& payload = request->payload();
        stt::TranscribeOptions options;
        options.language = request->language().empty() ? stt_sessions_->config().language : request->language();
        if (!request->model().empty()) {
            options.model = stt::parse_model_choice(request->model(), options.model);
        } else {
            options.model = stt::parse_model_choice(stt_sessions_->config().default_model, stt::SttModelChoice::Auto);
        }
        const auto result = stt_sessions_->transcribe_batch(
            format,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()),
            sample_rate,
            options);
        fill_stt_response(result, response);
        return grpc::Status::OK;
    }

    grpc::Status TranscribeStream(
        grpc::ServerContext* context,
        grpc::ServerReaderWriter<voiceqas::v1::SttTranscriptEvent, voiceqas::v1::SttAudioChunk>* stream) override {
        if (!stt_sessions_) {
            voiceqas::v1::SttTranscriptEvent event;
            event.set_type(voiceqas::v1::STT_ERROR);
            event.set_error("STT not configured");
            stream->Write(event);
            return grpc::Status::OK;
        }

        voiceqas::v1::SttAudioChunk chunk;
        while (stream->Read(&chunk)) {
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }

            const auto& payload = chunk.payload();
            const auto format = from_proto(chunk.format());
            const int sample_rate = chunk.sample_rate() > 0
                ? chunk.sample_rate()
                : sample_rate_for_format(format);

            if (!payload.empty()) {
                stt_sessions_->append_chunk(
                    chunk.session_id(),
                    format,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()),
                    sample_rate);
            }

            if (chunk.flush()) {
                stt::TranscribeOptions options;
                options.language = stt_sessions_->config().language;
                if (!chunk.model().empty()) {
                    options.model = stt::parse_model_choice(chunk.model(), options.model);
                } else {
                    options.model = stt::parse_model_choice(
                        stt_sessions_->config().default_model, stt::SttModelChoice::Auto);
                }
                const auto result = stt_sessions_->flush(chunk.session_id(), options);
                voiceqas::v1::SttTranscriptEvent event;
                const auto type = result.ok ? voiceqas::v1::STT_FINAL : voiceqas::v1::STT_ERROR;
                fill_stt_event(chunk.session_id(), type, result, &event);
                stream->Write(event);
            }
        }
        return grpc::Status::OK;
    }

private:
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
};

}  // namespace

GrpcServer::GrpcServer(std::string bind_addr,
                       std::shared_ptr<SessionManager> sessions,
                       std::shared_ptr<stt::SttSessionManager> stt_sessions)
    : bind_addr_(std::move(bind_addr)),
      sessions_(std::move(sessions)),
      stt_sessions_(std::move(stt_sessions)) {}

void GrpcServer::run() {
    VoiceQualityServiceImpl quality_service(sessions_);
    SpeechToTextServiceImpl stt_service(stt_sessions_);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(bind_addr_, grpc::InsecureServerCredentials());
    builder.RegisterService(&quality_service);
    builder.RegisterService(&stt_service);
    auto server = builder.BuildAndStart();
    if (!server) {
        throw std::runtime_error("failed to start gRPC server on " + bind_addr_);
    }
    std::cout << "gRPC listening on " << bind_addr_ << " (VoiceQuality + SpeechToText)\n";
    server->Wait();
}

void GrpcServer::stop() {}

}  // namespace voiceqas
