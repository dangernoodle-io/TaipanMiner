"""Tests for fleetlib.spec — schema navigation and validation."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.spec import Spec

# Minimal OpenAPI 3.1 doc with structured heap fields (as served by TaipanMiner)
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
                                        "heap": {
                                            "type": "object",
                                            "properties": {
                                                "internal": {
                                                    "type": "object",
                                                    "properties": {
                                                        "free":     {"type": "integer"},
                                                        "min_free": {"type": "integer"},
                                                    },
                                                }
                                            },
                                        },
                                    },
                                    "required": ["hostname", "version"],
                                }
                            }
                        }
                    }
                }
            }
        },
        "/api/diag/asic": {
            "get": {
                "responses": {
                    "200": {
                        "content": {
                            "application/json": {
                                "schema": {"type": "object"}
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
                                    "hostname": {"type": "string"},
                                }
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


class TestSpec(unittest.TestCase):
    def setUp(self):
        self.spec = Spec(SAMPLE_SPEC)

    def test_paths_contains_known(self):
        paths = self.spec.paths()
        self.assertIn("/api/info", paths)
        self.assertIn("/api/diag/asic", paths)

    def test_has_path_true(self):
        self.assertTrue(self.spec.has_path("/api/info"))

    def test_has_path_false(self):
        self.assertFalse(self.spec.has_path("/api/nonexistent"))

    def test_methods(self):
        self.assertIn("get", self.spec.methods("/api/info"))
        self.assertIn("patch", self.spec.methods("/api/settings"))

    def test_response_schema_type(self):
        schema = self.spec.response_schema("/api/info")
        self.assertIsNotNone(schema)
        self.assertEqual(schema["type"], "object")

    def test_response_schema_properties(self):
        schema = self.spec.response_schema("/api/info")
        self.assertIn("hostname", schema["properties"])
        self.assertIn("heap", schema["properties"])

    def test_response_schema_missing_path(self):
        self.assertIsNone(self.spec.response_schema("/nonexistent"))

    def test_request_schema(self):
        schema = self.spec.request_schema("/api/settings", "patch")
        self.assertIsNotNone(schema)
        self.assertIn("hostname", schema["properties"])

    def test_request_schema_missing(self):
        self.assertIsNone(self.spec.request_schema("/api/info", "get"))

    def test_validate_valid_response(self):
        try:
            import jsonschema  # noqa: F401
        except ImportError:
            self.skipTest("jsonschema not installed")
        errors = self.spec.validate("/api/info", "get", {
            "hostname": "taipan-test",
            "version": "v1.0.0",
            "uptime_ms": 12345,
            "heap": {"internal": {"free": 80000, "min_free": 75000}},
        })
        self.assertEqual(errors, [], msg=f"unexpected errors: {errors}")

    def test_validate_missing_required_field(self):
        try:
            import jsonschema  # noqa: F401
        except ImportError:
            self.skipTest("jsonschema not installed")
        # missing required "hostname" and "version"
        errors = self.spec.validate("/api/info", "get", {"uptime_ms": 123})
        self.assertTrue(len(errors) > 0, "expected validation errors for missing required fields")

    def test_validate_wrong_type(self):
        try:
            import jsonschema  # noqa: F401
        except ImportError:
            self.skipTest("jsonschema not installed")
        errors = self.spec.validate("/api/info", "get", {
            "hostname": "taipan-test",
            "version": "v1.0.0",
            "uptime_ms": "not-an-integer",  # wrong type
        })
        self.assertTrue(len(errors) > 0, "expected type error")

    def test_validate_no_schema_returns_empty(self):
        try:
            import jsonschema  # noqa: F401
        except ImportError:
            self.skipTest("jsonschema not installed")
        errors = self.spec.validate("/api/nonexistent", "get", {"anything": 1})
        self.assertEqual(errors, [])

    def test_validate_no_jsonschema_raises(self):
        import importlib
        import unittest.mock as mock
        spec = Spec(SAMPLE_SPEC)
        with mock.patch.dict("sys.modules", {"jsonschema": None}):
            import fleetlib.spec as spec_mod
            orig = spec_mod._HAS_JSONSCHEMA
            spec_mod._HAS_JSONSCHEMA = False
            try:
                with self.assertRaises(ImportError):
                    spec.validate("/api/info", "get", {})
            finally:
                spec_mod._HAS_JSONSCHEMA = orig


if __name__ == "__main__":
    unittest.main()
