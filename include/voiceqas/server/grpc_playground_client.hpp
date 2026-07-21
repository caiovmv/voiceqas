#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "voiceqas/stt/client.hpp"

namespace voiceqas::server {

/** Loopback target for in-process REST playground → gRPC (maps 0.0.0.0/:: → [::1]).
 *  On Linux gRPC often binds wildcard as IPv6-only; IPv4-mapped usually still works for 127.0.0.1. */
std::string local_grpc_target(const std::string& grpc_bind_addr);

/**
 * Call SpeechToTextService.Transcribe on the real gRPC server (Beyla :50051 + OTel spans).
 * Expects playground JSON with pcm_bytes/format/sample_rate/model/provider/session_id.
 */
nlohmann::json grpc_stt_transcribe_via_client(
    const std::string& grpc_target,
    const nlohmann::json& body,
    const stt::TranscribeOptions& options);

}  // namespace voiceqas::server
