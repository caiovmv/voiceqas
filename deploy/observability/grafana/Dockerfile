FROM grafana/grafana:12.4.5

USER root

# Plugins live outside grafana-data volume; entrypoint copies them to /var/lib/grafana/plugins.
RUN mkdir -p /etc/grafana/bundled-plugins \
 && grafana cli --pluginsDir /etc/grafana/bundled-plugins plugins install volkovlabs-echarts-panel \
 && grafana cli --pluginsDir /etc/grafana/bundled-plugins plugins install yesoreyeram-infinity-datasource

COPY enable-plugins.sh /usr/local/bin/enable-plugins.sh
RUN chmod +x /usr/local/bin/enable-plugins.sh

USER grafana

ENTRYPOINT ["/usr/local/bin/enable-plugins.sh"]
