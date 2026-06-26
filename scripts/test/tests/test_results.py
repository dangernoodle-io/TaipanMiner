"""Tests for fleetlib.results — JSON/JUnit emit and baseline regression flagging."""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device
from fleetlib.results import (
    Result,
    ResultSet,
    STATUS_FAIL,
    STATUS_PASS,
    STATUS_SKIP,
)


def _dev(ip: str = "192.0.2.1", board: str = "test-board") -> Device:
    return Device(hostname="test-host", ip=ip, port=80, board=board, version="v1.0.0")


def _make_set() -> ResultSet:
    rs = ResultSet("test-suite")
    rs.add(Result("soak/heap", _dev(), STATUS_PASS, "all good", {"min_heap": 60_000}))
    rs.add(Result("soak/reboot", _dev("192.0.2.2"), STATUS_FAIL, "rebooted at t=300s", {"reboots": 1}))
    rs.add(Result("soak/skipped", _dev("192.0.2.3"), STATUS_SKIP, "no hardware"))
    return rs


class TestResultSetJsonEmit(unittest.TestCase):
    def test_json_structure(self):
        rs = _make_set()
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False, mode="w") as f:
            path = f.name
        try:
            rs.to_json(path)
            with open(path) as f:
                data = json.load(f)
            self.assertEqual(data["suite"], "test-suite")
            self.assertEqual(data["summary"]["pass"], 1)
            self.assertEqual(data["summary"]["fail"], 1)
            self.assertEqual(data["summary"]["skip"], 1)
            self.assertEqual(len(data["results"]), 3)
        finally:
            os.unlink(path)

    def test_json_metrics_preserved(self):
        rs = ResultSet("m")
        rs.add(Result("r", _dev(), STATUS_PASS, "", {"min_heap": 55000, "hashrate": 485.0}))
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False, mode="w") as f:
            path = f.name
        try:
            rs.to_json(path)
            with open(path) as f:
                data = json.load(f)
            self.assertEqual(data["results"][0]["metrics"]["min_heap"], 55000)
            self.assertEqual(data["results"][0]["metrics"]["hashrate"], 485.0)
        finally:
            os.unlink(path)


class TestResultSetJunitEmit(unittest.TestCase):
    def test_junit_contains_testsuite(self):
        rs = _make_set()
        with tempfile.NamedTemporaryFile(suffix=".xml", delete=False, mode="w") as f:
            path = f.name
        try:
            rs.to_junit(path)
            with open(path) as f:
                content = f.read()
            self.assertIn("testsuite", content)
            self.assertIn("testcase", content)
        finally:
            os.unlink(path)

    def test_junit_failure_element(self):
        rs = _make_set()
        with tempfile.NamedTemporaryFile(suffix=".xml", delete=False, mode="w") as f:
            path = f.name
        try:
            rs.to_junit(path)
            with open(path) as f:
                content = f.read()
            self.assertIn("failure", content)
            self.assertIn("rebooted at t=300s", content)
        finally:
            os.unlink(path)

    def test_junit_skipped_element(self):
        rs = _make_set()
        with tempfile.NamedTemporaryFile(suffix=".xml", delete=False, mode="w") as f:
            path = f.name
        try:
            rs.to_junit(path)
            with open(path) as f:
                content = f.read()
            self.assertIn("skipped", content)
        finally:
            os.unlink(path)

    def test_junit_counts(self):
        rs = _make_set()
        with tempfile.NamedTemporaryFile(suffix=".xml", delete=False, mode="w") as f:
            path = f.name
        try:
            rs.to_junit(path)
            with open(path) as f:
                content = f.read()
            self.assertIn('failures="1"', content)
            self.assertIn('skipped="1"', content)
            self.assertIn('tests="3"', content)
        finally:
            os.unlink(path)


class TestBaselineRegression(unittest.TestCase):
    def _write_baseline(self, results: list) -> str:
        data = {"suite": "test", "results": results}
        with tempfile.NamedTemporaryFile(
            suffix=".json", delete=False, mode="w"
        ) as f:
            json.dump(data, f)
            return f.name

    def test_min_heap_regression(self):
        rs = ResultSet("t")
        rs.add(Result("soak/heap", _dev(), STATUS_PASS, "", {"min_heap": 40_000}))
        path = self._write_baseline([
            {"name": "soak/heap", "metrics": {"min_heap": 55_000}}
        ])
        try:
            regressions = rs.compare_baseline(path)
            self.assertEqual(len(regressions), 1)
            self.assertIn("min_heap", regressions[0])
            self.assertIn("regression", regressions[0])
        finally:
            os.unlink(path)

    def test_reboots_regression(self):
        rs = ResultSet("t")
        rs.add(Result("soak/reboot", _dev(), STATUS_FAIL, "", {"reboots": 2}))
        path = self._write_baseline([
            {"name": "soak/reboot", "metrics": {"reboots": 0}}
        ])
        try:
            regressions = rs.compare_baseline(path)
            self.assertEqual(len(regressions), 1)
            self.assertIn("reboots", regressions[0])
        finally:
            os.unlink(path)

    def test_no_regression(self):
        rs = ResultSet("t")
        rs.add(Result("soak/heap", _dev(), STATUS_PASS, "", {"min_heap": 60_000}))
        path = self._write_baseline([
            {"name": "soak/heap", "metrics": {"min_heap": 55_000}}
        ])
        try:
            regressions = rs.compare_baseline(path)
            self.assertEqual(regressions, [])
        finally:
            os.unlink(path)

    def test_missing_baseline_file(self):
        rs = ResultSet("t")
        rs.add(Result("r", _dev(), STATUS_PASS, ""))
        regressions = rs.compare_baseline("/nonexistent/baseline.json")
        self.assertEqual(len(regressions), 1)
        self.assertIn("could not load baseline", regressions[0])

    def test_new_result_not_in_baseline(self):
        rs = ResultSet("t")
        rs.add(Result("soak/new", _dev(), STATUS_PASS, "", {"min_heap": 40_000}))
        path = self._write_baseline([])  # empty baseline
        try:
            regressions = rs.compare_baseline(path)
            self.assertEqual(regressions, [])  # no match = no regression
        finally:
            os.unlink(path)


class TestBaselineRegressionNewMetrics(unittest.TestCase):
    """TA-449: new per-run summary metrics regress correctly in compare_baseline."""

    def _write_baseline(self, results: list) -> str:
        data = {"suite": "test", "results": results}
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False, mode="w") as f:
            json.dump(data, f)
            return f.name

    def test_heap_free_min_regression(self):
        rs = ResultSet("t")
        rs.add(Result("soak/heap", _dev(), STATUS_PASS, "", {"heap_free_min": 40_000}))
        path = self._write_baseline([{"name": "soak/heap", "metrics": {"heap_free_min": 60_000}}])
        try:
            regressions = rs.compare_baseline(path)
            self.assertEqual(len(regressions), 1)
            self.assertIn("heap_free_min", regressions[0])
            self.assertIn("regression", regressions[0])
        finally:
            os.unlink(path)

    def test_hashrate_avg_regression(self):
        rs = ResultSet("t")
        rs.add(Result("soak/hr", _dev(), STATUS_PASS, "", {"hashrate_avg": 400.0}))
        path = self._write_baseline([{"name": "soak/hr", "metrics": {"hashrate_avg": 480.0}}])
        try:
            regressions = rs.compare_baseline(path)
            self.assertEqual(len(regressions), 1)
            self.assertIn("hashrate_avg", regressions[0])
        finally:
            os.unlink(path)

    def test_temp_max_regression(self):
        rs = ResultSet("t")
        rs.add(Result("soak/temp", _dev(), STATUS_PASS, "", {"temp_max": 85.0}))
        path = self._write_baseline([{"name": "soak/temp", "metrics": {"temp_max": 70.0}}])
        try:
            regressions = rs.compare_baseline(path)
            self.assertEqual(len(regressions), 1)
            self.assertIn("temp_max", regressions[0])
            self.assertIn("regression", regressions[0])
        finally:
            os.unlink(path)

    def test_anomaly_count_regression(self):
        rs = ResultSet("t")
        rs.add(Result("soak/a", _dev(), STATUS_FAIL, "", {"anomaly_count": 3}))
        path = self._write_baseline([{"name": "soak/a", "metrics": {"anomaly_count": 0}}])
        try:
            regressions = rs.compare_baseline(path)
            self.assertEqual(len(regressions), 1)
            self.assertIn("anomaly_count", regressions[0])
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
