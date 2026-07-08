#include "voiceqas/tracing/tracing.hpp"

#include <memory>
#include <mutex>
#include <utility>

#if defined(VOICEQAS_ENABLE_TRACING)

#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_options.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/scope.h"

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace otlp = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;
namespace propagation = opentelemetry::trace::propagation;

#endif

namespace voiceqas::tracing {
namespace {

std::mutex g_mutex;
Config g_config;
bool g_enabled = false;

thread_local std::string g_active_session_id;

#if defined(VOICEQAS_ENABLE_TRACING)

class HeaderCarrier final : public opentelemetry::context::propagation::TextMapCarrier {
public:
    explicit HeaderCarrier(const std::map<std::string, std::string>& headers) : headers_(headers) {}

    opentelemetry::nostd::string_view Get(opentelemetry::nostd::string_view key) const noexcept override {
        for (const auto& [k, v] : headers_) {
            if (k == key) {
                return v;
            }
        }
        return {};
    }

    void Set(opentelemetry::nostd::string_view, opentelemetry::nostd::string_view) noexcept override {}

private:
    const std::map<std::string, std::string>& headers_;
};

trace_api::Tracer* tracer() {
    auto provider = trace_api::Provider::GetTracerProvider();
    if (!provider) {
        return nullptr;
    }
    return provider->GetTracer("voiceqas").get();
}

opentelemetry::context::Context extract_parent_context(
    const std::map<std::string, std::string>& carrier_headers) {
    auto current = opentelemetry::context::RuntimeContext::GetCurrent();
    if (carrier_headers.empty()) {
        return current;
    }
    static propagation::HttpTraceContext propagator;
    HeaderCarrier carrier(carrier_headers);
    return propagator.Extract(carrier, current);
}

void apply_session_attr(const opentelemetry::nostd::shared_ptr<trace_api::Span>& span, const std::string& session_id) {
    if (!span) {
        return;
    }
    const std::string& sid = !session_id.empty() ? session_id : g_active_session_id;
    if (!sid.empty()) {
        span->SetAttribute("voiceqas.session_id", sid);
    }
}

#endif

}  // namespace

#if defined(VOICEQAS_ENABLE_TRACING)
struct RequestScope::Impl {
    opentelemetry::nostd::shared_ptr<trace_api::Span> span;
    std::unique_ptr<trace_api::Scope> scope;
    opentelemetry::nostd::unique_ptr<opentelemetry::context::Token> parent_token;
    std::string saved_session;

    Impl(
        std::string operation,
        const std::map<std::string, std::string>& carrier_headers,
        std::string session_id) {
        saved_session = g_active_session_id;
        if (!session_id.empty()) {
            g_active_session_id = session_id;
        }
        if (!g_enabled) {
            return;
        }
        auto* tr = tracer();
        if (!tr) {
            return;
        }
        parent_token = opentelemetry::context::RuntimeContext::Attach(
            extract_parent_context(carrier_headers));
        trace_api::StartSpanOptions opts;
        opts.kind = trace_api::SpanKind::kServer;
        span = tr->StartSpan(operation, opts);
        scope = std::make_unique<trace_api::Scope>(span);
        if (span) {
            span->SetAttribute("voiceqas.component", "server");
            apply_session_attr(span, session_id);
        }
    }
};

struct StageSpan::Impl {
    opentelemetry::nostd::shared_ptr<trace_api::Span> span;
    std::unique_ptr<trace_api::Scope> scope;

    Impl(std::string_view stage, std::string_view label, std::string session_id) {
        if (!g_enabled) {
            return;
        }
        auto* tr = tracer();
        if (!tr) {
            return;
        }
        trace_api::StartSpanOptions opts;
        opts.kind = trace_api::SpanKind::kInternal;
        const auto name = std::string("pipeline/") + std::string(stage);
        span = tr->StartSpan(name, opts);
        scope = std::make_unique<trace_api::Scope>(span);
        if (span) {
            span->SetAttribute("voiceqas.stage", std::string(stage));
            span->SetAttribute("voiceqas.stage_label", std::string(label));
            apply_session_attr(span, session_id);
        }
    }
};
#else
struct RequestScope::Impl {
    std::string saved_session;
    Impl(std::string, const std::map<std::string, std::string>&, std::string session_id) {
        saved_session = g_active_session_id;
        if (!session_id.empty()) {
            g_active_session_id = session_id;
        }
    }
};

struct StageSpan::Impl {};
#endif

bool enabled() {
    return g_enabled;
}

void init(const Config& config) {
    std::lock_guard lock(g_mutex);
    g_config = config;
    g_enabled = config.enabled;
#if defined(VOICEQAS_ENABLE_TRACING)
    if (!config.enabled) {
        return;
    }

    otlp::OtlpGrpcExporterOptions exporter_opts;
    exporter_opts.endpoint = config.otlp_endpoint;
    exporter_opts.use_ssl_credentials = false;

    auto exporter = otlp::OtlpGrpcExporterFactory::Create(exporter_opts);
    trace_sdk::BatchSpanProcessorOptions processor_opts;
    auto processor = trace_sdk::BatchSpanProcessorFactory::Create(std::move(exporter), processor_opts);

    auto resource_attrs = resource::Resource::Create({
        {"service.name", config.service_name},
        {"service.namespace", "voiceqas"},
    });
    std::unique_ptr<trace_sdk::TracerProvider> provider =
        trace_sdk::TracerProviderFactory::Create(std::move(processor), resource_attrs);
    std::shared_ptr<trace_api::TracerProvider> api_provider(std::move(provider));
    trace_api::Provider::SetTracerProvider(
        opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>(api_provider));
#endif
}

void shutdown() {
    std::lock_guard lock(g_mutex);
#if defined(VOICEQAS_ENABLE_TRACING)
    if (auto provider = trace_api::Provider::GetTracerProvider()) {
        if (auto* sdk = dynamic_cast<trace_sdk::TracerProvider*>(provider.get())) {
            sdk->Shutdown();
        }
    }
    trace_api::Provider::SetTracerProvider(
        opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>(
            std::make_shared<trace_api::NoopTracerProvider>()));
#endif
    g_enabled = false;
}

void set_active_session(std::string_view session_id) {
    g_active_session_id = std::string(session_id);
}

std::string active_session() {
    return g_active_session_id;
}

bool is_noise_http_path(std::string_view path) {
    const auto q = path.find('?');
    const auto bare = path.substr(0, q);
    return bare == "/health" || bare == "/ready" || bare == "/metrics";
}

RequestScope::RequestScope(
    std::string operation,
    const std::map<std::string, std::string>& carrier_headers,
    std::string session_id)
    : impl_(std::make_unique<Impl>(std::move(operation), carrier_headers, std::move(session_id))) {}

RequestScope::~RequestScope() {
    if (!impl_) {
        return;
    }
    g_active_session_id = impl_->saved_session;
}

void RequestScope::set_attribute(std::string_view key, std::string_view value) {
#if defined(VOICEQAS_ENABLE_TRACING)
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(std::string(key), std::string(value));
    }
#else
    (void)key;
    (void)value;
#endif
}

void RequestScope::set_attribute(std::string_view key, int64_t value) {
#if defined(VOICEQAS_ENABLE_TRACING)
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(std::string(key), value);
    }
#else
    (void)key;
    (void)value;
#endif
}

void RequestScope::set_attribute(std::string_view key, double value) {
#if defined(VOICEQAS_ENABLE_TRACING)
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(std::string(key), value);
    }
#else
    (void)key;
    (void)value;
#endif
}

void RequestScope::set_error(std::string_view message) {
#if defined(VOICEQAS_ENABLE_TRACING)
    if (impl_ && impl_->span) {
        impl_->span->SetStatus(trace_api::StatusCode::kError, std::string(message));
    }
#else
    (void)message;
#endif
}

StageSpan::StageSpan(std::string_view stage, std::string_view label, std::string session_id)
    : impl_(std::make_unique<Impl>(stage, label, std::move(session_id))) {}

StageSpan::~StageSpan() = default;

void StageSpan::set_bytes_in(uint64_t bytes) {
    set_metric("voiceqas.bytes_in", static_cast<int64_t>(bytes));
}

void StageSpan::set_bytes_out(uint64_t bytes) {
    set_metric("voiceqas.bytes_out", static_cast<int64_t>(bytes));
}

void StageSpan::set_metric(std::string_view key, double value) {
#if defined(VOICEQAS_ENABLE_TRACING)
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(std::string(key), value);
    }
#else
    (void)key;
    (void)value;
#endif
}

void StageSpan::set_metric(std::string_view key, int64_t value) {
#if defined(VOICEQAS_ENABLE_TRACING)
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(std::string(key), value);
    }
#else
    (void)key;
    (void)value;
#endif
}

void StageSpan::set_metric(std::string_view key, std::string_view value) {
#if defined(VOICEQAS_ENABLE_TRACING)
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(std::string(key), std::string(value));
    }
#else
    (void)key;
    (void)value;
#endif
}

}  // namespace voiceqas::tracing
