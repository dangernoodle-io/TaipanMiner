#!/usr/bin/env python3
# Fleet health monitor: per board per cycle — reset_reason (crash detect),
# free heap, the active transport (mqtt/http + tls), and publisher state.
# usage: fleetmon.py [cycles] [interval_s]
import json, time, sys, urllib.request
BOARDS = [
    ("wroom32-1", "172.16.1.81"),
    ("s2mini-1",  "172.16.1.107"),
    ("c3mini-1",  "172.16.1.110"),
    ("tdongles3-1","172.16.1.71"),
    ("bitaxe601-1","172.16.1.68"),
]
def get(ip, path):
    try: return json.loads(urllib.request.urlopen(f"http://{ip}{path}", timeout=5).read())
    except Exception: return None
N = int(sys.argv[1]) if len(sys.argv) > 1 else 30
INT = int(sys.argv[2]) if len(sys.argv) > 2 else 60
ABNORMAL = ("panic", "task_wdt", "int_wdt", "brownout", "unknown")
print(f"fleet monitor: {N} cycles @ {INT}s — watching reset / free-heap / transport / publisher")
for c in range(N):
    ts = time.strftime("%H:%M:%S")
    for name, ip in BOARDS:
        info = get(ip, "/api/info"); tel = get(ip, "/api/telemetry")
        if not info:
            print(f"{ts} {name:12} *** DOWN / unresponsive ***"); continue
        reset = str(info.get("reset_reason")); free = info.get("free_heap")
        if tel:
            m, h, p = tel["mqtt"], tel["http"], tel["publisher"]
            if m["enabled"]:   trans = f"mqtt(conn={m['connected']},tls={m['tls']})"
            elif h["enabled"]: trans = f"http({'https' if 'https' in h['base'] else 'plain'})"
            else:              trans = "none"
            pub = f"ok={p['last_publish_ok']} age={p['last_publish_age_ms']} sinks={p['sink_count']}"
        else:
            trans, pub = "?", "no-telemetry"
        flag = "   <<<<< ABNORMAL RESET" if any(a in reset for a in ABNORMAL) else ""
        print(f"{ts} {name:12} reset={reset:9} free={free} {trans:26} {pub}{flag}")
    print("-" * 8)
    time.sleep(INT)
print("monitor done")
