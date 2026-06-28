"""Suite command factory — creates NAME/HELP/add_arguments/run objects for each suite."""
from __future__ import annotations

import logging
import os
import sys

from core import resolve_devices, unwrap_devices, no_devices_message, build_suite_context


class _SuiteCommand:
    """Module-like command object for a single suite."""

    def __init__(self, name: str) -> None:
        self.NAME = name
        self.HELP = self._get_help(name)

    @staticmethod
    def _get_help(name: str) -> str:
        from core import suite_help
        return suite_help(name)

    def add_arguments(self, parser) -> None:
        from core import add_common_flags, add_suite_arguments
        add_common_flags(parser)
        add_suite_arguments(parser, self.NAME)

    def run(self, args) -> int:
        """Generic suite runner."""
        from suites import load_suite, SUITES

        suite_name = self.NAME

        if suite_name not in SUITES:
            print(f"Suite '{suite_name}' is not yet implemented.")
            return 1

        try:
            mod = load_suite(suite_name)
        except ImportError as exc:
            # Suite module doesn't exist yet (soak/stress/faults/telemetry are stubs)
            print(f"Suite '{suite_name}' not available: {exc}")
            return 1

        ctx = build_suite_context(args, name=suite_name)
        _resolve_result = resolve_devices(args)
        ctx.devices = unwrap_devices(_resolve_result)

        # Forward all suite-specific flags into ctx.extra generically.
        # COMMON_DEST enumerates every dest that add_common_flags and the main parser register,
        # so anything left over must belong to the suite subparser.
        _COMMON_DEST = {
            "hosts", "discover_timeout", "board",
            "fields", "gates", "skip_gates", "out_json", "out_junit", "baseline", "criteria",
            "settle", "dry_run", "yes",
            "log_level", "subcommand", "func",
            "metrics_mqtt_url", "metrics_topic", "no_publish_metrics",
        }
        ctx.extra = {k: v for k, v in vars(args).items() if k not in _COMMON_DEST}

        if not ctx.devices:
            print(no_devices_message(_resolve_result), file=sys.stderr)
            return 1

        print(f"Running {suite_name} suite on {len(ctx.devices)} device(s)…")

        rs = mod.run(ctx)

        # Print summary
        summary = {
            "pass": sum(1 for r in rs.results if r.status == "pass"),
            "fail": sum(1 for r in rs.results if r.status == "fail"),
            "skip": sum(1 for r in rs.results if r.status == "skip"),
        }
        total = sum(summary.values())
        print(f"\nResults: {total} tests — {summary['pass']} pass, {summary['fail']} fail, {summary['skip']} skip")

        # Print failures
        failures = [r for r in rs.results if r.status == "fail"]
        if failures:
            print("\nFAILURES:")
            for r in failures:
                print(f"  FAIL  {r.name}")
                if r.detail:
                    print(f"        {r.detail}")

        # Baseline comparison
        if ctx.baseline:
            regressions = rs.compare_baseline(ctx.baseline)
            if regressions:
                print("\nBASELINE REGRESSIONS:")
                for reg in regressions:
                    print(f"  {reg}")

        # Metrics publishing — default ON when a broker is configured; opt-out via --no-publish-metrics.
        # Never fails the run: publish errors are warned, exit code is unchanged.
        no_publish = getattr(args, "no_publish_metrics", False)
        if not no_publish:
            broker_url = (
                getattr(args, "metrics_mqtt_url", None)
                or os.environ.get("BB_TEST_METRICS_BROKER")
                or os.environ.get("BB_TEST_RECEIVER")
            )
            if broker_url:
                topic_prefix = getattr(args, "metrics_topic", "fleettest") or "fleettest"
                try:
                    rs.push_telemetry(broker_url, topic_prefix=topic_prefix)
                except Exception as _pub_exc:
                    logging.getLogger(__name__).warning(
                        "run metrics publish failed (non-fatal): %s", _pub_exc
                    )
            else:
                print(
                    "note: run metrics not published (no broker configured; "
                    "set --metrics-mqtt-url or BB_TEST_METRICS_BROKER to enable)",
                    file=sys.stderr,
                )

        return 1 if summary["fail"] > 0 else 0


def suite_command(name: str) -> _SuiteCommand:
    """Return a command module-like object for the named suite."""
    return _SuiteCommand(name)
