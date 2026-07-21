#!/bin/sh
# Grafana 12+ ships community plugins disabled until an admin enables them.
# Plugins are baked into /etc/grafana/bundled-plugins (read-only, outside the
# grafana-data volume). Copy them into the default plugins dir on every boot so
# QueryData works (custom GF_PATHS_PLUGINS caused "plugin unavailable" on 12.4).
set -eu

PLUGIN_SRC="/etc/grafana/bundled-plugins"
PLUGIN_DST="/var/lib/grafana/plugins"

mkdir -p "${PLUGIN_DST}"
if [ -d "${PLUGIN_SRC}" ]; then
  cp -r "${PLUGIN_SRC}/." "${PLUGIN_DST}/"
  chown -R grafana:root "${PLUGIN_DST}" 2>/dev/null || true
fi

/run.sh &
grafana_pid=$!

admin_user="${GF_SECURITY_ADMIN_USER:-admin}"
admin_pass="${GF_SECURITY_ADMIN_PASSWORD:-admin}"

enable_plugin() {
  plugin_id="$1"
  for _ in $(seq 1 90); do
    if wget -qO- http://127.0.0.1:3000/api/health >/dev/null 2>&1; then
      break
    fi
    sleep 1
  done

  wget -qO- \
    --header="Content-Type: application/json" \
    --post-data='{"enabled":true}' \
    --http-user="${admin_user}" \
    --http-password="${admin_pass}" \
    "http://127.0.0.1:3000/api/plugins/${plugin_id}/settings" >/dev/null 2>&1 || true
}

enable_plugin yesoreyeram-infinity-datasource &
enable_plugin volkovlabs-echarts-panel &
wait

wait "${grafana_pid}"
