#!/usr/bin/env python3
# OTA-push a firmware .bin to a device, wait for reboot onto the new version.
# usage: otaflash.py <host> <bin>
import sys, time, json, urllib.request, urllib.error
DEV, BIN = sys.argv[1], sys.argv[2]
def info():
    try: return json.loads(urllib.request.urlopen(f"http://{DEV}/api/info", timeout=5).read())
    except Exception: return None
i0 = info()
v0 = i0.get("version") if i0 else None
print(f"{DEV}: current version {v0}")
data = open(BIN, "rb").read()
print(f"{DEV}: pushing {len(data)} bytes...")
r = urllib.request.Request(f"http://{DEV}/api/update/push", data=data, method="POST")
r.add_header("Content-Type", "application/octet-stream")
try:
    with urllib.request.urlopen(r, timeout=180) as resp:
        print(f"{DEV}: push HTTP {resp.status} {resp.read().decode()[:120]}")
except urllib.error.HTTPError as e:
    print(f"{DEV}: push HTTP {e.code} {e.read().decode()[:200]}"); sys.exit(1)
except Exception as e:
    # device may reboot before sending a full response — treat as maybe-ok, verify below
    print(f"{DEV}: push conn closed ({e}) — verifying reboot")
# wait for reboot + new version
for s in range(60):
    time.sleep(5)
    i = info()
    if i and i.get("version") and i.get("version") != v0:
        print(f"{DEV}: UP on {i.get('version')} (free={i.get('free_heap')}) after ~{(s+1)*5}s"); sys.exit(0)
print(f"{DEV}: did NOT come back on a new version (still {info() and info().get('version')})"); sys.exit(2)
