#include "voiceqas/server/grpc_server.hpp"

#include <grpcpp/grpcpp.h>

#include <cstring>
#include <map>
#include <memory>

#include "voiceqas/media/session.hpp"
#include "voiceqas/metrics.hpp"
#include "voiceqas/server/grpc_mappers.hpp"
#include "voiceqas/tracing/tracing.hpp"
#include "voiceqas/stt/json_util.hpp"
#include "voiceqas/stt/model_util.hpp"
#include "media.grpc.pb.h"
#include "media.pb.h"
#include "stt.grpc.pb.h"
#include "stt.pb.h"
#include "voice_quality.grpc.pb.h"
#include "voice_quality.pb.h"

namespace voiceqas {

namespace {

using server::audio_format_from_proto;
using server::audio_format_to_proto;
using server::fill_stt_event;
using server::fill_stt_response;
using server::quality_report_to_proto;

std::map<std::string, std::string> grpc_carrier(const grpc::ServerContext* context) {
    std::map<std::string, std::string> out;
    if (!context) {
        return out;
    }
    for (const auto& pair : context->client_metadata()) {
        out.emplace(std::string(pair.first.data(), pair.first.size()),
                    std::string(pair.second.data(), pair.second.size()));
    }
    return out;
}

std::string metadata_value(const grpc::ServerContext* context, const char* key) {
    if (!context) {
        return {};
    }
    const auto& md = context->client_metadata();
    auto it = md.find(key);
    if (it == md.end()) {
        return {};
    }
    return std::string(it->second.data(), it->second.size());
}

class VoiceQualityServiceImpl final : public voiceqas::v1::VoiceQualityService::Service {
public:
    explicit VoiceQualityServiceImpl(std::shared_ptr<VqaSessionManager> sessions)
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
            tracing::RequestScope scope(
                "gRPC VoiceQualityService/AnalyzeStream",
                grpc_carrier(context),
                frame.session_id());
            const auto payload = frame.payload();
            const auto format = audio_format_from_proto(frame.format());
            if (auto report = sessions_->push_frame(
                    frame.session_id(),
                    format,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()),
                    frame.timestamp_ms())) {
                stream->Write(quality_report_to_proto(frame.session_id(), *report));
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status AnalyzeBatch(grpc::ServerContext* context,
                              const voiceqas::v1::BatchRequest* request,
                              voiceqas::v1::BatchResponse* response) override {
        tracing::RequestScope scope("gRPC VoiceQualityService/AnalyzeBatch", grpc_carrier(context));
        const auto format = audio_format_from_proto(request->format());
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
            *response->add_windows() = quality_report_to_proto("batch", w);
        }
        for (const auto& [start, end] : result.stt_ready_segments) {
            auto* seg = response->add_stt_ready_segments();
            seg->set_start_ms(start);
            seg->set_end_ms(end);
        }
        return grpc::Status::OK;
    }

private:
    std::shared_ptr<VqaSessionManager> sessions_;
};

class SpeechToTextServiceImpl final : public voiceqas::v1::SpeechToTextService::Service {
public:
    explicit SpeechToTextServiceImpl(std::shared_ptr<stt::SttSessionManager> stt_sessions)
        : stt_sessions_(std::move(stt_sessions)) {}

    grpc::Status Ready(grpc::ServerContext* context, const voiceqas::v1::SttReadyRequest*,
                       voiceqas::v1::SttReadyResponse* response) override {
        tracing::RequestScope scope("gRPC SpeechToTextService/Ready", grpc_carrier(context));
        const auto ready = stt_sessions_ ? stt_sessions_->engine().ready_status() : stt::SttReadyStatus{};
        response->set_status(ready.ready() ? "ready" : "unavailable");
        response->set_service("voiceqas-stt");
        response->set_model(ready.parakeet_ready ? ready.parakeet_model : ready.whisper_model);
        response->set_language(stt_sessions_ ? stt_sessions_->config().language : "pt");
        return grpc::Status::OK;
    }

    grpc::Status Transcribe(grpc::ServerContext* context,
                            const voiceqas::v1::SttTranscribeRequest* request,
                            voiceqas::v1::SttTranscribeResponse* response) override {
        const auto session_id = metadata_value(context, "x-session-id");
        tracing::RequestScope scope(
            "gRPC SpeechToTextService/Transcribe",
            grpc_carrier(context),
            session_id);
        if (!stt_sessions_) {
            response->set_error("STT not configured");
            return grpc::Status::OK;
        }
        const auto format = audio_format_from_proto(request->format());
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
        if (const auto provider = metadata_value(context, "x-stt-provider"); !provider.empty()) {
            options.provider = provider;
        }
        if (!session_id.empty()) {
            options.telemetry_session_id = session_id;
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
            tracing::RequestScope scope(
                chunk.flush() ? "gRPC SpeechToTextService/TranscribeStream flush"
                              : "gRPC SpeechToTextService/TranscribeStream",
                grpc_carrier(context),
                chunk.session_id());

            const auto& payload = chunk.payload();
            const auto format = audio_format_from_proto(chunk.format());
            const int sample_rate = chunk.sample_rate() > 0
                ? chunk.sample_rate()
                : sample_rate_for_format(format);

            if (!payload.empty()) {
                stt_sessions_->append_chunk(
                    chunk.session_id(),
                    format,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()),
                    sample_rate);
                if (const auto partial = stt_sessions_->emit_partial_if_due(chunk.session_id())) {
                    if (partial->ok && !partial->text.empty()) {
                        voiceqas::v1::SttTranscriptEvent event;
                        fill_stt_event(chunk.session_id(), voiceqas::v1::STT_PARTIAL, *partial, &event);
                        stream->Write(event);
                    }
                }
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
                const auto result = stt_sessions_->flush_and_publish(chunk.session_id(), options, false);
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

class MediaRelayServiceImpl final : public voiceqas::v1::MediaRelayService::Service {
public:
    explicit MediaRelayServiceImpl(std::shared_ptr<media::MediaSessionManager> media_sessions,
                                   std::shared_ptr<stt::SttSessionManager> stt_sessions)
        : media_sessions_(std::move(media_sessions)), stt_sessions_(std::move(stt_sessions)) {}

    grpc::Status OpenSession(grpc::ServerContext*,
                             const voiceqas::v1::OpenMediaSessionRequest* request,
                             voiceqas::v1::OpenMediaSessionResponse* response) override {
        if (!media_sessions_) {
            response->set_error("media not configured");
            return grpc::Status::OK;
        }
        media::MediaSessionConfig cfg;
        const auto& s = request->session();
        cfg.session_id = s.session_id();
        cfg.format = audio_format_from_proto(s.format());
        cfg.sample_rate = s.sample_rate() > 0 ? s.sample_rate() : sample_rate_for_format(cfg.format);
        cfg.remote_host = s.remote_host();
        cfg.remote_port = static_cast<uint16_t>(s.remote_port());
        cfg.inbound_host = s.inbound_host();
        cfg.inbound_port = static_cast<uint16_t>(s.inbound_port());
        std::string error;
        if (!media_sessions_->open_session(cfg, error)) {
            response->set_error(error);
            return grpc::Status::OK;
        }
        response->set_status("ok");
        response->set_session_id(cfg.session_id);
        response->set_sample_rate(cfg.sample_rate);
        return grpc::Status::OK;
    }

    grpc::Status CloseSession(grpc::ServerContext*,
                              const voiceqas::v1::CloseMediaSessionRequest* request,
                              voiceqas::v1::CloseMediaSessionResponse* response) override {
        if (!media_sessions_) {
            response->set_error("media not configured");
            return grpc::Status::OK;
        }
        if (stt_sessions_) {
            stt_sessions_->flush_and_publish(request->session_id(), {}, false);
        }
        if (!media_sessions_->close_session(request->session_id())) {
            response->set_error("session not found");
            return grpc::Status::OK;
        }
        response->set_status("ok");
        return grpc::Status::OK;
    }

    grpc::Status ListSessions(grpc::ServerContext*,
                              const voiceqas::v1::ListMediaSessionsRequest*,
                              voiceqas::v1::ListMediaSessionsResponse* response) override {
        if (!media_sessions_) {
            return grpc::Status::OK;
        }
        for (const auto& cfg : media_sessions_->list_sessions()) {
            auto* out = response->add_sessions();
            out->set_session_id(cfg.session_id);
            out->set_format(audio_format_to_proto(cfg.format));
            out->set_sample_rate(cfg.sample_rate);
            out->set_remote_host(cfg.remote_host);
            out->set_remote_port(cfg.remote_port);
            out->set_inbound_host(cfg.inbound_host);
            out->set_inbound_port(cfg.inbound_port);
        }
        return grpc::Status::OK;
    }

    grpc::Status SendAgentAudio(grpc::ServerContext*,
                                const voiceqas::v1::SendAgentAudioRequest* request,
                                voiceqas::v1::SendAgentAudioResponse* response) override {
        if (!media_sessions_) {
            response->set_error("media not configured");
            return grpc::Status::OK;
        }
        const auto& payload = request->pcm_payload();
        if (payload.size() % 2 != 0) {
            response->set_error("pcm payload must be even length");
            return grpc::Status::OK;
        }
        std::vector<int16_t> pcm(payload.size() / 2);
        std::memcpy(pcm.data(), payload.data(), payload.size());
        const auto result = media_sessions_->send_agent_pcm(
            request->session_id(), pcm, request->sample_rate() > 0 ? request->sample_rate() : 16000);
        if (!result.ok) {
            response->set_error(result.error);
            return grpc::Status::OK;
        }
        response->set_status("ok");
        response->set_rtp_packets(static_cast<int32_t>(result.rtp_packets.size()));
        response->set_bytes_sent(static_cast<int32_t>(result.bytes_sent));
        return grpc::Status::OK;
    }

private:
    std::shared_ptr<media::MediaSessionManager> media_sessions_;
    std::shared_ptr<stt::SttSessionManager> stt_sessions_;
};

}  // namespace

GrpcServer::GrpcServer(std::string bind_addr,
                       std::shared_ptr<VqaSessionManager> sessions,
                       std::shared_ptr<stt::SttSessionManager> stt_sessions,
                       std::shared_ptr<media::MediaSessionManager> media_sessions)
    : bind_addr_(std::move(bind_addr)),
      sessions_(std::move(sessions)),
      stt_sessions_(std::move(stt_sessions)),
      media_sessions_(std::move(media_sessions)) {}

void GrpcServer::run() {
    VoiceQualityServiceImpl quality_service(sessions_);
    SpeechToTextServiceImpl stt_service(stt_sessions_);
    MediaRelayServiceImpl media_service(media_sessions_, stt_sessions_);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(bind_addr_, grpc::InsecureServerCredentials());
    builder.RegisterService(&quality_service);
    builder.RegisterService(&stt_service);
    if (media_sessions_) {
        builder.RegisterService(&media_service);
    }
    auto server = builder.BuildAndStart();
    if (!server) {
        throw std::runtime_error("failed to start gRPC server on " + bind_addr_);
    }
    std::cout << "gRPC listening on " << bind_addr_
              << " (VoiceQuality + SpeechToText"
              << (media_sessions_ ? " + MediaRelay" : "") << ")\n";
    server->Wait();
}

void GrpcServer::stop() {}

}  // namespace voiceqas
