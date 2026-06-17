#!/usr/bin/env python3
# Serialized OTA-pull driver: pull each board to the latest GitHub release, one at
# a time, confirm it reboots onto the new version, flag wedges, continue on failure.
# Usage: fleetpull.py <expected_version> <ip1> <ip2> ...
import sys, json, time, urllib.request

EXPECT = sys.argv[1]            # e.g. v0.29.0
IPS = sys.argv[2:]
PER_BOARD_TIMEOUT = 220         # seconds to reach the new version


def req(method, ip, path, timeout=15):
    r = urllib.request.Request(f"http://{ip}{path}", method=method)
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        return resp.status, resp.read().decode()


def ver(ip):
    try:
        _, b = req("GET", ip, "/api/info", timeout=6)
        return json.loads(b).get("version", "")
    except Exception:
        return None  # unreachable


def status(ip):
    try:
        _, b = req("GET", ip, "/api/update/status", timeout=6)
        d = json.loads(b)
        return d.get("state") or d.get("status") or d.get("outcome"), d
    except Exception:
        return None, {}


results = {}
for ip in IPS:
    pre = ver(ip)
    print(f"\n=== {ip} : pre={pre} -> pull to {EXPECT} ===", flush=True)
    if pre is None:
        print(f"  {ip}: UNREACHABLE pre-pull — skipping", flush=True)
        results[ip] = "UNREACHABLE_PRE"
        continue
    if EXPECT in (pre or ""):
        print(f"  {ip}: already on {EXPECT} — skipping", flush=True)
        results[ip] = "ALREADY"
        continue
    try:
        sc, sb = req("POST", ip, "/api/update/check", timeout=30)
        print(f"  check -> {sc} {sb[:80]}", flush=True)
    except Exception as e:
        print(f"  check ERR: {e}", flush=True)
    time.sleep(3)
    try:
        ac, ab = req("POST", ip, "/api/update/apply", timeout=30)
        print(f"  apply -> {ac} {ab[:100]}", flush=True)
    except Exception as e:
        print(f"  apply ERR: {e} — skipping", flush=True)
        results[ip] = "APPLY_ERR"
        continue

    t0 = time.time()
    outcome = "TIMEOUT"
    unreach_since = None
    while time.time() - t0 < PER_BOARD_TIMEOUT:
        time.sleep(6)
        v = ver(ip)
        st, sd = status(ip)
        el = int(time.time() - t0)
        if v is None:
            if unreach_since is None:
                unreach_since = time.time()
            gone = int(time.time() - unreach_since)
            print(f"  +{el}s: unreachable {gone}s (rebooting?)", flush=True)
            if gone > 90:
                outcome = "WEDGED"
                break
            continue
        unreach_since = None
        if EXPECT in v:
            print(f"  +{el}s: UP on {v}", flush=True)
            outcome = "SUCCESS"
            break
        prog = sd.get("progress") or sd.get("percent") or sd.get("bytes") or ""
        print(f"  +{el}s: v={v} state={st} prog={prog}", flush=True)
        if st in ("error", "failed") and el > 20:
            outcome = f"FAILED({st})"
            break
    print(f"  => {ip}: {outcome}", flush=True)
    results[ip] = outcome

print("\n=== SUMMARY ===", flush=True)
for ip, r in results.items():
    print(f"  {ip}: {r}", flush=True)
