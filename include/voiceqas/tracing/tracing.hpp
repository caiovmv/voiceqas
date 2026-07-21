#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace voiceqas::tracing {

struct Config {
    bool enabled = false;
    std::string otlp_endpoint = "http://localhost:4317";
    std::string service_name = "voiceqas";
};

bool enabled();
void init(const Config& config);
void shutdown();

void set_active_session(std::string_view session_id);
std::string active_session();

/** Parent span for an inbound HTTP/gRPC/WS operation (extracts W3C trace context when present). */
class RequestScope {
public:
    RequestScope(
        std::string operation,
        const std::map<std::string, std::string>& carrier_headers = {},
        std::string session_id = {});
    ~RequestScope();

    RequestScope(const RequestScope&) = delete;
    RequestScope& operator=(const RequestScope&) = delete;

    void set_attribute(std::string_view key, std::string_view value);
    void set_attribute(std::string_view key, int64_t value);
    void set_attribute(std::string_view key, double value);
    void set_error(std::string_view message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** Child span for an internal audio-pipeline stage (real wall-clock duration). */
class StageSpan {
public:
    StageSpan(std::string_view stage, std::string_view label, std::string session_id = {});
    ~StageSpan();

    StageSpan(const StageSpan&) = delete;
    StageSpan& operator=(const StageSpan&) = delete;

    void set_bytes_in(uint64_t bytes);
    void set_bytes_out(uint64_t bytes);
    void set_metric(std::string_view key, double value);
    void set_metric(std::string_view key, int64_t value);
    void set_metric(std::string_view key, std::string_view value);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool is_noise_http_path(std::string_view path);

}  // namespace voiceqas::tracing
