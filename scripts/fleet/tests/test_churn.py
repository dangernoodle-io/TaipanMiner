"""Unit tests for suites/churn.py (offline, mock urlopen + mock sampling)."""
import os
import sys
import time
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.criteria import Criteria
from fleetlib.discovery import Device
from fleetlib.monitor import Sample
from fleetlib.results import ResultSet, STATUS_PASS, STATUS_FAIL


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_device(ip="192.0.2.1", board="esp32-wroom32") -> Device:
    return Device(hostname=board, ip=ip, port=80, board=board, version="v0.99.0")


def _make_sample(device, ok=True, free=80_000, min_free=70_000, largest=60_000,
                  uptime_ms=300_000) -> Sample:
    info = {"uptime_ms": uptime_ms, "reset_reason": "normal", "wdt_resets": 0} if ok else None
    heap = {
        "internal": {"free": free, "min_free": min_free, "largest_free_block": largest}
    } if ok else None
    return Sample(
        device=device,
        timestamp=time.time(),
        info=info,
        heap=heap,
        telemetry=None,
        sensors=None,
        stats=None,
        ok=ok,
    )


def _make_ctx(devices, criteria=None, extra=None):
    from suites import SuiteContext, SettleConfig
    from fleetlib.safety import Guard
    rs = ResultSet("churn")
    return SuiteContext(
        devices=devices,
        criteria=criteria or Criteria(),
        guard=Guard(dry_run=True),
        results=rs,
        fields=None,
        gates=set(),
        settle=SettleConfig(settle_delay=0, enabled=False),
        out_json=None,
        out_junit=None,
        baseline=None,
        extra=extra or {},
    )


def _mock_resp():
    """A fake urlopen() response supporting read()/close()."""
    resp = MagicMock()
    resp.read.return_value = b"data: {}\n\n"
    resp.close.return_value = None
    return resp


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestChurnWorkerConnectCycles(unittest.TestCase):
    """Churn workers must actually issue connect+close cycles against /api/events."""

    def test_churn_worker_issues_multiple_connect_cycles(self):
        from suites.churn import _churn_worker, _ChurnCounter
        import threading

        device = _make_device()
        counter = _ChurnCounter()
        stop_event = threading.Event()

        calls = []

        def _fake_urlopen(req, timeout=None):
            calls.append(req.full_url)
            return _mock_resp()

        with patch("urllib.request.urlopen", side_effect=_fake_urlopen), \
             patch("suites.churn._CHURN_CYCLE_SLEEP", 0.01):
            t = threading.Thread(target=_churn_worker, args=(device, "health.alerts", stop_event, counter))
            t.start()
            time.sleep(0.15)
            stop_event.set()
            t.join(timeout=5)

        self.assertGreater(len(calls), 1, "expected multiple connect cycles")
        self.assertTrue(all("/api/events?topic=health.alerts" in u for u in calls))
        self.assertEqual(counter.successes, len(calls))

    def test_churn_worker_no_topic_omits_query_param(self):
        from suites.churn import _churn_worker, _ChurnCounter
        import threading

        device = _make_device()
        counter = _ChurnCounter()
        stop_event = threading.Event()
        calls = []

        def _fake_urlopen(req, timeout=None):
            calls.append(req.full_url)
            stop_event.set()  # single cycle is enough for this assertion
            return _mock_resp()

        with patch("urllib.request.urlopen", side_effect=_fake_urlopen), \
             patch("suites.churn._CHURN_CYCLE_SLEEP", 0.01):
            t = threading.Thread(target=_churn_worker, args=(device, "", stop_event, counter))
            t.start()
            t.join(timeout=5)

        self.assertEqual(calls, [f"http://{device.ip}/api/events"])

    def test_churn_once_read_timeout_still_counts_as_connect(self):
        """A read timeout after a successful urlopen() still counts as a churn
        cycle — the server already allocated the SSE buffer/task."""
        from suites.churn import _churn_once

        resp = _mock_resp()
        resp.read.side_effect = TimeoutError("idle topic")

        with patch("urllib.request.urlopen", return_value=resp):
            ok = _churn_once("http://192.0.2.1/api/events")

        self.assertTrue(ok)
        resp.close.assert_called_once()


class TestChurnPerHostIsolation(unittest.TestCase):
    """An unreachable host must be skipped by both churn + sampling without
    aborting the run or the other host's threads."""

    def test_unreachable_host_churn_keeps_looping(self):
        from suites.churn import _churn_worker, _ChurnCounter
        import threading

        device = _make_device(ip="192.0.2.99")
        counter = _ChurnCounter()
        stop_event = threading.Event()

        def _always_fail(req, timeout=None):
            raise ConnectionRefusedError("simulated unreachable host")

        with patch("urllib.request.urlopen", side_effect=_always_fail), \
             patch("suites.churn._CHURN_CYCLE_SLEEP", 0.01):
            t = threading.Thread(target=_churn_worker, args=(device, "", stop_event, counter))
            t.start()
            time.sleep(0.1)
            stop_event.set()
            t.join(timeout=5)

        # No successful connects, but the worker thread must have exited
        # cleanly (join didn't time out) rather than crashing/hanging.
        self.assertFalse(t.is_alive())
        self.assertEqual(counter.successes, 0)

    def test_unreachable_host_does_not_block_healthy_host(self):
        """Two devices: one always-unreachable for both churn + sampling, one
        healthy. The healthy host must still produce a PASS result and
        samples; the unreachable host must not abort the run."""
        from suites import churn

        dev_ok = _make_device(ip="192.0.2.10")
        dev_down = _make_device(ip="192.0.2.20")
        ctx = _make_ctx(
            [dev_ok, dev_down],
            extra={"duration": 0.3, "interval": 0.05, "sse_churn": 1},
        )

        def _fake_sample(dev, fset):
            if dev.ip == dev_down.ip:
                return _make_sample(dev, ok=False)
            return _make_sample(dev)

        def _fake_urlopen(req, timeout=None):
            if dev_down.ip in req.full_url:
                raise ConnectionRefusedError("simulated unreachable host")
            return _mock_resp()

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample), \
             patch("urllib.request.urlopen", side_effect=_fake_urlopen), \
             patch("suites.churn._CHURN_CYCLE_SLEEP", 0.02):
            churn.run(ctx)

        results = ctx.results.results
        self.assertEqual(len(results), 2)
        ok_result = next(r for r in results if r.device.ip == dev_ok.ip)
        down_result = next(r for r in results if r.device.ip == dev_down.ip)
        self.assertEqual(ok_result.status, STATUS_PASS)
        self.assertGreater(ok_result.metrics["churn_conns"], 0)
        # Unreachable host still produces a result (not aborted); zero conns.
        self.assertEqual(down_result.metrics["churn_conns"], 0)


class TestChurnConcurrentSampling(unittest.TestCase):
    """Sampling must run concurrently with churn and record largest_block."""

    def test_sampling_runs_concurrently_with_churn(self):
        from suites import churn

        device = _make_device()
        ctx = _make_ctx([device], extra={"duration": 0.3, "interval": 0.05, "sse_churn": 2})

        sample_calls = []

        def _fake_sample(dev, fset):
            sample_calls.append(set(fset))
            return _make_sample(dev, free=50_000, min_free=40_000, largest=30_000)

        connect_calls = []

        def _fake_urlopen(req, timeout=None):
            connect_calls.append(req.full_url)
            return _mock_resp()

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample), \
             patch("urllib.request.urlopen", side_effect=_fake_urlopen), \
             patch("suites.churn._CHURN_CYCLE_SLEEP", 0.02):
            churn.run(ctx)

        # Both sampling and churn connects happened during the same window.
        self.assertGreater(len(sample_calls), 0)
        self.assertGreater(len(connect_calls), 0)
        self.assertTrue(all({"info", "heap"} <= f for f in sample_calls))

        result = ctx.results.results[0]
        self.assertEqual(result.metrics["largest_block_min"], 30_000)
        self.assertEqual(result.metrics["free_min"], 50_000)
        self.assertEqual(result.metrics["min_free"], 40_000)


class TestChurnSummaryAggregation(unittest.TestCase):
    """Summary/result aggregation: largest_block_min, free_min, min_free,
    sample count, conns used."""

    def _run(self, sample_fn, extra=None):
        from suites import churn
        device = _make_device()
        base_extra = {"duration": 0.3, "interval": 0.05, "sse_churn": 1}
        if extra:
            base_extra.update(extra)
        ctx = _make_ctx([device], extra=base_extra)

        with patch("fleetlib.monitor._sample_device", side_effect=sample_fn), \
             patch("urllib.request.urlopen", side_effect=lambda req, timeout=None: _mock_resp()), \
             patch("suites.churn._CHURN_CYCLE_SLEEP", 0.02):
            churn.run(ctx)
        return ctx.results.results[0]

    def test_metrics_take_min_across_samples(self):
        frees = iter([90_000, 70_000, 80_000])
        min_frees = iter([60_000, 55_000, 58_000])
        largest = iter([50_000, 20_000, 40_000])

        def _fake(dev, fset):
            return _make_sample(
                dev,
                free=next(frees, 80_000),
                min_free=next(min_frees, 55_000),
                largest=next(largest, 20_000),
            )

        result = self._run(_fake)
        self.assertEqual(result.metrics["free_min"], 70_000)
        self.assertEqual(result.metrics["min_free"], 55_000)
        self.assertEqual(result.metrics["largest_block_min"], 20_000)
        self.assertGreater(result.metrics["sample_count"], 0)
        self.assertGreater(result.metrics["churn_conns"], 0)
        self.assertIn("anomaly_count", result.metrics)

    def test_diffable_aliases_match_soak_key_names(self):
        """heap_free_min/heap_min_free_min mirror soak's key names for diffability."""
        result = self._run(lambda dev, fset: _make_sample(dev, free=42_000, min_free=41_000))
        self.assertEqual(result.metrics["heap_free_min"], result.metrics["free_min"])
        self.assertEqual(result.metrics["heap_min_free_min"], result.metrics["min_free"])

    def test_zero_churn_workers_still_samples(self):
        """--sse-churn 0: no churn threads, but sampling still runs and reports."""
        result = self._run(lambda dev, fset: _make_sample(dev), extra={"sse_churn": 0})
        self.assertEqual(result.metrics["churn_conns"], 0)
        self.assertGreater(result.metrics["sample_count"], 0)
        self.assertEqual(result.status, STATUS_PASS)

    def test_heap_floor_anomaly_fails_result(self):
        """Reuses monitor's heap_floor detector: free below criteria floor -> FAIL."""
        result = self._run(lambda dev, fset: _make_sample(dev, free=1_000, min_free=500))
        self.assertEqual(result.status, STATUS_FAIL)
        self.assertGreater(result.metrics["anomaly_count"], 0)


class TestChurnVersionCheck(unittest.TestCase):
    def test_version_mismatch_fails_without_churning(self):
        from suites import churn

        device = Device(hostname="wroom32", ip="192.0.2.5", port=80,
                        board="esp32-wroom32", version="v0.68.0")
        ctx = _make_ctx([device], extra={"duration": 0.2, "interval": 0.05, "target": "v0.99.0"})

        with patch("urllib.request.urlopen") as mock_urlopen:
            churn._run_device(device, ctx, ctx.results)

        mock_urlopen.assert_not_called()
        results = ctx.results.results
        self.assertEqual(results[0].status, STATUS_FAIL)
        self.assertIn("version", results[0].detail)


if __name__ == "__main__":
    unittest.main()
