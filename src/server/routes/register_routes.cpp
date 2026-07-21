#include "voiceqas/server/routes/register_routes.hpp"

namespace voiceqas::routes {

void register_all_routes(httplib::Server& server, const RouteContext& ctx) {
    register_static_routes(server, ctx);
    register_vqa_routes(server, ctx);
    register_media_routes(server, ctx);
    register_ops_routes(server, ctx);
    register_stt_routes(server, ctx);
    register_analysis_routes(server, ctx);
    register_config_routes(server, ctx);
}

}  // namespace voiceqas::routes
