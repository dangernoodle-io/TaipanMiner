"""Tests for fleetlib.readiness — offline unit tests with mocked clients."""
import os
import sys
import time
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.criteria import Criteria
from fleetlib.readiness import wait_until_ready, Readiness, is_ready, ReadinessSnapshot


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_client(info=None, heap=None, stats=None, sensors=None, pool=None, raises=None):
    """Build a mock Client whose get_json returns canned responses per path."""
    c = MagicMock()

    def _get_json(path, timeout=5):
        if raises:
            raise raises
        mapping = {
            "/api/info": info,
            "/api/diag/heap": heap,
            "/api/stats": stats,
            "/api/sensors": sensors,
            "/api/pool": pool,
        }
        return mapping.get(path)

    c.get_json = _get_json
    return c


def _ready_info():
    return {"uptime_ms": 30000, "board": "esp32-wroom32", "version": "v0.69.0"}


def _ready_heap():
    return {"internal": {"free": 80000, "min_free": 75000}}


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestFloorElapsed(unittest.TestCase):
    """wait_until_ready must never return before settle_delay has elapsed."""

    def test_floor_elapsed_small_settle(self):
        """With settle_delay=2 and an immediately-ready device, elapsed >= 2."""
        c = _make_client(info=_ready_info(), heap=_ready_heap())
        criteria = Criteria(
            settle_delay=2,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        r = wait_until_ready(c, None, criteria, timeout=30)
        self.assertTrue(r.ready, f"expected ready=True, reason={r.reason!r}")
        self.assertGreaterEqual(
            r.elapsed_s, 2,
            f"elapsed {r.elapsed_s:.2f}s < settle_delay 2s",
        )

    def test_floor_elapsed_zero_settle(self):
        """With settle_delay=0, returns immediately once conditions met."""
        c = _make_client(info=_ready_info(), heap=_ready_heap())
        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        t0 = time.monotonic()
        r = wait_until_ready(c, None, criteria, timeout=30)
        elapsed = time.monotonic() - t0
        self.assertTrue(r.ready)
        # Should return quickly (well under 5s)
        self.assertLess(elapsed, 5.0)


class TestTimeout(unittest.TestCase):
    """wait_until_ready must return Readiness(ready=False) when timeout expires."""

    def test_timeout_heap_never_meets_floor(self):
        """Device heap stays below floor forever — should time out."""
        c = _make_client(
            info=_ready_info(),
            heap={"internal": {"free": 10000}},  # always below 50k floor
        )
        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        t0 = time.monotonic()
        r = wait_until_ready(c, None, criteria, timeout=8)
        elapsed = time.monotonic() - t0
        self.assertFalse(r.ready, f"expected ready=False, got {r}")
        self.assertGreaterEqual(elapsed, 7)

    def test_timeout_device_unreachable(self):
        """Device never responds — should time out."""
        c = _make_client()  # all endpoints return None
        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        r = wait_until_ready(c, None, criteria, timeout=8)
        self.assertFalse(r.ready)


class TestTransientErrors(unittest.TestCase):
    """wait_until_ready must tolerate transient errors and eventually succeed."""

    def test_transient_connection_errors(self):
        """Client raises ConnectionError for first 3 calls, then returns valid data."""
        call_count = {"n": 0}

        c = MagicMock()

        def _get_json(path, timeout=5):
            if path == "/api/info":
                call_count["n"] += 1
                if call_count["n"] <= 3:
                    raise ConnectionError("transient failure")
                return _ready_info()
            if path == "/api/diag/heap":
                if call_count["n"] <= 3:
                    return None
                return _ready_heap()
            return None

        c.get_json = _get_json

        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        r = wait_until_ready(c, None, criteria, timeout=60)
        self.assertTrue(r.ready, f"expected ready=True after transient errors, reason={r.reason!r}")
        self.assertGreater(call_count["n"], 3, "expected at least 4 calls")

    def test_osError_treated_as_not_ready(self):
        """OSError (connection refused) treated as not-ready, not a crash."""
        call_count = {"n": 0}

        c = MagicMock()

        def _get_json(path, timeout=5):
            call_count["n"] += 1
            if call_count["n"] < 3:
                raise OSError("connection refused")
            if path == "/api/info":
                return _ready_info()
            if path == "/api/diag/heap":
                return _ready_heap()
            return None

        c.get_json = _get_json
        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        r = wait_until_ready(c, None, criteria, timeout=60)
        self.assertTrue(r.ready)


class TestDeviceLike(unittest.TestCase):
    """wait_until_ready accepts a Device-like object (has .ip, .port) in addition to Client."""

    def test_device_object(self):
        """Pass a Device-like object; function must create a Client from .ip/.port."""
        class FakeDevice:
            ip = "127.0.0.1"
            port = 80

        # Patch Client at the module where it's used
        with patch("fleetlib.readiness.Client") as MockClient:
            instance = MagicMock()

            def _get_json(path, timeout=5):
                if path == "/api/info":
                    return _ready_info()
                if path == "/api/diag/heap":
                    return _ready_heap()
                return None

            instance.get_json = _get_json
            MockClient.return_value = instance

            criteria = Criteria(settle_delay=0, readiness_heap_floor=50_000)
            r = wait_until_ready(FakeDevice(), None, criteria, timeout=30)
            self.assertTrue(r.ready)
            MockClient.assert_called_once_with("127.0.0.1", 80)


class TestCapabilityScoping(unittest.TestCase):
    """Pool/hashrate checks are gated on expected_ghs > 0 (mining capability)."""

    def test_non_mining_board_ignores_disconnected_pool(self):
        """Board with expected_ghs=0 is ready even when pool reports not connected."""
        # expected_ghs=0 → non-mining → pool check skipped
        stats = {"hashrate": 0, "expected_ghs": 0.0}
        pool = {"connected": False}
        c = _make_client(
            info=_ready_info(),
            heap=_ready_heap(),
            stats=stats,
            pool=pool,
        )
        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        r = wait_until_ready(c, None, criteria, timeout=30)
        self.assertTrue(r.ready, f"non-mining board should be ready regardless of pool; reason={r.reason!r}")

    def test_mining_board_fails_when_pool_disconnected(self):
        """Board with expected_ghs>0 is NOT ready when pool is disconnected."""
        stats = {"hashrate": 300_000, "expected_ghs": 0.0003}
        pool = {"connected": False}
        c = _make_client(
            info=_ready_info(),
            heap=_ready_heap(),
            stats=stats,
            pool=pool,
        )
        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        r = wait_until_ready(c, None, criteria, timeout=8)
        self.assertFalse(r.ready, f"mining board with pool disconnected should not be ready; reason={r.reason!r}")
        self.assertIn("pool", r.reason.lower())

    def test_mining_board_pool_connected_is_ready(self):
        """Board with expected_ghs>0 is ready when pool is connected."""
        stats = {"hashrate": 300_000, "expected_ghs": 0.0003}
        pool = {"connected": True}
        c = _make_client(
            info=_ready_info(),
            heap=_ready_heap(),
            stats=stats,
            pool=pool,
        )
        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        r = wait_until_ready(c, None, criteria, timeout=30)
        self.assertTrue(r.ready, f"mining board with pool connected should be ready; reason={r.reason!r}")

    def test_stats_unavailable_treated_as_non_mining(self):
        """When /api/stats returns None, board is treated as non-mining (pool check skipped)."""
        pool = {"connected": False}
        c = _make_client(
            info=_ready_info(),
            heap=_ready_heap(),
            stats=None,
            pool=pool,
        )
        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        r = wait_until_ready(c, None, criteria, timeout=30)
        self.assertTrue(r.ready, f"board with no stats should be treated as non-mining; reason={r.reason!r}")

    def test_hashrate_min_respected_for_mining_board(self):
        """Mining board with hashrate below min is NOT ready (hashrate_min > 0)."""
        stats = {"hashrate": 100, "expected_ghs": 0.0003}
        pool = {"connected": True}
        c = _make_client(
            info=_ready_info(),
            heap=_ready_heap(),
            stats=stats,
            pool=pool,
        )
        criteria = Criteria(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=200.0,  # 200 Hz floor
            readiness_vcore_floor=0,
        )
        r = wait_until_ready(c, None, criteria, timeout=8)
        self.assertFalse(r.ready)
        self.assertIn("hashrate", r.reason.lower())


class TestIsReadyPredicate(unittest.TestCase):
    """Unit tests for the shared is_ready(snapshot, criteria) predicate."""

    def _criteria(self, **kw) -> Criteria:
        defaults = dict(
            settle_delay=0,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        defaults.update(kw)
        return Criteria(**defaults)

    # --- heap ---

    def test_heap_below_floor_not_ready(self):
        snap = ReadinessSnapshot(heap_free=10_000, is_mining=False, hashrate_ghs=None, pool_connected=None)
        ready, reasons = is_ready(snap, self._criteria(readiness_heap_floor=50_000))
        self.assertFalse(ready)
        self.assertTrue(any("heap_free" in r for r in reasons), reasons)

    def test_heap_at_floor_ready(self):
        snap = ReadinessSnapshot(heap_free=50_000, is_mining=False, hashrate_ghs=None, pool_connected=None)
        ready, reasons = is_ready(snap, self._criteria(readiness_heap_floor=50_000))
        self.assertTrue(ready, reasons)

    def test_heap_above_floor_ready(self):
        snap = ReadinessSnapshot(heap_free=80_000, is_mining=False, hashrate_ghs=None, pool_connected=None)
        ready, reasons = is_ready(snap, self._criteria(readiness_heap_floor=50_000))
        self.assertTrue(ready, reasons)

    def test_heap_none_not_ready(self):
        snap = ReadinessSnapshot(heap_free=None, is_mining=False, hashrate_ghs=None, pool_connected=None)
        ready, reasons = is_ready(snap, self._criteria())
        self.assertFalse(ready)
        self.assertTrue(any("unavailable" in r for r in reasons), reasons)

    # --- non-mining board ---

    def test_non_mining_board_ready_on_heap_alone(self):
        """Non-mining board: heap met, pool disconnected, low hashrate — still ready."""
        snap = ReadinessSnapshot(
            heap_free=80_000,
            is_mining=False,
            hashrate_ghs=0.0,
            pool_connected=False,
        )
        ready, reasons = is_ready(snap, self._criteria())
        self.assertTrue(ready, reasons)

    # --- mining board + pool ---

    def test_mining_board_pool_disconnected_not_ready(self):
        snap = ReadinessSnapshot(
            heap_free=80_000,
            is_mining=True,
            hashrate_ghs=300.0,
            pool_connected=False,
        )
        ready, reasons = is_ready(snap, self._criteria())
        self.assertFalse(ready)
        self.assertTrue(any("pool" in r for r in reasons), reasons)

    def test_mining_board_pool_connected_ready(self):
        snap = ReadinessSnapshot(
            heap_free=80_000,
            is_mining=True,
            hashrate_ghs=300.0,
            pool_connected=True,
        )
        ready, reasons = is_ready(snap, self._criteria())
        self.assertTrue(ready, reasons)

    def test_mining_board_pool_connected_none_not_penalised(self):
        """pool_connected=None (endpoint absent) must not block readiness."""
        snap = ReadinessSnapshot(
            heap_free=80_000,
            is_mining=True,
            hashrate_ghs=300.0,
            pool_connected=None,
        )
        ready, reasons = is_ready(snap, self._criteria())
        self.assertTrue(ready, reasons)

    # --- mining board + hashrate ---

    def test_mining_board_low_hashrate_not_ready(self):
        snap = ReadinessSnapshot(
            heap_free=80_000,
            is_mining=True,
            hashrate_ghs=0.0001,
            pool_connected=True,
        )
        ready, reasons = is_ready(snap, self._criteria(readiness_hashrate_min=1.0))
        self.assertFalse(ready)
        self.assertTrue(any("hashrate" in r for r in reasons), reasons)

    def test_mining_board_hashrate_min_zero_skipped(self):
        """readiness_hashrate_min=0 disables the hashrate check."""
        snap = ReadinessSnapshot(
            heap_free=80_000,
            is_mining=True,
            hashrate_ghs=0.0,
            pool_connected=True,
        )
        ready, reasons = is_ready(snap, self._criteria(readiness_hashrate_min=0.0))
        self.assertTrue(ready, reasons)

    def test_reasons_empty_when_ready(self):
        snap = ReadinessSnapshot(heap_free=80_000, is_mining=False, hashrate_ghs=None, pool_connected=None)
        ready, reasons = is_ready(snap, self._criteria())
        self.assertTrue(ready)
        self.assertEqual(reasons, [])

    def test_multiple_failures_all_reported(self):
        """When both heap and hashrate fail, both reasons are returned."""
        snap = ReadinessSnapshot(
            heap_free=10_000,
            is_mining=True,
            hashrate_ghs=0.0,
            pool_connected=True,
        )
        ready, reasons = is_ready(snap, self._criteria(
            readiness_heap_floor=50_000,
            readiness_hashrate_min=1.0,
        ))
        self.assertFalse(ready)
        self.assertGreaterEqual(len(reasons), 2)


if __name__ == "__main__":
    unittest.main()
