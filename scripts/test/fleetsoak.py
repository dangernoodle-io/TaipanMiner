#!/usr/bin/env python3
# Per-board soak rotation. Cycles a board through a list of transport paths.
# For each path: configure (disable other sink, set cfg+certs, enable) -> reboot
# -> verify -> dwell D minutes polling heap/reset/publisher each minute and
# firing one update-check mid-dwell (concurrent sink+HTTPS-update-check TLS
# stress). Loops forever. Run one process per board in parallel.
# Note: the exclusive-sink arbiter means only ONE sink is ever active; the only
# concurrent-TLS path is active-sink + the HTTPS update-check.
# usage: fleetsoak.py <host> <hosttag> <dwell_min> <path1> [path2 ...]
#   path in: mqtt_plain mqtt_stls mqtt_mtls http_plain http_tls
import json, os, sys, time, urllib.request, urllib.error
DEV, TAG, DWELL = sys.argv[1], sys.argv[2], int(sys.argv[3])
PATHS = sys.argv[4:]
HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.environ.get("BB_TEST_RECEIVER", "172.16.1.100")
CERTS = os.environ.get("BB_TEST_CERTS") or os.path.normpath(os.path.join(HERE, "../../../mqtt-stack/certs"))
def rd(p):
    with open(p) as f: return f.read()
CA = rd(f"{CERTS}/ca.crt"); CRT = rd(f"{CERTS}/client.crt"); KEY = rd(f"{CERTS}/client.key")
ABN = ("panic", "task_wdt", "int_wdt", "brownout")
def req(m, p, b=None, t=20):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(f"http://{DEV}{p}", data=d, method=m)
    if d: r.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(r, timeout=t) as x: return x.status, x.read().decode()
    except urllib.error.HTTPError as e: return e.code, e.read().decode()
    except Exception as e: return None, str(e)
def gj(p, t=8):
    _, b = req("GET", p, None, t)
    try: return json.loads(b)
    except Exception: return None
def log(m): print(f"[{time.strftime('%H:%M:%S')} {TAG}] {m}", flush=True)
def configure(step):
    if step.startswith("mqtt"):
        req("PATCH", "/api/telemetry", {"http": {"enabled": False}})
        cfg = {"tls": False, "enabled": True}
        if step == "mqtt_plain": cfg.update(uri=f"mqtt://{HOST}:1883")
        if step == "mqtt_stls":  cfg.update(uri=f"mqtts://{HOST}:8883", tls=True, tls_ca=CA)
        if step == "mqtt_mtls":  cfg.update(uri=f"mqtts://{HOST}:8884", tls=True, tls_ca=CA, tls_cert=CRT, tls_key=KEY)
        req("PATCH", "/api/telemetry", {"mqtt": cfg})
    else:
        req("PATCH", "/api/telemetry", {"mqtt": {"enabled": False}})
        cfg = {"enabled": True}
        if step == "http_plain": cfg.update(base=f"http://{HOST}:9880")
        if step == "http_tls":   cfg.update(base=f"https://{HOST}:9881", tls_ca=CA)
        req("PATCH", "/api/telemetry", {"http": cfg})
def reboot_wait():
    req("POST", "/api/reboot"); time.sleep(7)
    for i in range(20):
        if gj("/api/health", 4): return 7 + i * 3
        time.sleep(3)
    return -1
log(f"soak start: dwell={DWELL}m paths={PATHS}")
while True:
    for step in PATHS:
        log(f"--- configure {step} ---")
        configure(step)
        tel = gj("/api/telemetry")
        sec = (tel or {}).get("mqtt" if step.startswith("mqtt") else "http", {})
        log(f"cfg flags ca={sec.get('ca_set')} cert={sec.get('cert_set')} key={sec.get('key_set')}")
        up = reboot_wait(); log(f"reboot -> up={up}s")
        time.sleep(12)
        tel = gj("/api/telemetry")
        if tel:
            m, h, p = tel["mqtt"], tel["http"], tel["publisher"]
            tr = (f"mqtt(conn={m['connected']},tls={m['tls']})" if m["enabled"]
                  else (f"http({'tls' if 'https' in h['base'] else 'plain'})" if h["enabled"] else "none"))
            log(f"post-boot {tr} pub_ok={p['last_publish_ok']} age={p['last_publish_age_ms']} sinks={p['sink_count']}")
        reset0 = (gj("/api/info") or {}).get("reset_reason")
        for minute in range(DWELL):
            time.sleep(60)
            info = gj("/api/info"); heap = gj("/api/diag/heap")
            if not info: log(f"t={minute+1}m *** DOWN ***"); continue
            hp = heap["internal"] if heap else {}
            rr = info.get("reset_reason")
            flag = " <<< ABNORMAL RESET" if (rr not in (reset0, "software", "power-on") and rr in ABN) else ""
            tel = gj("/api/telemetry"); p = (tel or {}).get("publisher", {})
            log(f"t={minute+1}m free={info.get('free_heap')} blk={hp.get('largest_free_block')} "
                f"min={hp.get('minimum_ever_free')} pub_ok={p.get('last_publish_ok')} "
                f"age={p.get('last_publish_age_ms')} reset={rr}{flag}")
            if minute == DWELL // 2:
                h0 = info.get("free_heap")
                rc, _ = req("POST", "/api/update/check"); time.sleep(20)
                st = gj("/api/update/status"); i2 = gj("/api/info")
                rr2 = i2.get("reset_reason") if i2 else None
                cr = rr2 in ABN and rr2 not in (reset0, "software", "power-on")
                log(f"  update-check POST={rc} ok={st and st.get('last_check_ok')} "
                    f"avail={st and st.get('available')} free {h0}->{i2 and i2.get('free_heap')}"
                    f"{'  <<< CRASHED ON UPDATE-CHECK' if cr else ''}")
