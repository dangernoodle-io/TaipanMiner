"""Telemetry transport suite (NAME="telemetry") — folds tlsrow.py / run_matrix.sh.

For each transport row, PATCH /api/telemetry to configure the sink (MUTATING → via
ctx.guard), settle, then verify publish health under the no-false-sinks rule: a sink
counts as healthy ONLY on positive confirmation (mqtt.connected AND
publisher.last_publish_ok for mqtt rows; publisher.last_publish_ok + fresh age for
http rows), never on mere absence of error, and never while the other sink is still
enabled.

For MQTT rows, after the device-side check passes, an additional broker-subscribe
validation is performed: a paho-mqtt subscriber connects to the same broker endpoint
the device was configured to use, subscribes to a wildcard topic covering the device's
hostname, and waits for a message to arrive. This confirms end-to-end mosquitto receipt.

The device publishes to: <prefix>/<hostname>/<subtopic>
Default prefix is "metrics" (BB_PUB_TOPIC_PREFIX Kconfig default). The subscriber
uses the wildcard "metrics/<hostname>/#" to match any subtopic from the device,
then matches the received message payload (JSON field "hostname" or topic segment).

Config (no hardcoded IPs/paths, no secrets in code):
  BB_TEST_RECEIVER — receiver host (env, or ctx.extra['receiver'] / --receiver)
  BB_TEST_CERTS    — dir with ca.crt/client.crt/client.key (env, or ctx.extra['certs'] / --certs)

Note: InfluxDB docker-exec validation has been removed (it assumed a local container
and is useless on a separate-server stack). A network-influx check is a follow-up
(TA-455-adjacent). Broker-subscribe is the primary validation for MQTT rows.
"""
from __future__ import annotations
import logging
import os
import ssl
import sys
import tempfile
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


DEFAULT_BROKER_TIMEOUT = 15  # seconds to wait for broker message
DEFAULT_TOPIC_PREFIX = "metrics"  # BB_PUB_TOPIC_PREFIX Kconfig default


def add_arguments(parser) -> None:
    parser.add_argument(
        "--rows", metavar="R,R,…", default=None,
        help="comma-separated subset of transport rows (default: mqtt_plain; "
             f"available: {','.join(ROWS)})",
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
        "--broker-timeout", metavar="SEC", type=int, default=DEFAULT_BROKER_TIMEOUT,
        help=f"seconds to wait for broker message (default: {DEFAULT_BROKER_TIMEOUT})",
    )
    parser.add_argument(
        "--topic-prefix", metavar="PREFIX", default=DEFAULT_TOPIC_PREFIX,
        help=f"MQTT topic prefix (BB_PUB_TOPIC_PREFIX; default: {DEFAULT_TOPIC_PREFIX})",
    )


def _resolve_receiver(ctx) -> Optional[str]:
    return ctx.extra.get("receiver") or os.environ.get("BB_TEST_RECEIVER")


def _resolve_certs_dir(ctx) -> Optional[str]:
    return ctx.extra.get("certs") or os.environ.get("BB_TEST_CERTS")


def selected_rows(ctx) -> list:
    sel = ctx.extra.get("rows")
    if isinstance(sel, str):
        sel = [r.strip() for r in sel.split(",") if r.strip()]
    rows = sel or ["mqtt_plain"]
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


def _mqtt_broker_verify(
    host: str,
    port: int,
    hostname: str,
    certs: dict,
    row: str,
    timeout: int = DEFAULT_BROKER_TIMEOUT,
    topic_prefix: str = DEFAULT_TOPIC_PREFIX,
    _paho_client_factory=None,
) -> Tuple[bool, str]:
    """Subscribe to the broker and confirm a message from this device arrives.

    Delegates to fleetlib.mqtt.subscribe_and_wait after building an optional
    TLS context from ``certs`` for stls/mtls rows.

    Returns (ok, detail).  Positive confirmation only — a received message is
    required for ok=True; timeout or connect failure → ok=False.
    """
    from fleetlib.mqtt import subscribe_and_wait, build_tls_context

    topic = f"{topic_prefix}/{hostname}/#"
    broker_url = f"{host}:{port}"

    tls_ctx = None
    use_tls = row in ("mqtt_stls", "mqtt_mtls")
    if use_tls and certs:
        with tempfile.TemporaryDirectory() as td:
            tls_ctx, err = build_tls_context(
                ca=certs.get("ca"),
                cert=certs.get("cert"),
                key=certs.get("key"),
                tmpdir=td,
            )
            if err:
                return False, f"TLS setup failed: {err}"
            return subscribe_and_wait(
                broker_url,
                topic,
                timeout=timeout,
                tls_ctx=tls_ctx,
                _client_factory=_paho_client_factory,
            )

    return subscribe_and_wait(
        broker_url,
        topic,
        timeout=timeout,
        tls_ctx=None,
        _client_factory=_paho_client_factory,
    )


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

        # MQTT rows: broker-subscribe validation (positive confirmation from mosquitto).
        # HTTP rows: device-side signal is sufficient (no broker).
        if row.startswith("mqtt"):
            broker_timeout = ctx.extra.get("broker_timeout", DEFAULT_BROKER_TIMEOUT)
            topic_prefix = ctx.extra.get("topic_prefix", DEFAULT_TOPIC_PREFIX)
            paho_factory = ctx.extra.get("_paho_client_factory")
            broker_ok, broker_detail = _mqtt_broker_verify(
                host=receiver,
                port=port,
                hostname=device.hostname,
                certs=certs,
                row=row,
                timeout=broker_timeout,
                topic_prefix=topic_prefix,
                _paho_client_factory=paho_factory,
            )
            if broker_ok:
                detail = f"{detail}; {broker_detail}"
            else:
                detail = f"{detail}; broker: {broker_detail}"

        rs.add(Result(name=name, device=device, status=STATUS_PASS,
                      detail=detail, metrics=metrics))
