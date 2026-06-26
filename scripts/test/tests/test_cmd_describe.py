"""Tests for fleet.py cmd_describe — path listing, schema rendering, --json raw output."""
import io
import os
import sys
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.spec import Spec

# ---------------------------------------------------------------------------
# Shared fixtures
# ---------------------------------------------------------------------------

SAMPLE_SPEC = {
    "openapi": "3.1.0",
    "info": {"title": "TaipanMiner", "version": "1.0.0"},
    "paths": {
        "/api/info": {
            "get": {
                "responses": {
                    "200": {
                        "content": {
                            "application/json": {
                                "schema": {
                                    "type": "object",
                                    "properties": {
                                        "hostname": {"type": "string"},
                                        "version": {"type": "string"},
                                        "uptime_ms": {"type": "integer"},
                                    },
                                    "required": ["hostname", "version"],
                                }
                            }
                        }
                    }
                }
            }
        },
        "/api/settings": {
            "patch": {
                "requestBody": {
                    "content": {
                        "application/json": {
                            "schema": {
                                "type": "object",
                                "properties": {
                                    "led_heartbeat_en": {"type": "boolean"},
                                    "display_en": {"type": "boolean"},
                                },
                            }
                        }
                    }
                },
                "responses": {
                    "200": {
                        "content": {
                            "application/json": {
                                "schema": {"type": "object", "properties": {"ok": {"type": "boolean"}}}
                            }
                        }
                    }
                },
            }
        },
    },
}


def _make_device(ip="192.0.2.1"):
    from fleetlib.discovery import Device
    return Device(hostname="test-taipan", ip=ip, port=80, board="test-board", version="v1.0.0")


def _args(path=None, method=None, json_raw=False, hosts=None, board=None):
    """Build a minimal args namespace for cmd_describe."""
    ns = MagicMock()
    ns.path = path
    ns.method = method
    ns.json_raw = json_raw
    ns.hosts = hosts
    ns.board = board
    ns.discover_timeout = 10
    return ns


# ---------------------------------------------------------------------------
# Helpers tested in isolation
# ---------------------------------------------------------------------------

class TestRenderSchema(unittest.TestCase):
    def setUp(self):
        import fleet
        self.fleet = fleet

    def _capture(self, schema, indent=0):
        buf = io.StringIO()
        with patch("builtins.print", lambda *a, **kw: buf.write(" ".join(str(x) for x in a) + "\n")):
            self.fleet._render_schema(schema, indent=indent)
        return buf.getvalue()

    def test_no_properties_shows_type(self):
        out = self._capture({"type": "string"})
        self.assertIn("type: string", out)

    def test_properties_header_present(self):
        schema = {
            "type": "object",
            "properties": {"hostname": {"type": "string"}},
        }
        out = self._capture(schema)
        self.assertIn("FIELD", out)
        self.assertIn("TYPE", out)

    def test_required_marked(self):
        schema = {
            "type": "object",
            "properties": {"hostname": {"type": "string"}, "version": {"type": "string"}},
            "required": ["hostname"],
        }
        out = self._capture(schema)
        lines = [l for l in out.splitlines() if "hostname" in l]
        self.assertTrue(any("yes" in l for l in lines), f"expected 'yes' in hostname row: {out}")

    def test_optional_not_marked(self):
        schema = {
            "type": "object",
            "properties": {"uptime_ms": {"type": "integer"}},
        }
        out = self._capture(schema)
        lines = [l for l in out.splitlines() if "uptime_ms" in l]
        self.assertTrue(lines)
        self.assertNotIn("yes", lines[0])

    def test_nested_object_recurses(self):
        schema = {
            "type": "object",
            "properties": {
                "heap": {
                    "type": "object",
                    "properties": {"free": {"type": "integer"}},
                }
            },
        }
        out = self._capture(schema)
        self.assertIn("heap", out)
        self.assertIn("free", out)

    def test_enum_in_notes(self):
        schema = {
            "type": "object",
            "properties": {
                "level": {"type": "string", "enum": ["low", "med", "high"]},
            },
        }
        out = self._capture(schema)
        self.assertIn("enum", out)

    def test_anyof_nullable_type(self):
        schema = {
            "type": "object",
            "properties": {
                "val": {"anyOf": [{"type": "integer"}, {"type": "null"}]},
            },
        }
        out = self._capture(schema)
        self.assertIn("integer", out)


class TestSchemaTypeStr(unittest.TestCase):
    def setUp(self):
        import fleet
        self.fleet = fleet

    def test_plain_type(self):
        self.assertEqual(self.fleet._schema_type_str({"type": "string"}), "string")

    def test_anyof_non_null(self):
        result = self.fleet._schema_type_str({"anyOf": [{"type": "integer"}, {"type": "null"}]})
        self.assertIn("integer", result)
        self.assertIn("?", result)

    def test_no_type_returns_any(self):
        self.assertEqual(self.fleet._schema_type_str({}), "any")


# ---------------------------------------------------------------------------
# cmd_describe integration tests (mocked client + resolve_devices)
# ---------------------------------------------------------------------------

class TestCmdDescribeListPaths(unittest.TestCase):
    def setUp(self):
        import fleet
        self.fleet = fleet

    def _run(self, args):
        buf = io.StringIO()
        with patch("builtins.print", lambda *a, **kw: buf.write(" ".join(str(x) for x in a) + "\n")):
            code = self.fleet.cmd_describe(args)
        return code, buf.getvalue()

    def test_list_all_paths(self):
        dev = _make_device()
        args = _args()  # no path
        with patch("fleet.resolve_devices", return_value=[dev]):
            with patch("fleetlib.client.Client.spec", new_callable=lambda: property(lambda self: SAMPLE_SPEC)):
                code, out = self._run(args)
        self.assertEqual(code, 0)
        self.assertIn("/api/info", out)
        self.assertIn("/api/settings", out)
        self.assertIn("GET", out)
        self.assertIn("PATCH", out)

    def test_no_devices_returns_1(self):
        args = _args()
        with patch("fleet.resolve_devices", return_value=[]):
            code, out = self._run(args)
        self.assertEqual(code, 1)

    def test_unreachable_spec_returns_1(self):
        dev = _make_device()
        args = _args()
        with patch("fleet.resolve_devices", return_value=[dev]):
            with patch("fleetlib.client.Client.spec", new_callable=lambda: property(lambda self: None)):
                code, out = self._run(args)
        self.assertEqual(code, 1)


class TestCmdDescribePathOnly(unittest.TestCase):
    def setUp(self):
        import fleet
        self.fleet = fleet

    def _run(self, args):
        buf = io.StringIO()
        with patch("builtins.print", lambda *a, **kw: buf.write(" ".join(str(x) for x in a) + "\n")):
            code = self.fleet.cmd_describe(args)
        return code, buf.getvalue()

    def test_known_path_shows_schema(self):
        dev = _make_device()
        args = _args(path="/api/settings")
        with patch("fleet.resolve_devices", return_value=[dev]):
            with patch("fleetlib.client.Client.spec", new_callable=lambda: property(lambda self: SAMPLE_SPEC)):
                code, out = self._run(args)
        self.assertEqual(code, 0)
        self.assertIn("PATCH", out)
        self.assertIn("led_heartbeat_en", out)

    def test_unknown_path_returns_1(self):
        dev = _make_device()
        args = _args(path="/api/nonexistent")
        with patch("fleet.resolve_devices", return_value=[dev]):
            with patch("fleetlib.client.Client.spec", new_callable=lambda: property(lambda self: SAMPLE_SPEC)):
                code, out = self._run(args)
        self.assertEqual(code, 1)
        self.assertIn("not in the spec", out)
        self.assertIn("./fleet describe", out)

    def test_path_and_method(self):
        dev = _make_device()
        args = _args(path="/api/settings", method="PATCH")
        with patch("fleet.resolve_devices", return_value=[dev]):
            with patch("fleetlib.client.Client.spec", new_callable=lambda: property(lambda self: SAMPLE_SPEC)):
                code, out = self._run(args)
        self.assertEqual(code, 0)
        self.assertIn("PATCH /api/settings", out)


class TestCmdDescribeRawJson(unittest.TestCase):
    def setUp(self):
        import fleet
        self.fleet = fleet

    def _run(self, args):
        buf = io.StringIO()
        with patch("builtins.print", lambda *a, **kw: buf.write(" ".join(str(x) for x in a) + "\n")):
            code = self.fleet.cmd_describe(args)
        return code, buf.getvalue()

    def test_raw_json_contains_json(self):
        import json
        dev = _make_device()
        args = _args(path="/api/settings", method="PATCH", json_raw=True)
        with patch("fleet.resolve_devices", return_value=[dev]):
            with patch("fleetlib.client.Client.spec", new_callable=lambda: property(lambda self: SAMPLE_SPEC)):
                code, out = self._run(args)
        self.assertEqual(code, 0)
        # output should contain raw JSON — parse it to confirm
        # find the JSON block
        self.assertIn('"led_heartbeat_en"', out)
        self.assertIn('"type"', out)


if __name__ == "__main__":
    unittest.main()
