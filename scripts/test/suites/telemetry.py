"""Telemetry transport suite (NAME="telemetry") — folds tlsrow.py / run_matrix.sh.

For each transport row, PATCH /api/telemetry to configure the sink (MUTATING → via
ctx.guard), settle, then verify publish health under the no-false-sinks rule: a sink
counts as healthy ONLY on positive confirmation (mqtt.connected AND
publisher.last_publish_ok for mqtt rows; publisher.last_publish_ok + fresh age for
http rows), never on mere absence of error, and never while the other sink is still
enabled. Optional InfluxDB sink validation via `docker exec` is gated and skipped
when unavailable.

Config (no hardcoded IPs/paths, no secrets in code):
  BB_TEST_RECEIVER — receiver host (env, or ctx.extra['receiver'] / --receiver)
  BB_TEST_CERTS    — dir with ca.crt/client.crt/client.key (env, or ctx.extra['certs'] / --certs)
"""
from __future__ import annotations
import logging
import os
import subprocess
import sys
from typing import Optional, Tuple, TYPE_CHECKING

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

if TYPE_CHECKING:
    from suites import SuiteContext

from fleetlib.client import Client, TIMEOUT_INFO, TIMEOUT_WRITE
from fleetlib.criteria import for_profile
from fleetlib.profiles import profile_for
from fleetlib.results import ResultSet, Result, STATUS_PASS, STATUS_FAIL, STATUS_SKIP
from suites import gate_enabled

logger = logging.getLogger(__name__)

NAME = "telemetry"
HELP = "Telemetry transport — configure each telemetry transport and verify publish health"

ROWS = ["mqtt_plain", "mqtt_stls", "mqtt_mtls", "http_plain", "http_tls"]

# cert files (within BB_TEST_CERTS) required per row
_ROW_CERTS = {
    "mqtt_plain": (),
    "mqtt_stls": ("ca.crt",),
    "mqtt_mtls": ("ca.crt", "client.crt", "client.key"),
    "http_plain": (),
    "http_tls": ("ca.crt",),
}

_DEFAULT_PORTS = {
    "mqtt_plain": 1883,
    "mqtt_stls": 8883,
    "mqtt_mtls": 8884,
    "http_plain": 9880,
    "http_tls": 9881,
}

FRESH_AGE_MS = 20_000


def add_arguments(parser) -> None:
    parser.add_argument(
        "--rows", metavar="R,R,…", default=None,
        help=f"comma-separated subset of transport rows (default: all of {','.join(ROWS)})",
    )
    parser.add_argument(
        "--receiver", metavar="HOST", default=None,
        help="telemetry receiver host (overrides BB_TEST_RECEIVER)",
    )
    parser.add_argument(
        "--certs", metavar="DIR", default=None,
        help="dir with ca.crt/client.crt/client.key (overrides BB_TEST_CERTS)",
    )
    parser.add_argument(
        "--influx-container", default="influxdb",
        help="docker container running InfluxDB for sink validation (default: influxdb)",
    )


def _default_docker(args, timeout: int = 30):
    """Run `docker <args...>`. Returns (rc, output); rc None on error."""
    try:
        r = subprocess.run(["docker", *args], capture_output=True, text=True, timeout=timeout)
        return r.returncode, (r.stdout + r.stderr).strip()
    except FileNotFoundError:
        return None, "docker not found"
    except Exception as exc:  # noqa: BLE001
        return None, str(exc)


def _resolve_receiver(ctx) -> Optional[str]:
    return ctx.extra.get("receiver") or os.environ.get("BB_TEST_RECEIVER")


def _resolve_certs_dir(ctx) -> Optional[str]:
    return ctx.extra.get("certs") or os.environ.get("BB_TEST_CERTS")


def selected_rows(ctx) -> list:
    sel = ctx.extra.get("rows")
    if isinstance(sel, str):
        sel = [r.strip() for r in sel.split(",") if r.strip()]
    rows = sel or list(ROWS)
    return [r for r in rows if r in ROWS]


def _load_certs(certs_dir: str, needed) -> Tuple[Optional[dict], Optional[str]]:
    out: dict = {}
    name_to_key = {"ca.crt": "ca", "client.crt": "cert", "client.key": "key"}
    for fname in needed:
        path = os.path.join(certs_dir, fname)
        try:
            with open(path) as f:
                out[name_to_key[fname]] = f.read()
        except OSError as exc:
            return None, f"cert {fname} unavailable: {exc}"
    return out, None


def build_config(row: str, receiver: str, certs: dict, port: int) -> Tuple[dict, dict]:
    """Return (disable_other_sink_patch, enable_target_sink_patch) for a row."""
    if row.startswith("mqtt"):
        cfg = {"enabled": True, "tls": False}
        if row == "mqtt_plain":
            cfg["uri"] = f"mqtt://{receiver}:{port}"
        elif row == "mqtt_stls":
            cfg.update(uri=f"mqtts://{receiver}:{port}", tls=True, tls_ca=certs["ca"])
        elif row == "mqtt_mtls":
            cfg.update(uri=f"mqtts://{receiver}:{port}", tls=True,
                       tls_ca=certs["ca"], tls_cert=certs["cert"], tls_key=certs["key"])
        return {"http": {"enabled": False}}, {"mqtt": cfg}
    cfg = {"enabled": True}
    if row == "http_plain":
        cfg["base"] = f"http://{receiver}:{port}"
    elif row == "http_tls":
        cfg.update(base=f"https://{receiver}:{port}", tls_ca=certs["ca"])
    return {"mqtt": {"enabled": False}}, {"http": cfg}


def evaluate_row(row: str, telemetry, fresh_age_ms: int = FRESH_AGE_MS) -> Tuple[bool, str]:
    """No-false-sinks publish-health check.

    Healthy ONLY on positive confirmation; a disabled/never-connected sink, or a row
    where the other sink is still enabled, is never counted ok.
    """
    if telemetry is None:
        return False, "no /api/telemetry response"
    pub = telemetry.get("publisher") or {}
    mqtt = telemetry.get("mqtt") or {}
    http = telemetry.get("http") or {}

    if row.startswith("mqtt"):
        if http.get("enabled"):
            return False, "http sink still enabled (false-sink risk)"
        if not mqtt.get("enabled"):
            return False, "mqtt sink not enabled"
        if not mqtt.get("connected"):
            return False, "mqtt not connected"
        if not pub.get("last_publish_ok"):
            return False, "publisher.last_publish_ok false after mqtt.connected"
        return True, f"mqtt connected + publish ok (age {pub.get('last_publish_age_ms')}ms)"

    if mqtt.get("enabled"):
        return False, "mqtt sink still enabled (false-sink risk)"
    if not http.get("enabled"):
        return False, "http sink not enabled"
    if not pub.get("last_publish_ok"):
        return False, "publisher.last_publish_ok false"
    age = pub.get("last_publish_age_ms")
    if age is not None and age > fresh_age_ms:
        return False, f"publish stale: age {age}ms > {fresh_age_ms}ms"
    return True, f"http publish ok (age {age}ms)"


def _influx_validate(ctx, row: str, telemetry) -> Optional[str]:
    """Gated InfluxDB sink check via docker exec. Returns a note or None.

    Returns None when the 'influx' gate is off. Returns a 'skipped' note when
    docker/influx is unavailable (does not fail the row).
    """
    if not gate_enabled(ctx, "influx"):
        return None
    docker = ctx.extra.get("docker_runner") or _default_docker
    container = ctx.extra.get("influx_container") or "influxdb"
    rc, _out = docker(["exec", container, "influx", "ping"])
    if rc != 0:
        return f"influx check skipped: {container} unavailable"
    return "influx reachable"


def run(ctx: "SuiteContext") -> ResultSet:
    rs = ctx.results
    receiver = _resolve_receiver(ctx)
    certs_dir = _resolve_certs_dir(ctx)
    rows = selected_rows(ctx)

    for device in ctx.devices:
        _run_device(device, ctx, rs, rows, receiver, certs_dir)

    if ctx.out_json:
        rs.to_json(ctx.out_json)
    if ctx.out_junit:
        rs.to_junit(ctx.out_junit)
    return rs


def _run_device(device, ctx, rs, rows, receiver, certs_dir) -> None:
    profile = profile_for(device.board, ctx.profiles)
    criteria = for_profile(ctx.criteria, profile)
    ports = ctx.extra.get("ports") or {}
    c = Client(device.ip, getattr(device, "port", 80))

    for row in rows:
        name = f"{device.ip}/telemetry/{row}"

        if not gate_enabled(ctx, row):
            rs.add(Result(name=name, device=device, status=STATUS_SKIP, detail="row gated out"))
            continue
        if not receiver:
            rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                          detail="no receiver configured (set BB_TEST_RECEIVER or --receiver)"))
            continue

        needed = _ROW_CERTS[row]
        certs: dict = {}
        if needed:
            if not certs_dir:
                rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                              detail="row needs certs but no BB_TEST_CERTS/--certs set"))
                continue
            certs, err = _load_certs(certs_dir, needed)
            if err:
                rs.add(Result(name=name, device=device, status=STATUS_SKIP, detail=err))
                continue

        port = ports.get(row, _DEFAULT_PORTS[row])
        disable_patch, enable_patch = build_config(row, receiver, certs, port)

        # MUTATING: configure transport via guard.
        try:
            gres = ctx.guard.check(device, "PATCH", "/api/telemetry")
        except Exception as exc:  # noqa: BLE001
            rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                          detail=f"refused: {type(exc).__name__}: {exc}"))
            continue
        if ctx.guard.is_dry_run_skip(gres):
            rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                          detail=f"dry-run: PATCH /api/telemetry skipped for {row}"))
            continue

        st1, _ = c.request("PATCH", "/api/telemetry", body=disable_patch, timeout=TIMEOUT_WRITE)
        st2, _ = c.request("PATCH", "/api/telemetry", body=enable_patch, timeout=TIMEOUT_WRITE)
        if st1 is None or st2 is None or st1 >= 400 or st2 >= 400:
            rs.add(Result(name=name, device=device, status=STATUS_FAIL,
                          detail=f"PATCH failed (disable={st1}, enable={st2})"))
            continue

        ctx.settle.wait_ready(device, criteria)
        telem = c.get_json("/api/telemetry", timeout=TIMEOUT_INFO)
        ok, detail = evaluate_row(row, telem)

        metrics: dict = {}
        if telem:
            pub = telem.get("publisher") or {}
            metrics["last_publish_age_ms"] = pub.get("last_publish_age_ms")
            metrics["sink_count"] = pub.get("sink_count")

        if not ok:
            rs.add(Result(name=name, device=device, status=STATUS_FAIL,
                          detail=detail, metrics=metrics))
            continue

        note = _influx_validate(ctx, row, telem)
        if note:
            detail = f"{detail}; {note}"

        rs.add(Result(name=name, device=device, status=STATUS_PASS,
                      detail=detail, metrics=metrics))
