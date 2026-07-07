from pathlib import Path

root = Path(__file__).resolve().parent
helpers = (root / "_helpers_extract.txt").read_text(encoding="utf-8")
routes = (root / "_routes_extract.txt").read_text(encoding="utf-8")

helpers_h_path = root.parent.parent / "include" / "voiceqas" / "server" / "routes" / "route_helpers.hpp"
helpers_h_path.parent.mkdir(parents=True, exist_ok=True)
helpers_h_path.write_text(
    "#pragma once\n\n"
    "#include <httplib.h>\n"
    "#include <nlohmann/json.hpp>\n\n"
    "#include <optional>\n"
    "#include <span>\n"
    "#include <string>\n"
    "#include <vector>\n\n"
    "#include \"voiceqas/metrics.hpp\"\n\n"
    "namespace voiceqas::routes {\n\n"
    + helpers
    + "\n}  // namespace voiceqas::routes\n",
    encoding="utf-8",
)

replacements = [
    ("media_sessions_", "ctx.media_sessions"),
    ("stt_sessions_", "ctx.stt_sessions"),
    ("web_root_", "ctx.web_root"),
    ("openapi_path_", "ctx.openapi_path"),
    ("sessions_", "ctx.sessions"),
    ("global_media_config()", "ctx.media_config"),
    ("[this]", "[ctx]"),
]
body = routes
for old, new in replacements:
    body = body.replace(old, new)

cpp = (
    '#include "voiceqas/server/routes/register_routes.hpp"\n\n'
    '#include "voiceqas/server/auth.hpp"\n'
    '#include "voiceqas/server/routes/route_helpers.hpp"\n\n'
    '#include "voiceqas/audio/decoder.hpp"\n'
    '#include "voiceqas/audio/encoder.hpp"\n'
    '#include "voiceqas/audio/ingress_codec.hpp"\n'
    '#include "voiceqas/json_util.hpp"\n'
    '#include "voiceqas/media/session.hpp"\n'
    '#include "voiceqas/ops/event_store.hpp"\n'
    '#include "voiceqas/ops/metrics_hub.hpp"\n'
    '#include "voiceqas/ops/pipeline_tracker.hpp"\n'
    '#include "voiceqas/ops/prometheus.hpp"\n'
    '#include "voiceqas/ops/webhook.hpp"\n'
    '#include "voiceqas/rtp/depacketizer.hpp"\n'
    '#include "voiceqas/stt/json_util.hpp"\n'
    '#include "voiceqas/stt/model_util.hpp"\n'
    '#include "voiceqas/stt/vad_model.hpp"\n'
    '#include "voiceqas/wav.hpp"\n\n'
    "#include <httplib.h>\n"
    "#include <nlohmann/json.hpp>\n\n"
    "#include <cstring>\n"
    "#include <sstream>\n\n"
    "namespace voiceqas::routes {\n\n"
    "void register_all_routes(httplib::Server& server, const RouteContext& ctx) {\n"
    + body
    + "\n}\n\n"
    "void register_static_routes(httplib::Server&, const RouteContext&) {}\n"
    "void register_vqa_routes(httplib::Server&, const RouteContext&) {}\n"
    "void register_stt_routes(httplib::Server&, const RouteContext&) {}\n"
    "void register_media_routes(httplib::Server&, const RouteContext&) {}\n"
    "void register_ops_routes(httplib::Server&, const RouteContext&) {}\n\n"
    "}  // namespace voiceqas::routes\n"
)
(root / "register_routes.cpp").write_text(cpp, encoding="utf-8")
print("ok", len(cpp))
