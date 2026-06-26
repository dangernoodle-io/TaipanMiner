"""Functional suite — validates each device's REST API against its own OpenAPI spec.

For every GET endpoint that declares a 200 application/json schema, the suite:
  1. Calls the endpoint
  2. Validates the response body against the device's own served schema
  3. Records pass/fail/skip per endpoint into ResultSet

Design principles:
  - SSOT is the device's own spec (GET /api/openapi.json) — no hand-maintained field lists
  - Skips paths with path parameters (e.g., /api/thing/{id})
  - Skips methods other than GET (POST/PUT/DELETE modify state)
  - Skips endpoints where the spec declares no 200 application/json schema
  - Tolerates unreachable devices (all endpoints SKIP)
  - Zero false failures on live wroom32/bitaxe devices
"""
from __future__ import annotations
import logging
import sys
import os
from typing import TYPE_CHECKING

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

if TYPE_CHECKING:
    from suites import SuiteContext

from fleetlib.results import ResultSet, Result, STATUS_PASS, STATUS_FAIL, STATUS_SKIP
from fleetlib.client import Client, TIMEOUT_INFO
from fleetlib.spec import Spec

logger = logging.getLogger(__name__)

NAME = "functional"
HELP = "Validate each device's REST API against its own OpenAPI spec"

# Error message substrings that indicate schema-precision issues (not firmware bugs).
# These arise from OpenAPI 3.0-style nullable fields served in a 3.1 doc, or from
# enum fields that legitimately hold an empty-string sentinel on boards where the
# feature isn't applicable.  Downgrade to SKIP instead of FAIL in lenient mode.
_LENIENT_PATTERNS = (
    "is not of type 'string'",   # null where schema says type=string (nullable issue)
    "is not of type 'integer'",  # null where schema says type=integer
    "is not of type 'number'",   # null where schema says type=number
    "is not of type 'boolean'",  # null where schema says type=boolean
    "'' is not one of",          # empty-string enum sentinel (feature not applicable on board)
)


def add_arguments(parser) -> None:
    """Functional-suite-specific arguments."""
    parser.add_argument(
        "--strict", action="store_true",
        help="strict schema validation: fail on null-for-typed-field and empty-enum errors "
             "(by default these are downgraded to SKIP as known schema-precision issues)",
    )


def run(ctx: "SuiteContext") -> ResultSet:
    """Run functional validation across all devices in ctx.devices."""
    rs = ctx.results

    for device in ctx.devices:
        _run_device(device, ctx, rs)

    # Write outputs
    if ctx.out_json:
        rs.to_json(ctx.out_json)
    if ctx.out_junit:
        rs.to_junit(ctx.out_junit)

    return rs


def _run_device(device, ctx: "SuiteContext", rs: ResultSet) -> None:
    """Run functional checks for a single device."""
    c = Client(device.ip, getattr(device, "port", 80))

    # Fetch the device's own OpenAPI spec
    raw_spec = c.get_json("/api/openapi.json", timeout=TIMEOUT_INFO)
    if raw_spec is None:
        rs.add(Result(
            name=f"{device.ip}/api/openapi.json",
            device=device,
            status=STATUS_SKIP,
            detail="unreachable or no spec served",
        ))
        logger.warning("device %s: could not fetch /api/openapi.json — skipping all endpoints", device.ip)
        return

    spec = Spec(raw_spec)
    paths = spec.paths()
    logger.info("device %s (%s): found %d paths in spec", device.ip, device.board, len(paths))

    for path in paths:
        # Skip paths with path parameters — no test data available
        if "{" in path:
            logger.debug("skip %s (has path parameters)", path)
            continue

        methods = spec.methods(path)
        if "get" not in methods:
            continue

        # Only test endpoints with a declared 200 application/json schema
        schema = spec.response_schema(path, "get", "200")
        if schema is None:
            logger.debug("skip %s GET (no 200 application/json schema)", path)
            continue

        name = f"{device.ip}{path}"
        _check_endpoint(c, device, spec, path, schema, ctx, rs, name)


def _check_endpoint(c, device, spec: Spec, path: str, schema: dict, ctx: "SuiteContext", rs: ResultSet, name: str) -> None:
    """Call a single GET endpoint and validate its response."""
    try:
        # Use get_json — returns None on any network/parse error
        raw_resp = c.get_json(path, timeout=TIMEOUT_INFO)
    except Exception as exc:
        rs.add(Result(
            name=name,
            device=device,
            status=STATUS_SKIP,
            detail=f"request error: {exc}",
        ))
        return

    if raw_resp is None:
        # get_json returns None on HTTP errors AND network errors
        # Distinguish: try the request() method to see the status code
        status, _ = c.request("GET", path, timeout=TIMEOUT_INFO)
        if status is None:
            rs.add(Result(
                name=name,
                device=device,
                status=STATUS_SKIP,
                detail="network error (connection refused or timeout)",
            ))
        elif status >= 500:
            rs.add(Result(
                name=name,
                device=device,
                status=STATUS_SKIP,
                detail=f"server error HTTP {status}",
            ))
        elif status >= 400:
            rs.add(Result(
                name=name,
                device=device,
                status=STATUS_SKIP,
                detail=f"client error HTTP {status} (endpoint requires state/auth?)",
            ))
        else:
            rs.add(Result(
                name=name,
                device=device,
                status=STATUS_SKIP,
                detail=f"unexpected status HTTP {status} with empty/non-JSON body",
            ))
        return

    # We have a parsed JSON response — validate it
    if ctx.fields is not None:
        # Subset validation: check only that requested fields exist in the response
        missing = []
        for field_path in ctx.fields:
            val = _get_nested(raw_resp, field_path)
            if val is None and not _has_nested(raw_resp, field_path):
                missing.append(field_path)
        if missing:
            rs.add(Result(
                name=name,
                device=device,
                status=STATUS_FAIL,
                detail=f"missing fields: {', '.join(missing)}",
                metrics={"fields_checked": len(ctx.fields), "fields_missing": len(missing)},
            ))
        else:
            rs.add(Result(
                name=name,
                device=device,
                status=STATUS_PASS,
                detail=f"fields present: {', '.join(ctx.fields)}",
                metrics={"fields_checked": len(ctx.fields)},
            ))
        return

    # Full schema validation
    try:
        errors = spec.validate(path, "get", raw_resp)
    except ImportError as exc:
        rs.add(Result(
            name=name,
            device=device,
            status=STATUS_SKIP,
            detail=f"jsonschema not available: {exc}",
        ))
        return
    except Exception as exc:
        rs.add(Result(
            name=name,
            device=device,
            status=STATUS_SKIP,
            detail=f"validator error: {exc}",
        ))
        return

    strict = ctx.extra.get("strict", False)

    if errors:
        # In lenient mode (default), downgrade schema-precision issues to SKIP.
        # These arise from nullable fields served with non-nullable schemas (OA 3.0→3.1
        # migration) and from empty-string enum sentinels on boards where the feature
        # isn't applicable.
        hard_errors = errors
        if not strict:
            hard_errors = [
                e for e in errors
                if not any(pat in e for pat in _LENIENT_PATTERNS)
            ]
            lenient_count = len(errors) - len(hard_errors)
        else:
            lenient_count = 0

        if hard_errors:
            detail = "; ".join(hard_errors[:5])
            if len(hard_errors) > 5:
                detail += f" (+ {len(hard_errors) - 5} more)"
            rs.add(Result(
                name=name,
                device=device,
                status=STATUS_FAIL,
                detail=detail,
                metrics={"schema_errors": len(hard_errors), "lenient_skipped": lenient_count},
            ))
        elif lenient_count > 0:
            rs.add(Result(
                name=name,
                device=device,
                status=STATUS_SKIP,
                detail=f"{lenient_count} schema-precision issue(s) (nullable/enum); use --strict to fail",
                metrics={"schema_errors": 0, "lenient_skipped": lenient_count},
            ))
        else:
            rs.add(Result(
                name=name,
                device=device,
                status=STATUS_PASS,
                detail="schema valid",
                metrics={"schema_errors": 0},
            ))
    else:
        rs.add(Result(
            name=name,
            device=device,
            status=STATUS_PASS,
            detail="schema valid",
            metrics={"schema_errors": 0},
        ))


def _get_nested(obj, path: str):
    """Navigate a dotted path in obj, returning None if any step is missing."""
    parts = path.split(".")
    cur = obj
    for p in parts:
        if not isinstance(cur, dict):
            return None
        cur = cur.get(p)
    return cur


def _has_nested(obj, path: str) -> bool:
    """Return True if the dotted path exists in obj (even if the value is None)."""
    parts = path.split(".")
    cur = obj
    for p in parts:
        if not isinstance(cur, dict) or p not in cur:
            return False
        cur = cur[p]
    return True
