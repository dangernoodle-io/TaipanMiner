#!/usr/bin/env bash
# OTA-push a firmware image to a board and verify it boots clean on the new build.
# Usage: ota_push.sh <device_host> <firmware.bin>
set -u
D="$1"; FW="$2"
[ -f "$FW" ] || { echo "  $D: firmware missing: $FW"; exit 1; }

pre=$(curl -s -m 8 "http://$D/api/info" 2>/dev/null | python3 -c 'import sys,json;print(json.load(sys.stdin).get("version"))' 2>/dev/null)
echo "=== $D : push $(stat -f%z "$FW") bytes (was $pre) ==="
st=$(curl -s -m 180 -X POST "http://$D/api/update/push" -H 'Content-Type: application/octet-stream' \
       --data-binary @"$FW" -w '%{http_code}' -o /tmp/ota_resp 2>/dev/null)
echo "  push -> $st $(cat /tmp/ota_resp 2>/dev/null)"
[ "$st" = "200" ] || { echo "  PUSH FAILED"; exit 1; }

for i in $(seq 1 25); do curl -s -m 4 "http://$D/api/health" -o /dev/null 2>/dev/null && { echo "  up ~$((i*4))s"; break; }; sleep 4; done
sleep 12
curl -s -m 10 "http://$D/api/info" 2>/dev/null | python3 -c 'import sys,json
d=json.load(sys.stdin); print("  ver=%s ota_validated=%s" % (d.get("version"),d.get("ota_validated")))' 2>/dev/null
curl -s -m 10 "http://$D/api/telemetry" 2>/dev/null | python3 -c '
import sys,json
d=json.load(sys.stdin); m=d.get("mqtt",{}); h=d.get("http")
print("  mqtt(en=%s,conn=%s,tls=%s,certs=%s/%s/%s) http=%s" % (
  m.get("enabled"),m.get("connected"),m.get("tls"),
  m.get("ca_set"),m.get("cert_set"),m.get("key_set"),
  "ABSENT" if not h else "PRESENT(BUG)"))' 2>/dev/null
