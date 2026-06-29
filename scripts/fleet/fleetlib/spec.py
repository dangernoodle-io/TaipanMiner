"""Wraps a served OpenAPI 3.1 document for a TaipanMiner device."""
from __future__ import annotations
from typing import Any, List, Optional

try:
    import jsonschema as _jsonschema
    _HAS_JSONSCHEMA = True
except ImportError:
    _HAS_JSONSCHEMA = False


class Spec:
    """Wraps a parsed OpenAPI 3.1 dict (as served from GET /api/openapi.json)."""

    def __init__(self, doc: dict):
        self._doc = doc

    def paths(self) -> List[str]:
        """List all path keys declared in the spec."""
        return list(self._doc.get("paths", {}).keys())

    def has_path(self, path: str) -> bool:
        return path in self._doc.get("paths", {})

    def methods(self, path: str) -> List[str]:
        """HTTP methods declared for path (lowercase, e.g. ['get', 'patch'])."""
        return list(self._doc.get("paths", {}).get(path, {}).keys())

    def response_schema(
        self, path: str, method: str = "get", status: str = "200"
    ) -> Optional[dict]:
        """Navigate paths[path][method].responses[status].content['application/json'].schema.

        Schemas are inline (components.schemas is empty on TaipanMiner devices).
        Returns None if any navigation step is missing.
        """
        try:
            return (
                self._doc["paths"][path][method.lower()]
                ["responses"][status]
                ["content"]["application/json"]["schema"]
            )
        except (KeyError, TypeError):
            return None

    def request_schema(self, path: str, method: str = "patch") -> Optional[dict]:
        """Navigate paths[path][method].requestBody.content['application/json'].schema."""
        try:
            return (
                self._doc["paths"][path][method.lower()]
                ["requestBody"]["content"]["application/json"]["schema"]
            )
        except (KeyError, TypeError):
            return None

    def request_required(self, path: str, method: str = "patch") -> bool:
        """Return requestBody.required for the given path/method (default False)."""
        try:
            return bool(
                self._doc["paths"][path][method.lower()]
                ["requestBody"].get("required", False)
            )
        except (KeyError, TypeError):
            return False

    def validate(self, path: str, method: str, response_json: Any) -> List[str]:
        """Validate response_json against the schema served for this path/method.

        Uses jsonschema Draft 2020-12 (OpenAPI 3.1 == JSON Schema 2020-12).
        Returns a list of human-readable error strings (empty = valid).
        Raises ImportError if jsonschema is not installed.
        """
        if not _HAS_JSONSCHEMA:
            raise ImportError(
                "jsonschema is required for spec validation; "
                "install it with: pip install jsonschema"
            )
        schema = self.response_schema(path, method)
        if schema is None:
            return []  # no schema to validate against

        errors: List[str] = []
        try:
            validator = _jsonschema.Draft202012Validator(schema)
            for err in validator.iter_errors(response_json):
                loc = ".".join(str(p) for p in err.absolute_path) or "<root>"
                errors.append(f"{loc}: {err.message}")
        except Exception as e:
            errors.append(f"validator error: {e}")
        return errors
