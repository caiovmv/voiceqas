#pragma once

#include <httplib.h>

#include "voiceqas/server/route_context.hpp"

namespace voiceqas::routes {

void register_static_routes(httplib::Server& server, const RouteContext& ctx);
void register_vqa_routes(httplib::Server& server, const RouteContext& ctx);
void register_stt_routes(httplib::Server& server, const RouteContext& ctx);
void register_media_routes(httplib::Server& server, const RouteContext& ctx);
void register_ops_routes(httplib::Server& server, const RouteContext& ctx);
void register_analysis_routes(httplib::Server& server, const RouteContext& ctx);
void register_config_routes(httplib::Server& server, const RouteContext& ctx);

void register_all_routes(httplib::Server& server, const RouteContext& ctx);

}  // namespace voiceqas::routes
