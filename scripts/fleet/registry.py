"""Command registry + plugin API."""
from __future__ import annotations
import sys
from typing import Any

# Global registry: name -> command module
COMMANDS: dict[str, Any] = {}


class PluginAPI:
    """API surface exposed to plugin .py files via register(api)."""

    def add_command(self, name: str, module: Any) -> None:
        if name in COMMANDS:
            print(f"fleet: plugin command collision '{name}' — ignored", file=sys.stderr)
            return
        COMMANDS[name] = module
