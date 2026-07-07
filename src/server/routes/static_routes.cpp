#include "voiceqas/server/routes/register_routes.hpp"

#include "voiceqas/server/auth.hpp"
#include "voiceqas/server/routes/route_helpers.hpp"

#include "voiceqas/ops/metrics_hub.hpp"
#include "voiceqas/ops/prometheus.hpp"

#include <sstream>

namespace voiceqas::routes {

void register_static_routes(httplib::Server& server, const RouteContext& ctx) {
    server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    server->Get("/metrics", [ctx](const httplib::Request&, httplib::Response& res) {
        const bool stt_ready = ctx.stt_sessions && ctx.stt_sessions->engine().ready();
        const auto ops_listeners = ops::OpsMetricsHub::instance().listener_count();
        const auto vqa = ops::PrometheusMetrics::instance().snapshot();
        std::ostringstream out;
        out << "# HELP voiceqas_up VoiceQAS REST is responding.\n"
            << "# TYPE voiceqas_up gauge\n"
            << "voiceqas_up 1\n"
            << "# HELP voiceqas_stt_ready STT engine ready (1=yes).\n"
            << "# TYPE voiceqas_stt_ready gauge\n"
            << "voiceqas_stt_ready " << (stt_ready ? 1 : 0) << "\n"
            << "# HELP voiceqas_ops_ws_listeners Active Command Center ops WebSocket listeners.\n"
            << "# TYPE voiceqas_ops_ws_listeners gauge\n"
            << "voiceqas_ops_ws_listeners " << ops_listeners << "\n"
            << ops::PrometheusMetrics::instance().render_prometheus(vqa);
        res.set_content(out.str(), "text/plain; version=0.0.4; charset=utf-8");
    });

    server->Get("/ready", [ctx](const httplib::Request&, httplib::Response& res) {
        nlohmann::json body = {
            {"status", "ready"},
            {"service", "voiceqas"},
            {"interfaces", {"rest", "grpc", "websocket", "stt", "media"}},
            {"docs", {"/docs/swagger", "/docs/redoc", "/ui/"}},
            {"stt", {
                {"ready", ctx.stt_sessions && ctx.stt_sessions->engine().ready()},
                {"endpoint", "/v1/stt/transcribe"},
                {"models", {"parakeet", "whisper", "auto"}},
            }},
            {"toolchain", {
                {"cpp_standard", "c++26"},
                {"gcc", "15"},
                {"deps_manager", "vcpkg"},
            }},
        };
        const auto versions = read_file_or_empty("dependency-versions.txt");
        if (!versions.empty()) {
            body["dependency_versions"] = versions;
        }
        res.set_content(body.dump(), "application/json");
    });

    server->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/ui/");
    });

    server->Get("/docs", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/docs/swagger");
    });

    server->Get("/docs/swagger", [ctx](const httplib::Request&, httplib::Response& res) {
        const auto content = read_file_or_empty(ctx.web_root + "/swagger.html");
        if (content.empty()) {
            res.status = 404;
            return;
        }
        res.set_content(content, "text/html; charset=utf-8");
    });

    server->Get("/docs/redoc", [ctx](const httplib::Request&, httplib::Response& res) {
        const auto content = read_file_or_empty(ctx.web_root + "/redoc.html");
        if (content.empty()) {
            res.status = 404;
            return;
        }
        res.set_content(content, "text/html; charset=utf-8");
    });

    server->Get("/openapi/voiceqas.yaml", [ctx](const httplib::Request&, httplib::Response& res) {
        const auto content = read_file_or_empty(ctx.openapi_path);
        if (content.empty()) {
            res.status = 404;
            res.set_content("openapi spec not found", "text/plain");
            return;
        }
        res.set_content(content, "application/yaml");
    });

}

}  // namespace voiceqas::routes
