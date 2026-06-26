"""Test result types, serialization (JSON + JUnit XML), and baseline comparison."""
from __future__ import annotations
import json
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from .discovery import Device

STATUS_PASS = "pass"
STATUS_FAIL = "fail"
STATUS_SKIP = "skip"


@dataclass
class Result:
    name: str
    device: "Device"
    status: str          # STATUS_PASS | STATUS_FAIL | STATUS_SKIP
    detail: str = ""
    metrics: Dict[str, Any] = field(default_factory=dict)
    timestamp: float = field(default_factory=time.time)
    logs: Optional[List[str]] = field(default=None)


class ResultSet:
    """Collection of Results with serialization and baseline comparison."""

    def __init__(self, suite_name: str = "fleet-test"):
        self.suite_name = suite_name
        self.results: List[Result] = []

    def add(self, result: Result) -> None:
        self.results.append(result)

    def _to_dict(self) -> dict:
        return {
            "suite": self.suite_name,
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "results": [
                {
                    "name": r.name,
                    "device": r.device.ip,
                    "board": r.device.board,
                    "status": r.status,
                    "detail": r.detail,
                    "metrics": r.metrics,
                    "timestamp": r.timestamp,
                    "logs": r.logs,
                }
                for r in self.results
            ],
            "summary": {
                "pass": sum(1 for r in self.results if r.status == STATUS_PASS),
                "fail": sum(1 for r in self.results if r.status == STATUS_FAIL),
                "skip": sum(1 for r in self.results if r.status == STATUS_SKIP),
            },
        }

    def to_json(self, path: str) -> None:
        """Write results to a JSON file."""
        with open(path, "w") as f:
            json.dump(self._to_dict(), f, indent=2)

    def to_junit(self, path: str) -> None:
        """Write results as JUnit XML (testsuite/testcase/failure/skipped)."""
        suite = ET.Element("testsuite", {
            "name": self.suite_name,
            "tests": str(len(self.results)),
            "failures": str(sum(1 for r in self.results if r.status == STATUS_FAIL)),
            "skipped": str(sum(1 for r in self.results if r.status == STATUS_SKIP)),
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        })
        for r in self.results:
            tc = ET.SubElement(suite, "testcase", {
                "name": r.name,
                "classname": f"{r.device.board}.{r.device.ip}",
            })
            if r.status == STATUS_FAIL:
                f_el = ET.SubElement(tc, "failure", {"message": r.detail[:120]})
                f_el.text = r.detail
            elif r.status == STATUS_SKIP:
                ET.SubElement(tc, "skipped", {"message": r.detail})
        tree = ET.ElementTree(suite)
        ET.indent(tree, space="  ")
        tree.write(path, encoding="unicode", xml_declaration=True)

    def compare_baseline(self, prev_json_path: str) -> List[str]:
        """Compare tracked numeric metrics against a prior JSON result file.

        Returns a list of regression strings (empty = no regressions).

        Tracked directions:
          HIGHER_BETTER: min_heap, hashrate, heap_free_floor  (regression = cur < prev)
          LOWER_BETTER:  missed_polls, reboots, wdt_resets    (regression = cur > prev)
        """
        try:
            with open(prev_json_path) as f:
                prev = json.load(f)
        except Exception as e:
            return [f"could not load baseline {prev_json_path!r}: {e}"]

        prev_by_name: Dict[str, dict] = {
            r["name"]: r for r in prev.get("results", [])
        }
        regressions: List[str] = []
        HIGHER_BETTER = {
            "min_heap", "hashrate", "heap_free_floor", "hashrate_ghs",
            "heap_free_min", "heap_min_free_min", "largest_block_min",
            "hashrate_min", "hashrate_avg", "hashrate_pct_expected_min",
        }
        LOWER_BETTER = {
            "missed_polls", "reboots", "wdt_resets",
            "temp_max", "reboot_count", "anomaly_count",
        }

        for r in self.results:
            if r.name not in prev_by_name:
                continue
            prev_r = prev_by_name[r.name]
            for metric, cur_val in r.metrics.items():
                if not isinstance(cur_val, (int, float)):
                    continue
                prev_val = prev_r.get("metrics", {}).get(metric)
                if prev_val is None or not isinstance(prev_val, (int, float)):
                    continue
                if metric in HIGHER_BETTER and cur_val < prev_val:
                    regressions.append(
                        f"{r.name}/{metric}: {cur_val} < baseline {prev_val} (regression)"
                    )
                elif metric in LOWER_BETTER and cur_val > prev_val:
                    regressions.append(
                        f"{r.name}/{metric}: {cur_val} > baseline {prev_val} (regression)"
                    )
        return regressions

    def push_influxdb(
        self,
        url: str,
        token: str,
        org: str,
        bucket: str,
    ) -> None:
        """Push metrics to InfluxDB (stub — no-op when url is empty).

        Real implementation would use influxdb-client-python or urllib
        line-protocol POST. Enabled only when url is non-empty and the
        caller explicitly passes credentials.
        """
        if not url:
            return
        # stub: future implementation
