#!/usr/bin/env python3
# Periodic update-check sweep across the fleet. For each board: snapshot heap,
# POST /api/update/check, wait, read /api/update/status + heap delta. This
# exercises the heap-tight "update-check with an active telemetry sink" path
# (B1-280/B1-281) repeatedly on hardware and flags any crash/heap regression.
import json, time, urllib.request, urllib.error
BOARDS = [
    ("wroom32-1", "172.16.1.81"),
    ("s2mini-1",  "172.16.1.107"),
    ("c3mini-1",  "172.16.1.110"),
    ("tdongles3-1","172.16.1.71"),
    ("bitaxe601-1","172.16.1.68"),
]
def get(ip, path, t=8):
    try: return json.loads(urllib.request.urlopen(f"http://{ip}{path}", timeout=t).read())
    except Exception: return None
def post(ip, path, t=10):
    try:
        r = urllib.request.Request(f"http://{ip}{path}", data=b"", method="POST")
        with urllib.request.urlopen(r, timeout=t) as x: return x.status
    except urllib.error.HTTPError as e: return e.code
    except Exception as e: return f"ERR:{e}"
ts = time.strftime("%H:%M:%S")
print(f"=== update-check sweep {ts} ===")
for name, ip in BOARDS:
    h0 = get(ip, "/api/diag/heap"); reset0 = (get(ip, "/api/info") or {}).get("reset_reason")
    free0 = h0["internal"]["free"] if h0 else None
    blk0 = h0["internal"]["largest_free_block"] if h0 else None
    rc = post(ip, "/api/update/check")
    time.sleep(20)  # let the manifest fetch + TLS handshake complete
    st = get(ip, "/api/update/status")
    h1 = get(ip, "/api/diag/heap"); info1 = get(ip, "/api/info") or {}
    free1 = h1["internal"]["free"] if h1 else None
    reset1 = info1.get("reset_reason")
    if not info1:
        print(f"{name:12} *** DOWN after update-check (was reset={reset0}) ***"); continue
    crashed = reset1 not in (reset0, "software", "power-on") and reset1 in ("panic","task_wdt","int_wdt","brownout")
    okf = st.get("last_check_ok") if st else None
    en = st.get("enabled") if st else None
    avail = st.get("available") if st else None
    flag = "  <<<<< CRASHED ON UPDATE-CHECK" if crashed else ""
    print(f"{name:12} POST={rc} ok={okf} en={en} avail={avail} "
          f"free {free0}->{free1} blk0={blk0} reset={reset1}{flag}")
print("=== sweep done ===")
