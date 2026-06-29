"""describe command — inspect the served OpenAPI spec."""
from __future__ import annotations

import sys

from core import resolve_devices, unwrap_devices, no_devices_message

NAME = "describe"
HELP = "inspect the served OpenAPI spec"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)
    parser.add_argument("path", nargs="?", metavar="PATH",
                        help="endpoint path to inspect (e.g. /api/settings)")
    parser.add_argument("method", nargs="?", metavar="METHOD",
                        help="HTTP method to inspect (e.g. PATCH)")
    parser.add_argument("--json", dest="json_raw", action="store_true",
                        help="dump raw schema JSON instead of pretty table")


def _schema_type_str(schema: dict) -> str:
    """Return a short type string for a JSON Schema sub-schema."""
    if "type" in schema:
        return schema["type"]
    if "anyOf" in schema:
        parts = [s.get("type", "any") for s in schema["anyOf"]]
        non_null = [t for t in parts if t != "null"]
        nullable = len(parts) != len(non_null)
        base = " | ".join(non_null) if non_null else "any"
        return f"{base}?" if nullable else base
    return "any"


def _schema_notes(schema: dict) -> str:
    """Return a short notes string for a JSON Schema sub-schema (enum, min/max, desc)."""
    notes = []
    if "enum" in schema:
        notes.append(f"enum: {schema['enum']}")
    if "minimum" in schema:
        notes.append(f"min: {schema['minimum']}")
    if "maximum" in schema:
        notes.append(f"max: {schema['maximum']}")
    if "description" in schema:
        desc = schema["description"]
        if len(desc) > 60:
            desc = desc[:57] + "…"
        notes.append(desc)
    return ", ".join(notes)


def _render_schema(schema: dict, indent: int = 0) -> None:
    """Render a JSON Schema object as a human-readable field table (recursive)."""
    if schema is None:
        return
    props = schema.get("properties", {})
    required_set = set(schema.get("required", []))

    pad = " " * indent

    if not props:
        schema_type = schema.get("type", "any")
        desc = schema.get("description", "")
        if desc:
            print(f"{pad}(type: {schema_type}) — {desc}")
        else:
            print(f"{pad}(type: {schema_type})")
        return

    print(f"{pad}{'FIELD':<32} {'TYPE':<14} {'REQ':<4} NOTES")
    print(f"{pad}{'-' * 70}")

    for field, fschema in props.items():
        ftype = _schema_type_str(fschema)
        req_mark = "yes" if field in required_set else ""
        notes = _schema_notes(fschema)
        print(f"{pad}{field:<32} {ftype:<14} {req_mark:<4} {notes}")
        if fschema.get("type") == "object" and fschema.get("properties"):
            _render_schema(fschema, indent=indent + 2)


def run(args) -> int:
    """Describe the OpenAPI spec served by a device."""
    import json as _json
    from fleetlib.client import Client
    from fleetlib.spec import Spec

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    d = devices[0]
    c = Client(d.ip, getattr(d, "port", 80))
    spec_doc = c.spec
    if spec_doc is None:
        print(f"ERROR: could not fetch OpenAPI spec from {d.ip}")
        return 1

    spec = Spec(spec_doc)

    # Warn if multiple devices have differing path sets
    if len(devices) > 1:
        first_paths = set(spec.paths())
        for other_d in devices[1:]:
            oc = Client(other_d.ip, getattr(other_d, "port", 80))
            other_doc = oc.spec
            if other_doc is None:
                continue
            other_paths = set(Spec(other_doc).paths())
            if other_paths != first_paths:
                extra = other_paths - first_paths
                missing = first_paths - other_paths
                note = []
                if extra:
                    note.append(f"+{sorted(extra)}")
                if missing:
                    note.append(f"-{sorted(missing)}")
                print(f"NOTE: {other_d.ip} has a different path set than {d.ip}: {'; '.join(note)}")

    path = getattr(args, "path", None)
    method = getattr(args, "method", None)
    raw_json = getattr(args, "json_raw", False)

    if path is None:
        # Table of all paths and their methods
        paths = sorted(spec.paths())
        if not paths:
            print(f"Spec from {d.ip} has no paths.")
            return 0
        print(f"\nOpenAPI spec from {d.ip}:\n")
        print(f"  {'PATH':<40} METHODS")
        print(f"  {'-' * 60}")
        for p in paths:
            methods_str = ", ".join(m.upper() for m in spec.methods(p))
            print(f"  {p:<40} {methods_str}")
        print(f"\n  {len(paths)} endpoint(s)")
        return 0

    if not spec.has_path(path):
        print(f"ERROR: {path!r} is not in the spec served by {d.ip}.")
        print("Run ./fleet describe to list available endpoints.")
        return 1

    methods_to_show = [method.lower()] if method else spec.methods(path)
    if not methods_to_show:
        print(f"No methods found for {path} in spec from {d.ip}.")
        return 0

    for m in methods_to_show:
        print(f"\n{m.upper()} {path}  [{d.ip}]")

        req_schema = spec.request_schema(path, m)
        if req_schema is not None:
            req_hdr = "Request body (required):" if spec.request_required(path, m) else "Request body:"
            print(f"\n  {req_hdr}")
            if raw_json:
                print(_json.dumps(req_schema, indent=4))
            else:
                _render_schema(req_schema, indent=4)
        else:
            print("  Request body: (none)")

        resp_schema = spec.response_schema(path, m)
        if resp_schema is not None:
            print("\n  200 response:")
            if raw_json:
                print(_json.dumps(resp_schema, indent=4))
            else:
                _render_schema(resp_schema, indent=4)
        else:
            print("  200 response: (no schema)")

    return 0
