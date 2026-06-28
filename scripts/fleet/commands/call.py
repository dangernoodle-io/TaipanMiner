"""call command — make an arbitrary API request (safety-gated for mutating methods)."""
from __future__ import annotations

import sys

from core import resolve_devices

NAME = "call"
HELP = "make an arbitrary API request (safety-gated for mutating methods)"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)
    parser.add_argument("call_method", metavar="METHOD",
                        help="HTTP method: GET, POST, PUT, PATCH, DELETE")
    parser.add_argument("call_path", metavar="PATH",
                        help="endpoint path (e.g. /api/settings)")
    parser.add_argument("--json", dest="json_body", metavar="JSON",
                        help="request body as inline JSON string")
    parser.add_argument("--json-file", dest="json_file", metavar="FILE",
                        help="request body from JSON file")
    parser.add_argument("--no-validate", dest="no_validate", action="store_true",
                        help="skip request body schema validation against the served spec")


def run(args) -> int:
    """Make an arbitrary API request (safety-gated for mutating methods)."""
    import json as _json
    from fleetlib.client import Client, get_field, TIMEOUT_WRITE
    from fleetlib.spec import Spec
    from fleetlib.safety import Guard, MUTATING, DeviceUnreachable, IdentityMismatch, RefusedWithoutConfirmation
    from core import unwrap_devices, no_devices_message

    method = args.call_method.upper()
    path = args.call_path

    # Parse body early — return before any network I/O on error
    body = None
    json_body = getattr(args, "json_body", None)
    json_file = getattr(args, "json_file", None)
    if json_body:
        try:
            body = _json.loads(json_body)
        except _json.JSONDecodeError as e:
            print(f"ERROR: --json body is not valid JSON: {e}")
            return 1
    elif json_file:
        try:
            with open(json_file) as fh:
                body = _json.load(fh)
        except FileNotFoundError:
            print(f"ERROR: --json-file not found: {json_file}")
            return 1
        except _json.JSONDecodeError as e:
            print(f"ERROR: --json-file is not valid JSON: {e}")
            return 1

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    # Fetch spec from first device for path-warning and body validation
    d0 = devices[0]
    c0 = Client(d0.ip, getattr(d0, "port", 80))
    spec_doc = c0.spec
    spec = Spec(spec_doc) if spec_doc else None

    # Warn if path unknown to served spec (non-fatal)
    if spec is not None and not spec.has_path(path):
        print(f"WARNING: {path!r} not found in served spec. Proceeding anyway.")

    # Pre-validate request body against served schema (unless --no-validate)
    if body is not None and not getattr(args, "no_validate", False) and spec is not None:
        req_schema = spec.request_schema(path, method)
        if req_schema is not None:
            try:
                import jsonschema
                validator = jsonschema.Draft202012Validator(req_schema)
                errors = list(validator.iter_errors(body))
                if errors:
                    print("ERROR: request body fails schema validation:")
                    for err in errors:
                        loc = ".".join(str(p) for p in err.absolute_path) or "<root>"
                        print(f"  {loc}: {err.message}")
                    print(f"  run ./fleet describe {path} {method} to see the expected shape")
                    return 1
            except ImportError:
                pass  # jsonschema not available; skip validation

    is_mutating = method in MUTATING
    guard = Guard(
        dry_run=getattr(args, "dry_run", False),
        confirm=getattr(args, "yes", False),
    )

    fields_raw = getattr(args, "fields", None)
    fields = [f.strip() for f in fields_raw.split(",") if f.strip()] if fields_raw else None
    out_json_path = getattr(args, "out_json", None)

    results = []
    all_ok = True

    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))

        if is_mutating:
            try:
                sentinel = guard.check(d, method, path)
            except DeviceUnreachable as e:
                print(f"ERROR: {e}")
                all_ok = False
                continue
            except IdentityMismatch as e:
                print(f"ERROR: {e}")
                all_ok = False
                continue
            except RefusedWithoutConfirmation as e:
                print(f"ERROR: {e}")
                all_ok = False
                continue

            if Guard.is_dry_run_skip(sentinel):
                print(f"DRY-RUN: would {method} {path} on {d.ip}")
                if body is not None:
                    print(f"  body: {_json.dumps(body, indent=2)}")
                results.append({"host": d.ip, "status": "dry-run", "body": body})
                continue

            status, resp_bytes = c.request(method, path, body=body)
            if status is None:
                msg = resp_bytes.decode(errors="replace")
                print(f"  {d.ip}: ERROR network error: {msg}")
                all_ok = False
                results.append({"host": d.ip, "status": None, "error": msg})
                continue

            print(f"  {d.ip}: HTTP {status}")
            try:
                resp_data = _json.loads(resp_bytes)
                print(_json.dumps(resp_data, indent=2))
                results.append({"host": d.ip, "status": status, "response": resp_data})
            except _json.JSONDecodeError:
                raw = resp_bytes.decode(errors="replace")
                print(raw)
                results.append({"host": d.ip, "status": status, "response": raw})

        else:
            # GET / HEAD / OPTIONS — no guard required
            resp_data = c.get_json(path)
            if resp_data is None:
                print(f"  {d.ip}: ERROR could not GET {path}")
                all_ok = False
                results.append({"host": d.ip, "status": None,
                                 "error": f"could not GET {path}"})
                continue

            print(f"  {d.ip}: HTTP 200")
            if fields:
                for field in fields:
                    val = get_field(resp_data, field)
                    print(f"  {field}: {val}")
            else:
                print(_json.dumps(resp_data, indent=2))
            results.append({"host": d.ip, "status": 200, "response": resp_data})

    if out_json_path:
        with open(out_json_path, "w") as fh:
            _json.dump(results, fh, indent=2)

    return 0 if all_ok else 1
