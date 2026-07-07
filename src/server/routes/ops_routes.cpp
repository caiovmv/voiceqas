#include "voiceqas/server/routes/register_routes.hpp"

#include "voiceqas/server/auth.hpp"
#include "voiceqas/server/routes/route_helpers.hpp"

#include "voiceqas/ops/event_store.hpp"
#include "voiceqas/ops/pipeline_tracker.hpp"

namespace voiceqas::routes {

void register_ops_routes(httplib::Server& server, const RouteContext& ctx) {

    server->Get("/v1/ops/metrics/history", [](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_read_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        std::optional<std::string> session_id;
        if (req.has_param("session_id")) {
            session_id = req.get_param_value("session_id");
        }
        std::optional<std::string> type;
        if (req.has_param("type")) {
            type = req.get_param_value("type");
        }
        int limit = 200;
        if (req.has_param("limit")) {
            limit = std::stoi(req.get_param_value("limit"));
        }
        const auto events = ops::OpsEventStore::instance().query(session_id, type, limit);
        nlohmann::json out = nlohmann::json::array();
        for (const auto& ev : events) {
            out.push_back(ev);
        }
        res.set_content(nlohmann::json{{"status", "ok"}, {"events", out}}.dump(), "application/json");
    });

    server->Get("/v1/ops/alerts", [](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_read_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        std::optional<std::string> session_id;
        if (req.has_param("session_id")) {
            session_id = req.get_param_value("session_id");
        }
        int limit = 50;
        if (req.has_param("limit")) {
            limit = std::stoi(req.get_param_value("limit"));
        }
        const auto events = ops::OpsEventStore::instance().query(session_id, std::string{"alert"}, limit);
        nlohmann::json out = nlohmann::json::array();
        for (const auto& ev : events) {
            out.push_back(ev);
        }
        res.set_content(nlohmann::json{{"status", "ok"}, {"alerts", out}}.dump(), "application/json");
    });

    server->Get("/v1/ops/pipeline/snapshot", [](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_read_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        std::optional<std::string> session_id = parse_pipeline_session_param(req);
        auto snapshot = ops::PipelineTracker::instance().build_snapshot(session_id);
        ops::PipelineTracker::instance().sync_fleet_prometheus();
        snapshot["status"] = "ok";
        res.set_content(snapshot.dump(), "application/json");
    });

    server->Get("/v1/ops/pipeline/sessions", [](const httplib::Request& req, httplib::Response& res) {
        if (!check_ops_read_auth(req)) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return;
        }
        (void)req;
        auto snapshot = ops::PipelineTracker::instance().list_sessions();
        ops::PipelineTracker::instance().sync_fleet_prometheus();
        res.set_content(snapshot.dump(), "application/json");
    });
}

}  // namespace voiceqas::routes
