#include "voiceqas/server/grpc_playground_client.hpp"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <stdexcept>

#include "voiceqas/server/grpc_mappers.hpp"
#include "voiceqas/server/routes/route_helpers.hpp"
#include "stt.grpc.pb.h"
#include "stt.pb.h"

namespace voiceqas::server {
namespace {

std::string model_choice_to_string(stt::SttModelChoice choice) {
    switch (choice) {
        case stt::SttModelChoice::Parakeet:
            return "parakeet";
        case stt::SttModelChoice::Whisper:
            return "whisper";
        default:
            return "auto";
    }
}

}  // namespace

std::string local_grpc_target(const std::string& grpc_bind_addr) {
    const auto colon = grpc_bind_addr.rfind(':');
    if (colon == std::string::npos) {
        // gRPC C++ on Linux often binds wildcard as IPv6-only ([::]:port).
        return "[::1]:50051";
    }
    auto host = grpc_bind_addr.substr(0, colon);
    const auto port = grpc_bind_addr.substr(colon + 1);
    // Wildcard / any-addr → loopback matching how BuildAndStart usually listens.
    if (host.empty() || host == "0.0.0.0" || host == "::" || host == "[::]") {
        return std::string("[::1]:") + port;
    }
    return host + ":" + port;
}

nlohmann::json grpc_stt_transcribe_via_client(
    const std::string& grpc_target,
    const nlohmann::json& body,
    const stt::TranscribeOptions& options) {
    const auto format = routes::format_from_json(body);
    const int sample_rate = body.value("sample_rate", sample_rate_for_format(format));
    auto payload = routes::decode_payload_json(body);
    if (payload.empty()) {
        throw std::runtime_error("empty pcm_bytes/payload");
    }

    auto channel = grpc::CreateChannel(grpc_target, grpc::InsecureChannelCredentials());
    auto stub = voiceqas::v1::SpeechToTextService::NewStub(channel);

    voiceqas::v1::SttTranscribeRequest request;
    request.set_payload(payload.data(), payload.size());
    request.set_format(audio_format_to_proto(format));
    request.set_sample_rate(sample_rate);
    request.set_language(options.language.empty() ? body.value("language", "pt") : options.language);
    request.set_model(model_choice_to_string(options.model));

    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(180));
    if (options.provider) {
        context.AddMetadata("x-stt-provider", *options.provider);
    }
    if (options.telemetry_session_id) {
        context.AddMetadata("x-session-id", *options.telemetry_session_id);
    }

    voiceqas::v1::SttTranscribeResponse response;
    const auto status = stub->Transcribe(&context, request, &response);
    if (!status.ok()) {
        throw std::runtime_error("gRPC Transcribe failed: " + status.error_message());
    }

    nlohmann::json segments = nlohmann::json::array();
    for (const auto& seg : response.segments()) {
        segments.push_back({
            {"start_ms", seg.start_ms()},
            {"end_ms", seg.end_ms()},
            {"text", seg.text()},
        });
    }

    nlohmann::json out = {
        {"text", response.text()},
        {"model", response.model()},
        {"language", response.language()},
        {"duration_ms", response.duration_ms()},
        {"processing_ms", response.processing_ms()},
        {"segments", segments},
        {"ok", response.error().empty()},
        {"via", "grpc"},
        {"grpc_target", grpc_target},
    };
    if (!response.error().empty()) {
        out["error"] = response.error();
        out["ok"] = false;
    }
    return out;
}

}  // namespace voiceqas::server
