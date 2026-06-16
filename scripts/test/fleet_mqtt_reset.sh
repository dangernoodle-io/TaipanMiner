#!/usr/bin/env bash
# Switch a board to MQTT plaintext and clear stale telemetry NVS (http sink +
# leftover TLS certs from the transport-matrix validation). Reboot-to-apply.
# Usage: fleet_mqtt_reset.sh <device_host> [broker_host]
set -u
D="$1"
BROKER="${2:-172.16.1.100}"

j() { curl -s -m 12 "$@" 2>/dev/null; }

echo "=== $D : reset to mqtt-plain + clear stale nvs ==="
if ! j "http://$D/api/telemetry" | grep -q '"mqtt"'; then
  echo "  no /api/telemetry (likely pre-telemetry firmware) — SKIP"; exit 0
fi

# 1. switch transport: http off, mqtt plaintext on
j -X PATCH "http://$D/api/telemetry" -H 'Content-Type: application/json' \
   -d '{"http":{"enabled":false}}' -o /dev/null -w '  http-off:%{http_code}\n'
j -X PATCH "http://$D/api/telemetry" -H 'Content-Type: application/json' \
   -d "{\"mqtt\":{\"enabled\":true,\"tls\":false,\"uri\":\"mqtt://$BROKER:1883\"}}" \
   -o /dev/null -w '  mqtt-plain-on:%{http_code}\n'

# 2. clear stale NVS — whole http sink namespace + mqtt TLS cert keys
del() { j -X DELETE "http://$D/api/nvs" -H 'Content-Type: application/json' -d "$1" -w " [%{http_code}]\n"; }
echo -n "  del bb_sink_http ns:"; del '{"namespace":"bb_sink_http","confirm":true}'
for k in tls_ca tls_cert tls_key; do
  echo -n "  del bb_mqtt/$k:"; del "{\"namespace\":\"bb_mqtt\",\"key\":\"$k\",\"confirm\":true}"
done

# 3. reboot-to-apply (B1-289)
j -X POST "http://$D/api/reboot" -o /dev/null -w '  reboot:%{http_code}\n'

# 4. wait + verify
for i in $(seq 1 15); do j "http://$D/api/health" -o /dev/null && break; sleep 3; done
sleep 10
j "http://$D/api/telemetry" | python3 -c '
import sys,json
try:
  d=json.load(sys.stdin); m=d.get("mqtt",{}); h=d.get("http")
  sec="ABSENT" if not h else ("present(en=%s,ca=%s)" % (h.get("enabled"),h.get("ca_set")))
  print("  AFTER: mqtt(en=%s,conn=%s,tls=%s,ca=%s,cert=%s,key=%s) http=%s" % (
        m.get("enabled"),m.get("connected"),m.get("tls"),
        m.get("ca_set"),m.get("cert_set"),m.get("key_set"),sec))
except Exception as e: print("  AFTER: parse-fail", e)
'
