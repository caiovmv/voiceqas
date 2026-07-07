FROM grafana/grafana:11.5.2

USER root

# Plugins live outside grafana-data volume (see GF_PATHS_PLUGINS in docker-compose).
RUN mkdir -p /etc/grafana/bundled-plugins \
 && grafana cli --pluginsDir /etc/grafana/bundled-plugins plugins install volkovlabs-echarts-panel \
 && grafana cli --pluginsDir /etc/grafana/bundled-plugins plugins install marcusolsson-json-datasource 1.3.27

USER grafana
