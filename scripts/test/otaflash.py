#!/usr/bin/env python3
# OTA-push a firmware .bin to a device, wait for reboot onto the new version.
# usage: otaflash.py <host> <bin> [--mark-valid]
#   --mark-valid: after the device boots the new image, POST /api/update/mark-valid
#     to cancel the rollback (otherwise a reboot before self-validation rolls back).
import sys, time, json, urllib.request, urllib.error
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
MARK_VALID = "--mark-valid" in sys.argv
DEV, BIN = ARGS[0], ARGS[1]
def info():
    try: return json.loads(urllib.request.urlopen(f"http://{DEV}/api/info", timeout=5).read())
    except Exception: return None
def post(path, t=10):
    try:
        r = urllib.request.Request(f"http://{DEV}{path}", data=b"", method="POST")
        with urllib.request.urlopen(r, timeout=t) as x: return x.status, x.read().decode()[:120]
    except urllib.error.HTTPError as e: return e.code, e.read().decode()[:160]
    except Exception as e: return None, str(e)
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
        print(f"{DEV}: UP on {i.get('version')} (free={i.get('free_heap')}) after ~{(s+1)*5}s")
        if MARK_VALID:
            time.sleep(3)  # let the http server settle
            st, body = post("/api/update/mark-valid")
            iv = info()
            print(f"{DEV}: mark-valid HTTP {st} {body} -> ota_validated={iv and iv.get('ota_validated')}")
        sys.exit(0)
print(f"{DEV}: did NOT come back on a new version (still {info() and info().get('version')})"); sys.exit(2)
