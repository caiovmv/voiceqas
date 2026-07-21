#!/usr/bin/env python3
"""Rewrite Grafana Sankey panel: single echarts blob + robust getOption."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / "deploy/observability/grafana/dashboards/voiceqas-pipeline-sankey.json"
dash = json.loads(path.read_text(encoding="utf-8"))

GET_OPTION = r"""const readField = (frame, name) => {
  const field = frame?.fields?.find((f) => f.name === name);
  if (!field?.values) {
    return undefined;
  }
  const values = field.values;
  if (typeof values.get === 'function') {
    return values.get(0);
  }
  if (values.buffer?.length) {
    return values.buffer[0];
  }
  return values[0];
};

const frame = context.panel.data.series?.[0];
const raw = readField(frame, 'echarts');
if (!raw) {
  return {
    title: { text: 'Pipeline Sankey', subtext: 'Aguardando tráfego (Tester :3000/#tester ou RTP UDP :10000)' },
    series: [{ type: 'sankey', data: [{ name: 'waiting' }], links: [] }],
  };
}

const opt = typeof raw === 'string' ? JSON.parse(raw) : raw;
const series = opt?.series?.[0] ?? {};
const links = series.links ?? [];
const data = series.data ?? [];

if (!links.length) {
  return {
    title: { text: 'Pipeline Sankey', subtext: 'Sessão sem fluxo — selecione All ou outra session_id' },
    series: [{ type: 'sankey', data, links: [] }],
  };
}

return {
  title: { text: 'Audio Pipeline Sankey', subtext: links.length + ' active links' },
  tooltip: opt.tooltip ?? { trigger: 'item' },
  series: [{
    type: 'sankey',
    left: '2%',
    right: '16%',
    top: '10%',
    bottom: '6%',
    nodeWidth: 16,
    nodeGap: 12,
    emphasis: { focus: 'adjacency' },
    lineStyle: { color: 'gradient', curveness: 0.5 },
    label: {
      color: context.grafana.theme.isDark ? '#e6edf3' : '#24292f',
      formatter: (p) => p.data.label || p.name,
    },
    data,
    links,
  }],
};"""

SNAPSHOT_QUERY = {
    "type": "json",
    "source": "url",
    "format": "table",
    "url": "/v1/ops/pipeline/snapshot",
    "url_options": {
        "method": "GET",
        "params": [{"key": "session_id", "value": "${session_id}"}],
    },
    "root_selector": "",
    "columns": [{"selector": "echarts", "text": "echarts", "type": "string"}],
    "parser": "backend",
    "filters": [],
    "datasource": {"type": "yesoreyeram-infinity-datasource", "uid": "voiceqas-api"},
}

for panel in dash["panels"]:
    if panel.get("id") == 20:
        panel["options"] = {
            "renderer": "canvas",
            "editor": {"format": "auto"},
            "getOption": GET_OPTION,
        }
        panel["targets"] = [
            {
                **SNAPSHOT_QUERY,
                "refId": "A",
            }
        ]
        break

for var in dash["templating"]["list"]:
    if var.get("name") == "session_id":
        var["allValue"] = "$__all"
        var["current"] = {"selected": True, "text": "All", "value": "$__all"}
        var["refresh"] = 1
        break

dash["refresh"] = "30s"
dash["version"] = dash.get("version", 1) + 1
path.write_text(json.dumps(dash, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"updated {path.name} -> version {dash['version']}, refresh={dash['refresh']}")
