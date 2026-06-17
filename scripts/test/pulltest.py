#!/usr/bin/env python3
# OTA-pull fragmentation probe: check -> apply -> poll status+heap during download.
# Usage: pulltest.py <host> [duration_s]
import sys, json, time, urllib.request

H = sys.argv[1]
DUR = int(sys.argv[2]) if len(sys.argv) > 2 else 120


def req(method, path, timeout=15):
    r = urllib.request.Request(f"http://{H}{path}", method=method)
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        return resp.status, resp.read().decode()


def gj(path):
    try:
        _, b = req("GET", path)
        return json.loads(b)
    except Exception as e:
        return {"_err": str(e)}


def heap():
    d = gj("/api/diag/heap").get("internal", {})
    return d.get("largest_free_block"), d.get("free"), d.get("minimum_ever_free")


print(f"=== pull test @ {H} ===")
i = gj("/api/info")
print("version:", i.get("version"))
lb, fr, mn = heap()
print(f"pre-apply heap: largest={lb} free={fr} min_ever={mn}")

try:
    st, b = req("POST", "/api/update/check", timeout=30)
    print("check ->", st, b[:200])
except Exception as e:
    print("check ERR:", e)
time.sleep(3)
print("status after check:", json.dumps(gj("/api/update/status")))

try:
    st, b = req("POST", "/api/update/apply", timeout=30)
    print("apply ->", st, b[:200])
except Exception as e:
    print("apply ERR:", e)

print("=== polling every 2s ===")
t0 = time.time()
last = None
while time.time() - t0 < DUR:
    s = gj("/api/update/status")
    lb, fr, mn = heap()
    state = s.get("state") or s.get("status")
    prog = s.get("progress") or s.get("percent") or s.get("bytes")
    err = s.get("error") or s.get("message") or ""
    line = f"  +{int(time.time()-t0):>3}s: largest={lb} free={fr} min={mn} state={state} prog={prog} {err}"
    print(line)
    # stop early on terminal states
    if state in ("error", "failed", "idle") and int(time.time() - t0) > 6:
        print("  -> terminal state, stopping")
        break
    if state in ("success", "done", "rebooting") or (isinstance(prog, (int, float)) and prog >= 100):
        print("  -> success/reboot, stopping")
        break
    time.sleep(2)

print("=== final ===")
print(json.dumps(gj("/api/update/status")))
