#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parent
src = (ROOT / "register_routes.cpp").read_text(encoding="utf-8")
start = src.index("void register_all_routes")
ob = src.index("{", start)
depth = 0
end = ob
for i, ch in enumerate(src[ob:], ob):
    if ch == "{":
        depth += 1
    elif ch == "}":
        depth -= 1
        if depth == 0:
            end = i
            break
lines = src[ob + 1 : end].splitlines(keepends=True)

COMMON = (
    '#include "voiceqas/server/routes/register_routes.hpp"\n\n'
    '#include "voiceqas/server/auth.hpp"\n'
    '#include "voiceqas/server/routes/route_helpers.hpp"\n\n'
)


def write(name: str, func: str, extra: str, content: str) -> None:
    out = (
        COMMON
        + extra
        + "namespace voiceqas::routes {\n\n"
        + f"void {func}(httplib::Server& server, const RouteContext& ctx) {{\n"
        + content
        + "}\n\n"
        + "}  // namespace voiceqas::routes\n"
    )
    (ROOT / name).write_text(out, encoding="utf-8")
    print(name, len(content))


write(
    "static_routes.cpp",
    "register_static_routes",
    '#include "voiceqas/ops/metrics_hub.hpp"\n#include "voiceqas/ops/prometheus.hpp"\n\n#include <sstream>\n\n',
    "".join(lines[1:83]),
)
write(
    "vqa_routes.cpp",
    "register_vqa_routes",
    '#include "voiceqas/json_util.hpp"\n#include "voiceqas/wav.hpp"\n\n#include <cstring>\n\n',
    "".join(lines[83:217]) + "".join(lines[628:]),
)
write(
    "media_routes.cpp",
    "register_media_routes",
    '#include "voiceqas/audio/ingress_codec.hpp"\n#include "voiceqas/media/session.hpp"\n#include "voiceqas/ops/webhook.hpp"\n\n#include <cstring>\n\n',
    "".join(lines[217:347]),
)
write(
    "ops_routes.cpp",
    "register_ops_routes",
    '#include "voiceqas/ops/event_store.hpp"\n#include "voiceqas/ops/pipeline_tracker.hpp"\n\n',
    "".join(lines[347:420]),
)
write(
    "stt_routes.cpp",
    "register_stt_routes",
    '#include "voiceqas/stt/json_util.hpp"\n#include "voiceqas/stt/model_util.hpp"\n#include "voiceqas/stt/vad_model.hpp"\n#include "voiceqas/wav.hpp"\n\n#include <cstring>\n\n',
    "".join(lines[420:628]),
)

(ROOT / "register_routes.cpp").write_text(
    '#include "voiceqas/server/routes/register_routes.hpp"\n\n'
    "namespace voiceqas::routes {\n\n"
    "void register_all_routes(httplib::Server& server, const RouteContext& ctx) {\n"
    "    register_static_routes(server, ctx);\n"
    "    register_vqa_routes(server, ctx);\n"
    "    register_media_routes(server, ctx);\n"
    "    register_ops_routes(server, ctx);\n"
    "    register_stt_routes(server, ctx);\n"
    "}\n\n"
    "}  // namespace voiceqas::routes\n",
    encoding="utf-8",
)
print("register_routes.cpp updated")
