#!/usr/bin/env bash
# Telemetry soak monitor: poll the dev fleet on an interval and log heap / resets
# / publish / buffer state. Flags any reboot (uptime drop) or low min-ever-free.
# Usage: soak_monitor.sh [interval_s]   (default 300); logs to $BB_SOAK_LOG or /tmp/bb_soak.log
set -u
INT="${1:-300}"
LOG="${BB_SOAK_LOG:-/tmp/bb_soak.log}"
FLEET="wroom32:81 bitaxe:68 tdongle:71 c3:110 s2:107"
declare -A LAST_UP
echo "soak start interval=${INT}s -> $LOG" | tee -a "$LOG"
while true; do
  ts=$(date '+%H:%M:%S' 2>/dev/null || echo "?")
  for e in $FLEET; do
    nm=${e%:*}; ip=${e#*:}
    line=$(curl -s -m8 "http://172.16.1.$ip/api/telemetry" 2>/dev/null | python3 -c '
import sys,json
try:
  d=json.load(sys.stdin); p=d.get("publisher",{}); m=d.get("mqtt",{})
  print("mqtt=%s pub_ok=%s buf=%s drop=%s"%(m.get("connected"),p.get("last_publish_ok"),p.get("buffer_count"),p.get("buffer_dropped")))
except: print("UNREACHABLE")' 2>/dev/null)
    heap=$(curl -s -m8 "http://172.16.1.$ip/api/diag/heap" 2>/dev/null | python3 -c '
import sys,json
try:
  i=json.load(sys.stdin).get("internal",{}); print("free=%s min=%s"%(i.get("free"),i.get("minimum_ever_free")))
except: print("")' 2>/dev/null)
    boot=$(curl -s -m8 "http://172.16.1.$ip/api/info" 2>/dev/null | python3 -c '
import sys,json
try:
  d=json.load(sys.stdin); print("up=%.0fs"%((d.get("uptime_ms") or 0)/1000))
except: print("up=?")' 2>/dev/null)
    up=$(echo "$boot" | grep -oE '[0-9]+' | head -1)
    flag=""
    [ -n "${LAST_UP[$nm]:-}" ] && [ -n "$up" ] && [ "$up" -lt "${LAST_UP[$nm]}" ] && flag=" *** REBOOTED ***"
    LAST_UP[$nm]=$up
    mn=$(echo "$heap" | grep -oE 'min=[0-9]+' | grep -oE '[0-9]+')
    [ -n "$mn" ] && [ "$mn" -lt 2048 ] && flag="$flag *** LOW-HEAP ***"
    echo "$ts $nm $boot $heap $line$flag" | tee -a "$LOG"
  done
  echo "---" >> "$LOG"
  sleep "$INT"
done
