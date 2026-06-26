"""Tests for fleet.py cmd_call — GET, mutating guard, body validation, --fields."""
import io
import json
import os
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch, PropertyMock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device
from fleetlib.safety import Guard


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

SAMPLE_SPEC = {
    "openapi": "3.1.0",
    "info": {"title": "TaipanMiner", "version": "1.0.0"},
    "paths": {
        "/api/diag/heap": {
            "get": {
                "responses": {
                    "200": {
                        "content": {
                            "application/json": {
                                "schema": {
                                    "type": "object",
                                    "properties": {
                                        "internal": {
                                            "type": "object",
                                            "properties": {
                                                "free": {"type": "integer"},
                                            },
                                        }
                                    },
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
                                "schema": {"type": "object"}
                            }
                        }
                    }
                },
            }
        },
    },
}


def _dev(ip="192.0.2.1"):
    return Device(hostname="test-taipan", ip=ip, port=80, board="test-board", version="v1.0.0")


def _args(method, path, json_body=None, json_file=None, fields=None, out_json=None,
          dry_run=False, yes=False, no_validate=False, hosts=None, board=None):
    ns = MagicMock()
    ns.call_method = method
    ns.call_path = path
    ns.json_body = json_body
    ns.json_file = json_file
    ns.fields = fields
    ns.out_json = out_json
    ns.dry_run = dry_run
    ns.yes = yes
    ns.no_validate = no_validate
    ns.hosts = hosts
    ns.board = board
    ns.discover_timeout = 10
    return ns


def _run_cmd_call(args, devices, spec_doc=SAMPLE_SPEC, client_responses=None):
    """Run cmd_call with mocked resolve_devices, Client.spec, and Client.get_json/request."""
    import fleet
    buf = io.StringIO()

    def mock_get_json(self_c, path, timeout=None):
        if client_responses and path in client_responses:
            return client_responses[path]
        return None

    def mock_request(self_c, method, path, body=None, timeout=None):
        if client_responses and (method, path) in client_responses:
            data = client_responses[(method, path)]
            return data[0], json.dumps(data[1]).encode()
        return 200, b'{}'

    with patch("fleet.resolve_devices", return_value=devices):
        with patch("fleetlib.client.Client.spec", new_callable=lambda: property(lambda s: spec_doc)):
            with patch("fleetlib.client.Client.get_json", mock_get_json):
                with patch("fleetlib.client.Client.request", mock_request):
                    with patch("builtins.print", lambda *a, **kw: buf.write(" ".join(str(x) for x in a) + "\n")):
                        code = fleet.cmd_call(args)

    return code, buf.getvalue()


# ---------------------------------------------------------------------------
# Tests: GET
# ---------------------------------------------------------------------------

class TestCmdCallGet(unittest.TestCase):
    def test_get_returns_0_and_prints_json(self):
        resp = {"internal": {"free": 87654, "min_free": 75000}}
        args = _args("GET", "/api/diag/heap")
        code, out = _run_cmd_call(args, [_dev()], client_responses={"/api/diag/heap": resp})
        self.assertEqual(code, 0)
        self.assertIn("87654", out)

    def test_get_no_response_returns_1(self):
        args = _args("GET", "/api/diag/heap")
        code, out = _run_cmd_call(args, [_dev()], client_responses={})
        self.assertEqual(code, 1)
        self.assertIn("ERROR", out)

    def test_no_devices_returns_1(self):
        args = _args("GET", "/api/diag/heap")
        code, out = _run_cmd_call(args, [])
        self.assertEqual(code, 1)

    def test_fields_extracts_value(self):
        resp = {"internal": {"free": 91234}}
        args = _args("GET", "/api/diag/heap", fields="internal.free")
        code, out = _run_cmd_call(args, [_dev()], client_responses={"/api/diag/heap": resp})
        self.assertEqual(code, 0)
        self.assertIn("91234", out)
        self.assertIn("internal.free", out)


# ---------------------------------------------------------------------------
# Tests: Guard enforcement for mutating methods
# ---------------------------------------------------------------------------

class TestCmdCallGuard(unittest.TestCase):
    def test_dry_run_skips_mutation(self):
        args = _args("PATCH", "/api/settings", json_body='{"led_heartbeat_en":false}',
                     dry_run=True, yes=False)
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            code, out = _run_cmd_call(args, [_dev()])
        self.assertEqual(code, 0)
        self.assertIn("DRY-RUN", out)
        # no HTTP status line should appear (no mutation executed)
        self.assertNotIn("HTTP 200", out)

    def test_no_yes_refuses_mutation(self):
        args = _args("PATCH", "/api/settings", json_body='{"led_heartbeat_en":false}',
                     dry_run=False, yes=False)
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            code, out = _run_cmd_call(args, [_dev()])
        self.assertEqual(code, 1)
        self.assertIn("ERROR", out)
        self.assertNotIn("HTTP 200", out)

    def test_identity_mismatch_refuses(self):
        args = _args("PATCH", "/api/settings", json_body='{"led_heartbeat_en":false}',
                     dry_run=False, yes=True)
        with patch("fleetlib.discovery.verify_identity", return_value=False):
            code, out = _run_cmd_call(args, [_dev()])
        self.assertEqual(code, 1)
        self.assertIn("ERROR", out)

    def test_yes_flag_allows_mutation(self):
        args = _args("PATCH", "/api/settings", json_body='{"led_heartbeat_en":false}',
                     dry_run=False, yes=True)
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            code, out = _run_cmd_call(
                args, [_dev()],
                client_responses={("PATCH", "/api/settings"): (200, {"ok": True})},
            )
        self.assertEqual(code, 0)
        self.assertIn("HTTP 200", out)


# ---------------------------------------------------------------------------
# Tests: body validation
# ---------------------------------------------------------------------------

class TestCmdCallBodyValidation(unittest.TestCase):
    def setUp(self):
        try:
            import jsonschema  # noqa: F401
        except ImportError:
            self.skipTest("jsonschema not installed")

    def test_valid_body_passes(self):
        args = _args("PATCH", "/api/settings", json_body='{"led_heartbeat_en":true}',
                     dry_run=True, yes=False)
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            code, out = _run_cmd_call(args, [_dev()])
        self.assertEqual(code, 0)
        self.assertIn("DRY-RUN", out)

    def test_invalid_body_fails_with_hint(self):
        # display_en should be boolean, not string
        args = _args("PATCH", "/api/settings", json_body='{"display_en":"notabool"}',
                     dry_run=True, yes=False)
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            code, out = _run_cmd_call(args, [_dev()])
        self.assertEqual(code, 1)
        self.assertIn("ERROR", out)
        self.assertIn("./fleet describe", out)
        self.assertNotIn("HTTP", out)

    def test_no_validate_bypasses_schema_check(self):
        # Same invalid body, but --no-validate
        args = _args("PATCH", "/api/settings", json_body='{"display_en":"notabool"}',
                     dry_run=True, yes=False, no_validate=True)
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            code, out = _run_cmd_call(args, [_dev()])
        self.assertEqual(code, 0)
        self.assertIn("DRY-RUN", out)


# ---------------------------------------------------------------------------
# Tests: body parsing errors
# ---------------------------------------------------------------------------

class TestCmdCallBodyParsing(unittest.TestCase):
    def test_bad_inline_json_returns_1(self):
        import fleet
        args = _args("PATCH", "/api/settings", json_body='{not json}')
        buf = io.StringIO()
        with patch("fleet.resolve_devices", return_value=[_dev()]):
            with patch("builtins.print", lambda *a, **kw: buf.write(" ".join(str(x) for x in a) + "\n")):
                code = fleet.cmd_call(args)
        self.assertEqual(code, 1)
        self.assertIn("not valid JSON", buf.getvalue())

    def test_json_file_loads_body(self):
        body = {"led_heartbeat_en": False}
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as fh:
            json.dump(body, fh)
            fname = fh.name
        try:
            args = _args("PATCH", "/api/settings", json_file=fname, dry_run=True)
            with patch("fleetlib.discovery.verify_identity", return_value=True):
                code, out = _run_cmd_call(args, [_dev()])
            self.assertEqual(code, 0)
            self.assertIn("DRY-RUN", out)
            self.assertIn("led_heartbeat_en", out)
        finally:
            os.unlink(fname)

    def test_json_file_missing_returns_1(self):
        import fleet
        args = _args("PATCH", "/api/settings", json_file="/nonexistent/path.json")
        buf = io.StringIO()
        with patch("fleet.resolve_devices", return_value=[_dev()]):
            with patch("builtins.print", lambda *a, **kw: buf.write(" ".join(str(x) for x in a) + "\n")):
                code = fleet.cmd_call(args)
        self.assertEqual(code, 1)
        self.assertIn("ERROR", buf.getvalue())


# ---------------------------------------------------------------------------
# Tests: --out-json
# ---------------------------------------------------------------------------

class TestCmdCallOutJson(unittest.TestCase):
    def test_out_json_written(self):
        resp = {"internal": {"free": 99000}}
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as fh:
            out_path = fh.name
        try:
            args = _args("GET", "/api/diag/heap", out_json=out_path)
            code, _ = _run_cmd_call(args, [_dev()], client_responses={"/api/diag/heap": resp})
            self.assertEqual(code, 0)
            with open(out_path) as fh:
                data = json.load(fh)
            self.assertEqual(len(data), 1)
            self.assertEqual(data[0]["host"], "192.0.2.1")
            self.assertEqual(data[0]["status"], 200)
        finally:
            os.unlink(out_path)


# ---------------------------------------------------------------------------
# Tests: unknown path warning
# ---------------------------------------------------------------------------

class TestCmdCallUnknownPath(unittest.TestCase):
    def test_unknown_path_warns_but_proceeds(self):
        resp = {"something": 1}
        args = _args("GET", "/api/unknown/path")
        code, out = _run_cmd_call(
            args, [_dev()],
            client_responses={"/api/unknown/path": resp},
        )
        self.assertEqual(code, 0)
        self.assertIn("WARNING", out)
        self.assertIn("not found in served spec", out)


if __name__ == "__main__":
    unittest.main()
