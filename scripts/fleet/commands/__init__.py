"""Register built-in commands into the COMMANDS registry.

Registration order matches _build_parser subparser addition order exactly
so that root --help subcommand listing is byte-identical to existing fixtures.
"""
from registry import COMMANDS
from commands import discover as _discover
from commands import status as _status
from commands import probe_endpoints as _probe_endpoints
from commands._suite import suite_command as _suite_command
from commands import describe as _describe
from commands import call as _call
from commands import watch as _watch
from commands import logs as _logs
from commands import ota as _ota
from commands import decode as _decode
from commands import elf as _elf

# Top-level non-suite commands
COMMANDS["discover"] = _discover
COMMANDS["status"] = _status
COMMANDS["probe-endpoints"] = _probe_endpoints

# Suites — in the same order as _build_parser
COMMANDS["functional"] = _suite_command("functional")
COMMANDS["soak"] = _suite_command("soak")
COMMANDS["stress"] = _suite_command("stress")
COMMANDS["faults"] = _suite_command("faults")
COMMANDS["telemetry"] = _suite_command("telemetry")

# Remaining top-level commands
COMMANDS["describe"] = _describe
COMMANDS["call"] = _call
COMMANDS["watch"] = _watch
COMMANDS["logs"] = _logs
COMMANDS["ota"] = _ota
COMMANDS["decode"] = _decode
COMMANDS["elf"] = _elf
