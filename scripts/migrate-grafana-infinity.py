#!/usr/bin/env python3
"""Migrate VoiceQAS Grafana dashboards from JSON API to Infinity datasource."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DASH = ROOT / "deploy/observability/grafana/dashboards/voiceqas-pipeline-sankey.json"
DS_TYPE = "yesoreyeram-infinity-datasource"


def infinity_query(
    url: str,
    *,
    root_selector: str = "",
    columns: list[dict],
    params: list[dict] | None = None,
) -> dict:
    return {
        "type": "json",
        "source": "url",
        "format": "table",
        "url": url,
        "url_options": {
            "method": "GET",
            "params": params or [],
        },
        "root_selector": root_selector,
        "columns": columns,
        "parser": "backend",
        "filters": [],
    }


def set_datasource(obj: dict) -> None:
    if isinstance(obj, dict):
        if obj.get("type") == "marcusolsson-json-datasource":
            obj["type"] = DS_TYPE
        for value in obj.values():
            set_datasource(value)
    elif isinstance(obj, list):
        for item in obj:
            set_datasource(item)


def convert_target(target: dict) -> dict:
    url = target.get("urlPath", "")
    params = []
    if "queryParams" in target:
        for part in target["queryParams"].split("&"):
            if not part:
                continue
            key, _, value = part.partition("=")
            params.append({"key": key, "value": value})

    fields = target.get("fields", [])
    if not fields:
        return target

    if len(fields) == 1 and fields[0].get("jsonPath") == "$.echarts":
        query = infinity_query(
            url,
            columns=[{"selector": "echarts", "text": "echarts", "type": "string"}],
            params=params,
        )
    elif fields[0].get("jsonPath", "").startswith("$.active_sessions"):
        query = infinity_query(
            url,
            root_selector="active_sessions",
            columns=[
                {"selector": "session_id", "text": "session_id", "type": "string"},
                {"selector": "codec", "text": "codec", "type": "string"},
                {"selector": "composite_score", "text": "composite_score", "type": "number"},
                {"selector": "last_seen_ms", "text": "last_seen_ms", "type": "number"},
            ],
        )
    elif fields[0].get("jsonPath", "").startswith("$.finished_sessions"):
        query = infinity_query(
            url,
            root_selector="finished_sessions",
            columns=[
                {"selector": "session_id", "text": "session_id", "type": "string"},
                {"selector": "reason", "text": "reason", "type": "string"},
                {"selector": "composite_score", "text": "composite_score", "type": "number"},
                {"selector": "duration_ms", "text": "duration_ms", "type": "number"},
                {"selector": "finished_ms", "text": "finished_ms", "type": "number"},
            ],
        )
    else:
        columns = []
        root = ""
        for field in fields:
            path = field.get("jsonPath", "")
            name = field.get("name", "value")
            ftype = field.get("type", "string")
            if "[*]" in path:
                root = path.split("[*]")[0].removeprefix("$.")
                selector = path.split("[*].")[-1]
            else:
                selector = path.removeprefix("$.")
            columns.append({"selector": selector, "text": name, "type": ftype})
        query = infinity_query(url, root_selector=root, columns=columns, params=params)

    ref_id = target.get("refId", "A")
    return {
        "refId": ref_id,
        "datasource": {"type": DS_TYPE, "uid": "voiceqas-api"},
        **query,
    }


def main() -> None:
    dash = json.loads(DASH.read_text(encoding="utf-8"))
    set_datasource(dash)

    for panel in dash.get("panels", []):
        if "targets" not in panel:
            continue
        panel["targets"] = [convert_target(t) for t in panel["targets"]]

    for var in dash.get("templating", {}).get("list", []):
        if var.get("name") != "session_id":
            continue
        var["datasource"] = {"type": DS_TYPE, "uid": "voiceqas-api"}
        var["query"] = {
            "queryType": "",
            "infinityQuery": infinity_query(
                "/v1/ops/pipeline/sessions",
                root_selector="session_ids",
                columns=[{"selector": ".", "text": "session_id", "type": "string"}],
            ),
        }

    dash["version"] = dash.get("version", 1) + 1
    DASH.write_text(json.dumps(dash, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"migrated {DASH.name} -> version {dash['version']}")


if __name__ == "__main__":
    main()
