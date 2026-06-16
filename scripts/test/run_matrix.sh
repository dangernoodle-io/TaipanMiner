#!/usr/bin/env bash
# Strictly sequential telemetry transport matrix across the dev fleet.
# One row at a time, one process — no concurrency (avoids cross-board contamination).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROW="$HERE/tlsrow.py"
LOG="${BB_MATRIX_LOG:-/tmp/bb_matrix.log}"
: > "$LOG"

declare -A FLEET=(
  [172.16.1.81]="mqtt_plain mqtt_stls mqtt_mtls http_plain http_tls"   # wroom32 full
  [172.16.1.71]="mqtt_plain mqtt_stls mqtt_mtls http_plain http_tls"   # tdongle full
  [172.16.1.68]="mqtt_plain mqtt_stls mqtt_mtls http_plain http_tls"   # bitaxe full
  [172.16.1.107]="mqtt_plain http_plain"                               # s2 plaintext only
)
ORDER=(172.16.1.81 172.16.1.71 172.16.1.68 172.16.1.107)

pass=0; fail=0
for dev in "${ORDER[@]}"; do
  for step in ${FLEET[$dev]}; do
    echo "########## $dev $step ##########" | tee -a "$LOG"
    out="$(python3 "$ROW" "$dev" "$step" 2>&1)"
    echo "$out" | tee -a "$LOG"
    res="$(echo "$out" | grep -o 'RESULT: [A-Z/-]*' | tail -1)"
    if echo "$res" | grep -q PASS; then pass=$((pass+1)); else fail=$((fail+1)); fi
    echo "  >>> $dev/$step: ${res:-NO-RESULT}" | tee -a "$LOG"
  done
done
echo "========== MATRIX DONE: $pass pass, $fail fail ==========" | tee -a "$LOG"
