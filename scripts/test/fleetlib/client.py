"""HTTP client for TaipanMiner fleet harness."""
from __future__ import annotations
import json
import urllib.request
import urllib.error
from typing import Any, Optional, Tuple

# Timeout constants (seconds)
TIMEOUT_INFO = 5
TIMEOUT_HEALTH = 5
TIMEOUT_TELEMETRY = 8
TIMEOUT_WRITE = 10
TIMEOUT_OTA_PUSH = 180
TIMEOUT_UPDATE_CHECK = 20

DEFAULT_TIMEOUT = TIMEOUT_INFO


def get_field(obj: Any, path: str, default: Any = None) -> Any:
    """Navigate a dotted path (e.g. 'heap.internal.free') in a JSON object."""
    parts = path.split(".")
    cur = obj
    for p in parts:
        if not isinstance(cur, dict):
            return default
        cur = cur.get(p)
        if cur is None:
            return default
    return cur


def info_field(info: dict, key: str, default: Any = None) -> Any:
    """Read a B1-360 moved field from /api/info via info.build.*.

    B1-360 moved version, idf_version, build_date, build_time, project_name,
    chip_model, chip_revision, cores, cpu_freq_mhz, flash_size, app_size,
    board, and app_sha256 under info["build"].  This accessor reads from
    info["build"][key] only (no top-level fallback).  Returns *default* when
    info["build"] is absent or the key is missing.

    Dynamic fields that remained top-level (mac, heap_internal, uptime_ms,
    network, ota_validated, reset_reason, …) must NOT be routed through here.
    """
    build = info.get("build") if isinstance(info, dict) else None
    if not isinstance(build, dict):
        return default
    v = build.get(key)
    return v if v is not None else default


class Client:
    """urllib-based HTTP client for a single TaipanMiner device."""

    def __init__(self, ip: str, port: int = 80):
        self.ip = ip
        self.port = port
        self._base = f"http://{ip}:{port}" if port != 80 else f"http://{ip}"
        self._spec_cache: Optional[dict] = None

    def get_json(self, path: str, timeout: float = DEFAULT_TIMEOUT) -> Optional[dict]:
        """GET path, parse and return JSON. Returns None on any error."""
        try:
            with urllib.request.urlopen(f"{self._base}{path}", timeout=timeout) as r:
                return json.loads(r.read())
        except Exception:
            return None

    def request(
        self,
        method: str,
        path: str,
        body: Any = None,
        timeout: float = TIMEOUT_WRITE,
    ) -> Tuple[Optional[int], bytes]:
        """Execute method on path.

        body may be JSON-serializable (sent as application/json) or raw bytes
        (sent as application/octet-stream).

        Returns (status_code, response_bytes). status_code is None on network error.
        """
        data: Optional[bytes] = None
        headers: dict = {}

        if body is not None:
            if isinstance(body, (bytes, bytearray)):
                data = bytes(body)
                headers["Content-Type"] = "application/octet-stream"
            else:
                data = json.dumps(body).encode()
                headers["Content-Type"] = "application/json"

        req = urllib.request.Request(
            f"{self._base}{path}", data=data, method=method.upper()
        )
        for k, v in headers.items():
            req.add_header(k, v)

        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()
        except Exception as e:
            return None, str(e).encode()

    @property
    def spec(self) -> Optional[dict]:
        """Lazily fetch and cache GET /api/openapi.json."""
        if self._spec_cache is None:
            self._spec_cache = self.get_json("/api/openapi.json", timeout=TIMEOUT_INFO)
        return self._spec_cache

    @staticmethod
    def get_field(obj: Any, path: str, default: Any = None) -> Any:
        """Module-level get_field exposed as a static method."""
        return get_field(obj, path, default)

    @staticmethod
    def info_field(info: dict, key: str, default: Any = None) -> Any:
        """Module-level info_field exposed as a static method."""
        return info_field(info, key, default)
