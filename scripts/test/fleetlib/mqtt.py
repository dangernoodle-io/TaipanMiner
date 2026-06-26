"""Shared paho-mqtt helpers — connect, publish, subscribe.

paho-mqtt is a soft dependency (lazy import).  If not installed, functions return
(False, "paho-mqtt not installed …") or skip with a note so callers can degrade
gracefully without raising ImportError.
"""
from __future__ import annotations
import logging
import os
import ssl
import tempfile
from typing import Optional, Tuple

logger = logging.getLogger(__name__)

_PAHO_MISSING_MSG = (
    "paho-mqtt not installed (pip install paho-mqtt); "
    "MQTT operation skipped"
)


def _import_paho():
    """Lazy import paho.mqtt.client.  Returns (module, None) or (None, reason)."""
    try:
        import paho.mqtt.client as mqtt
        return mqtt, None
    except ImportError:
        return None, _PAHO_MISSING_MSG


def build_tls_context(
    ca: Optional[str] = None,
    cert: Optional[str] = None,
    key: Optional[str] = None,
    tmpdir: Optional[str] = None,
) -> Tuple[Optional[ssl.SSLContext], Optional[str]]:
    """Build an ssl.SSLContext from PEM strings.

    All arguments are optional; an empty call returns (None, None) meaning
    "no TLS".  cert and key must both be provided or both omitted.

    tmpdir: an existing temporary directory to write cert files into.
    If None, one is created and the caller must clean it up.
    Returns (ctx, error_str).
    """
    if not ca and not cert and not key:
        return None, None

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

    def _write(td: str) -> Optional[str]:
        nonlocal ctx
        if ca:
            ca_path = os.path.join(td, "ca.crt")
            with open(ca_path, "w") as f:
                f.write(ca)
            ctx.verify_mode = ssl.CERT_REQUIRED
            ctx.check_hostname = True
            try:
                ctx.load_verify_locations(ca_path)
            except ssl.SSLError as exc:
                return f"TLS CA load failed: {exc}"
        if cert and key:
            cert_path = os.path.join(td, "client.crt")
            key_path = os.path.join(td, "client.key")
            with open(cert_path, "w") as f:
                f.write(cert)
            with open(key_path, "w") as f:
                f.write(key)
            try:
                ctx.load_cert_chain(cert_path, key_path)
            except ssl.SSLError as exc:
                return f"TLS cert/key load failed: {exc}"
        return None

    if tmpdir is not None:
        err = _write(tmpdir)
        return (None, err) if err else (ctx, None)

    with tempfile.TemporaryDirectory() as td:
        err = _write(td)
    return (None, err) if err else (ctx, None)


def connect_and_publish(
    broker_url: str,
    topic: str,
    payload: bytes,
    qos: int = 1,
    tls_ctx: Optional[ssl.SSLContext] = None,
    client_id: str = "",
    _client_factory=None,
) -> Tuple[bool, str]:
    """Connect to *broker_url*, publish *payload* to *topic*, then disconnect.

    broker_url: "host:port" or "host" (default port 1883).
    Returns (ok, detail).  Never raises.
    """
    host, port = _parse_broker_url(broker_url)

    if _client_factory is not None:
        client = _client_factory()
    else:
        mqtt, err = _import_paho()
        if mqtt is None:
            return False, err
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)

    if tls_ctx is not None:
        client.tls_set_context(tls_ctx)

    published = []
    error_holder = []

    def on_connect(c, userdata, flags, rc, *args):
        if rc == 0:
            info = c.publish(topic, payload, qos=qos)
            published.append(info)
        else:
            error_holder.append(f"connect rc={rc}")
            c.disconnect()

    def on_publish(c, userdata, mid, *args):
        c.disconnect()

    client.on_connect = on_connect
    client.on_publish = on_publish

    try:
        client.connect(host, port, keepalive=10)
    except Exception as exc:
        return False, f"broker connect failed: {exc}"

    try:
        import time
        client.loop_start()
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not published and not error_holder:
            time.sleep(0.05)
    finally:
        client.loop_stop()
        try:
            client.disconnect()
        except Exception:
            pass

    if error_holder:
        return False, error_holder[0]
    if published:
        return True, f"published to {topic} ({len(payload)}B)"
    return False, f"publish timeout: no ack within 10s on {topic!r}"


def subscribe_and_wait(
    broker_url: str,
    topic: str,
    timeout: int = 15,
    tls_ctx: Optional[ssl.SSLContext] = None,
    _client_factory=None,
) -> Tuple[bool, str]:
    """Subscribe to *topic* and wait up to *timeout* seconds for one message.

    Returns (ok, detail).  Positive confirmation only — a received message is
    required for ok=True; timeout or connect failure → ok=False.
    """
    host, port = _parse_broker_url(broker_url)
    received: list = []

    def on_connect(client, userdata, flags, rc, *args):
        if rc == 0:
            client.subscribe(topic, qos=0)
        else:
            logger.debug("broker verify: connect rc=%d", rc)

    def on_message(client, userdata, msg):
        received.append(msg)
        client.disconnect()

    if _client_factory is not None:
        client = _client_factory()
    else:
        mqtt, err = _import_paho()
        if mqtt is None:
            return False, (
                "paho-mqtt not installed (pip install paho-mqtt); "
                "broker-subscribe check skipped — device-side signal was the only validation"
            )
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

    client.on_connect = on_connect
    client.on_message = on_message

    if tls_ctx is not None:
        client.tls_set_context(tls_ctx)

    try:
        client.connect(host, port, keepalive=30)
    except Exception as exc:
        return False, f"broker connect failed: {exc}"

    try:
        import time
        client.loop_start()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and not received:
            time.sleep(0.2)
    finally:
        client.loop_stop()
        try:
            client.disconnect()
        except Exception:
            pass

    if received:
        msg = received[0]
        return True, f"broker receipt confirmed: topic={msg.topic} len={len(msg.payload)}B"
    return False, f"broker timeout: no message received on {topic!r} within {timeout}s"


def _parse_broker_url(url: str) -> Tuple[str, int]:
    """Parse 'host:port' or 'host' into (host, port). Default port 1883."""
    # strip scheme if any (mqtt:// mqtts://)
    for scheme in ("mqtts://", "mqtt://"):
        if url.startswith(scheme):
            url = url[len(scheme):]
            break
    if ":" in url:
        host, port_s = url.rsplit(":", 1)
        try:
            return host, int(port_s)
        except ValueError:
            pass
    return url, 1883
