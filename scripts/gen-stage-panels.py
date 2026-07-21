#!/usr/bin/env python3
"""Generate per-stage Grafana timeseries panels for voiceqas-pipeline-sankey dashboard."""
import json
from pathlib import Path

STAGES = [
    ("sip_in", "SIP Trunk (in)", [
        ("rate(voiceqas_pipeline_bytes_total{stage=\"sip_in\",direction=\"inbound\"}[1m])", "throughput", "Bps", "left"),
    ]),
    ("rtp_ingress", "RTP Ingress", [
        ("voiceqas_pipeline_jitter_ms{stage=\"rtp_ingress\",direction=\"inbound\"}", "jitter", "ms", "left"),
        ("voiceqas_pipeline_latency_ms{stage=\"rtp_ingress\",direction=\"inbound\"}", "latency", "ms", "left"),
        ("rate(voiceqas_pipeline_bytes_total{stage=\"rtp_ingress\",direction=\"inbound\"}[1m])", "throughput", "Bps", "right"),
    ]),
    ("decode_vqa", "Decode (VQA path)", [
        ("voiceqas_pipeline_latency_ms{stage=\"decode_vqa\",direction=\"inbound\"}", "decode latency", "ms", "left"),
        ("rate(voiceqas_pipeline_bytes_total{stage=\"decode_vqa\",direction=\"inbound\"}[1m])", "throughput", "Bps", "right"),
    ]),
    ("agc_vqa", "AGC Normalize (VQA)", [
        ("voiceqas_pipeline_latency_ms{stage=\"agc_vqa\",direction=\"inbound\"}", "agc latency", "ms", "left"),
    ]),
    ("enhancement", "Audio Enhancement", [
        ("rate(voiceqas_pipeline_bytes_total{stage=\"enhancement\",direction=\"inbound\"}[1m])", "throughput", "Bps", "left"),
    ]),
    ("vqa", "VQA Analyzer", [
        ("voiceqas_pipeline_composite_score{stage=\"vqa\",direction=\"inbound\"}", "composite score", "none", "left"),
        ("voiceqas_pipeline_jitter_ms{stage=\"vqa\",direction=\"inbound\"}", "jitter", "ms", "right"),
    ]),
    ("stt_gate", "STT Gate", [
        ("rate(voiceqas_pipeline_dropped_bytes_total{stage=\"stt_gate\",direction=\"inbound\"}[1m])", "dropped", "Bps", "left"),
        ("voiceqas_pipeline_composite_score{stage=\"stt_gate\",direction=\"inbound\"}", "VQA score", "none", "right"),
    ]),
    ("decode_stt", "Decode (STT path)", [
        ("voiceqas_pipeline_latency_ms{stage=\"decode_stt\",direction=\"inbound\"}", "decode latency", "ms", "left"),
        ("rate(voiceqas_pipeline_bytes_total{stage=\"decode_stt\",direction=\"inbound\"}[1m])", "throughput", "Bps", "right"),
    ]),
    ("agc_stt", "AGC (STT path)", [
        ("voiceqas_pipeline_latency_ms{stage=\"agc_stt\",direction=\"inbound\"}", "agc latency", "ms", "left"),
    ]),
    ("resample_16k", "Resample 16 kHz", [
        ("voiceqas_pipeline_latency_ms{stage=\"resample_16k\",direction=\"inbound\"}", "resample latency", "ms", "left"),
    ]),
    ("stt_buffer", "STT Buffer", [
        ("voiceqas_pipeline_bytes_total{stage=\"stt_buffer\",direction=\"inbound\"}", "buffer bytes", "bytes", "left"),
        ("voiceqas_pipeline_latency_ms{stage=\"stt_buffer\",direction=\"inbound\"}", "latency", "ms", "right"),
    ]),
]


def make_panel(idx: int, stage_id: str, title: str, targets: list, x: int, y: int) -> dict:
    overrides = []
    default_unit = targets[0][2] if targets[0][2] != "none" else "short"
    for legend, _, unit, axis in targets:
        if unit != default_unit or axis == "right":
            props = []
            if unit != "none":
                props.append({"id": "unit", "value": unit})
            if axis == "right":
                props.append({"id": "custom.axisPlacement", "value": "right"})
            overrides.append({
                "matcher": {"id": "byName", "options": legend},
                "properties": props,
            })

    defaults = {
        "color": {"mode": "palette-classic"},
        "custom": {
            "drawStyle": "line",
            "lineWidth": 2,
            "fillOpacity": 12,
            "gradientMode": "opacity",
        },
        "unit": default_unit,
    }
    if stage_id in ("vqa", "stt_gate"):
        for legend, _, unit, _ in targets:
            if unit == "none":
                overrides.append({
                    "matcher": {"id": "byName", "options": legend},
                    "properties": [
                        {"id": "unit", "value": "none"},
                        {"id": "min", "value": 0},
                        {"id": "max", "value": 100},
                    ],
                })

    prom_targets = []
    for i, (expr, legend, _, _) in enumerate(targets):
        prom_targets.append({
            "expr": expr,
            "legendFormat": legend,
            "refId": chr(65 + i),
        })

    return {
        "datasource": {"type": "prometheus", "uid": "prometheus"},
        "description": f"Fleet EWMA metrics for pipeline stage {stage_id}.",
        "fieldConfig": {"defaults": defaults, "overrides": overrides},
        "gridPos": {"h": 7, "w": 8, "x": x, "y": y},
        "id": 100 + idx,
        "options": {
            "legend": {
                "displayMode": "list",
                "placement": "bottom",
                "calcs": ["lastNotNull", "mean"],
            },
            "tooltip": {"mode": "multi", "sort": "desc"},
        },
        "targets": prom_targets,
        "title": f"{stage_id} — {title}",
        "type": "timeseries",
    }


def main() -> None:
    dashboard_path = Path(__file__).resolve().parents[1] / "deploy/observability/grafana/dashboards/voiceqas-pipeline-sankey.json"
    dashboard = json.loads(dashboard_path.read_text(encoding="utf-8"))

    # Remove old combined latency panels and any prior per-stage panels
    dashboard["panels"] = [
        p for p in dashboard["panels"]
        if p.get("id") not in (30, 31, 25) and not (100 <= p.get("id", 0) <= 110)
    ]

    row_panel = {
        "collapsed": False,
        "gridPos": {"h": 1, "w": 24, "x": 0, "y": 22},
        "id": 25,
        "panels": [],
        "title": "Pipeline Stages (per-step metrics)",
        "type": "row",
    }

    stage_panels = []
    for i, (sid, title, tgts) in enumerate(STAGES):
        row = i // 3
        col = i % 3
        stage_panels.append(make_panel(i, sid, title, tgts, col * 8, 23 + row * 7))

    # Shift tables below stage panels: 4 rows * 7 height = 28, starts at y=23, ends y=51
    tables_y = 51
    for panel in dashboard["panels"]:
        if panel.get("id") in (40, 41):
            panel["gridPos"]["y"] = tables_y

    # Insert row + stage panels after sankey (before tables)
    insert_at = next(i for i, p in enumerate(dashboard["panels"]) if p.get("id") == 40)
    dashboard["panels"][insert_at:insert_at] = [row_panel] + stage_panels

    dashboard["version"] = (dashboard.get("version") or 1) + 1
    dashboard_path.write_text(json.dumps(dashboard, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Updated {dashboard_path} with {len(stage_panels)} stage panels")


if __name__ == "__main__":
    main()
