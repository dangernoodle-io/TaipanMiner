#!/usr/bin/env python3
"""TLS sink test row via the supported config->reboot->verify path.
Usage: tlsrow.py <device_host> <step>
  step: mqtt_plain mqtt_stls mqtt_mtls http_plain http_tls
"""
import json, os, sys, time, urllib.request, urllib.error, subprocess

DEV = sys.argv[1]; STEP = sys.argv[2]
HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.environ.get("BB_TEST_RECEIVER", "172.16.1.100")
CERTS = os.environ.get("BB_TEST_CERTS") or os.path.normpath(os.path.join(HERE, "../../../mqtt-stack/certs"))
def rd(p):
    with open(p) as f: return f.read()
CA=rd(f"{CERTS}/ca.crt"); CRT=rd(f"{CERTS}/client.crt"); KEY=rd(f"{CERTS}/client.key")

def req(method, path, body=None, timeout=20):
    url=f"http://{DEV}{path}"
    data=json.dumps(body).encode() if body is not None else None
    r=urllib.request.Request(url, data=data, method=method)
    if data: r.add_header("Content-Type","application/json")
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp: return resp.status, resp.read().decode()
    except urllib.error.HTTPError as e: return e.code, e.read().decode()
    except Exception as e: return None, str(e)

def patch(b):
    st,_=req("PATCH","/api/telemetry",b)
    short={k:(f"<{len(v)}B>" if isinstance(v,str) and len(v)>40 else v) for k,v in list(b.values())[0].items()}
    print(f"  PATCH {list(b.keys())[0]}={short} -> {st}"); return st

# 1. configure (disable the other sink, set target config+certs+enabled)
print(f"=== {STEP} on {DEV} (config->reboot->verify) ===")
if STEP.startswith("mqtt"):
    patch({"http":{"enabled":False}})
    cfg={"tls":False,"enabled":True}
    if STEP=="mqtt_plain": cfg.update(uri=f"mqtt://{HOST}:1883")
    if STEP=="mqtt_stls":  cfg.update(uri=f"mqtts://{HOST}:8883", tls=True, tls_ca=CA)
    if STEP=="mqtt_mtls":  cfg.update(uri=f"mqtts://{HOST}:8884", tls=True, tls_ca=CA, tls_cert=CRT, tls_key=KEY)
    patch({"mqtt":cfg}); expect=("broker","mqtt")
else:
    patch({"mqtt":{"enabled":False}})
    cfg={"enabled":True}
    if STEP=="http_plain": cfg.update(base=f"http://{HOST}:9880")
    if STEP=="http_tls":   cfg.update(base=f"https://{HOST}:9881", tls_ca=CA)
    patch({"http":cfg}); expect=("transport","http")

# verify *_set flags persisted (cert upload integrity)
st,b=req("GET","/api/telemetry"); t=json.loads(b)
sec=t["mqtt"] if STEP.startswith("mqtt") else t["http"]
print(f"  flags: ca_set={sec.get('ca_set')} cert_set={sec.get('cert_set')} key_set={sec.get('key_set')}")

# 2. reboot
print("  rebooting..."); req("POST","/api/reboot")
time.sleep(6)
for i in range(15):
    st,b=req("GET","/api/health",timeout=4)
    if st==200: print(f"  up ~{6+i*3}s"); break
    time.sleep(3)
time.sleep(12)  # let it connect + publish a cycle

# 3. verify
st,b=req("GET","/api/telemetry"); t=json.loads(b)
m=t["mqtt"]; h=t["http"]; p=t["publisher"]
print(f"  mqtt: en={m['enabled']} conn={m.get('connected')} tls={m['tls']} uri={m['uri']}")
print(f"  http: en={h['enabled']} base={h['base']}")
print(f"  pub : last_ok={p['last_publish_ok']} age={p['last_publish_age_ms']} sinks={p['sink_count']}")
st,b=req("GET","/api/diag/heap"); hp=json.loads(b)["internal"]
print(f"  heap: free={hp['free']} largest={hp['largest_free_block']} min_ever={hp['minimum_ever_free']}")

key,val=expect
q=(f'from(bucket:"metrics")|>range(start:-30s)|>filter(fn:(r)=>r.host=="wroom32-1" and r["{key}"]=="{val}")'
   f'|>group(columns:["_measurement"])|>last()|>keep(columns:["_measurement","{key}"])')
out=subprocess.run(["docker","exec","influxdb","influx","query",q,"--org","dangernoodle",
    "--token","dev-token-please-change","--raw"],capture_output=True,text=True,timeout=30).stdout
rows=[l for l in out.splitlines() if val in l and "_measurement" not in l and "#" not in l]
print(f"  influx[{key}={val}] fresh measurements: {len(rows)}")
ok = m.get('connected') if STEP.startswith("mqtt") else (p['last_publish_ok'] and p['last_publish_age_ms']<20000)
print("  RESULT:", "PASS" if (len(rows)>0 and (ok if ok is not None else True)) else "CHECK")
