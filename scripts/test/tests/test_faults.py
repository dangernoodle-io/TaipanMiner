"""Offline unit tests for the faults suite — inject->recovery flows + guard enforcement.

All destructive I/O is mocked: no real sockets opened, no docker run, no device touched.
"""
import os
import sys
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.criteria import Criteria
from fleetlib.discovery import Device
from fleetlib.readiness import Readiness
from fleetlib.results import ResultSet
from fleetlib.safety import Guard
from suites import SuiteContext

import suites.faults as faults
from suites.faults import socket as socket_scn
from suites.faults import broker_outage as broker_scn
from suites.faults import vcore_drop as vcore_scn
from suites.faults._common import guard_step, PROCEED, DRYRUN, REFUSED


# --------------------------------------------------------------------------- helpers

def _device(board="esp32-wroom32", ip="192.0.2.10"):
    return Device(hostname="host", ip=ip, port=80, board=board, version="v0.69.0")


def _criteria():
    return Criteria(settle_delay=0, heap_floor=50_000, readiness_heap_floor=50_000)


class FakeSettle:
    def __init__(self, ready=True, settle_delay=0, enabled=True):
        self._ready = ready
        self.settle_delay = settle_delay
        self.enabled = enabled

    def wait_ready(self, device, criteria=None):
        return Readiness(ready=self._ready, elapsed_s=0.0,
                         reason="ready" if self._ready else "not ready")


def _ctx(devices, guard, settle, gates=None, extra=None, criteria=None):
    return SuiteContext(
        devices=devices, criteria=criteria or _criteria(), guard=guard,
        results=ResultSet("faults"), fields=None, gates=gates or set(),
        settle=settle, out_json=None, out_junit=None, baseline=None,
        extra=extra or {},
    )


def _mk_client(state):
    """Mock Client serving canned endpoints from a mutable `state` dict."""
    c = MagicMock()

    def gj(path, timeout=5):
        if path == "/api/info":
            return {"uptime_ms": state.get("uptime", 60_000),
                    "free_heap": state.get("heap", 80_000),
                    "board": state.get("board", "esp32-wroom32")}
        if path == "/api/diag/heap":
            return {"internal": {"free": state.get("heap", 80_000)}}
        if path == "/api/diag/sockets":
            sv = state.get("sockets", 4)
            if sv is None:
                return None
            return {"in_use": sv, "lwip_max_sockets": state.get("max_sockets", 16)}
        if path == "/api/diag/panic":
            return state.get("panic", {"task": None, "panic_reason": None, "count": 0})
        if path == "/api/sensors":
            return {"miner": {"vcore_mv": state.get("vcore", 1200)}}
        if path == "/api/pool":
            return {"connected": True}
        if path == "/api/stats":
            return {"hashrate_ghs": 480.0}
        return None

    c.get_json = gj
    c.request = MagicMock(return_value=(200, b""))
    c.spec = state.get("spec")
    return c


def _ok_guard(board="esp32-wroom32"):
    return Guard(dry_run=False, confirm=True, expect_board=board)


def _statuses(rs):
    return [r.status for r in rs.results]


# --------------------------------------------------------------------------- socket

class TestSocketScenario(unittest.TestCase):
    def test_recovery_pass(self):
        state = {}
        client = _mk_client(state)
        dev = _device()
        ctx = _ctx([dev], _ok_guard(), FakeSettle(ready=True))
        with patch("suites.faults.socket.Client", return_value=client), \
             patch("suites.faults.socket._drive_sockets", return_value=20), \
             patch("fleetlib.discovery._read_identity", return_value=("esp32-wroom32", "test-host")):
            socket_scn.run_device(dev, ctx, ctx.results)
        self.assertEqual(_statuses(ctx.results), ["pass"], ctx.results.results[0].detail)

    def test_crash_during_churn_fails(self):
        state = {}
        client = _mk_client(state)
        dev = _device()
        ctx = _ctx([dev], _ok_guard(), FakeSettle(ready=True))

        def crash_drive(ip, port, conns, **kw):
            state["uptime"] = 1_000  # uptime regression -> crash signal
            return 30

        with patch("suites.faults.socket.Client", return_value=client), \
             patch("suites.faults.socket._drive_sockets", side_effect=crash_drive), \
             patch("fleetlib.discovery._read_identity", return_value=("esp32-wroom32", "test-host")):
            socket_scn.run_device(dev, ctx, ctx.results)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "fail")
        self.assertIn("regression", r.detail)

    def test_no_drain_fails(self):
        state = {"sockets": 15}  # stays at/above drain threshold
        client = _mk_client(state)
        dev = _device()
        ctx = _ctx([dev], _ok_guard(), FakeSettle(ready=True))
        with patch("suites.faults.socket.Client", return_value=client), \
             patch("suites.faults.socket._drive_sockets", return_value=40), \
             patch("suites.faults.socket.time.sleep", return_value=None), \
             patch("fleetlib.discovery._read_identity", return_value=("esp32-wroom32", "test-host")):
            socket_scn.run_device(dev, ctx, ctx.results)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "fail")
        self.assertIn("drain", r.detail)

    def test_dry_run_skips_churn(self):
        state = {}
        client = _mk_client(state)
        dev = _device()
        guard = Guard(dry_run=True, confirm=True, expect_board=dev.board)
        ctx = _ctx([dev], guard, FakeSettle(ready=True))
        drive = MagicMock()
        with patch("suites.faults.socket.Client", return_value=client), \
             patch("suites.faults.socket._drive_sockets", drive), \
             patch("fleetlib.discovery._read_identity", return_value=("esp32-wroom32", "test-host")):
            socket_scn.run_device(dev, ctx, ctx.results)
        self.assertEqual(ctx.results.results[0].status, "skip")
        drive.assert_not_called()

    def test_not_ready_skips(self):
        dev = _device()
        ctx = _ctx([dev], _ok_guard(), FakeSettle(ready=False))
        socket_scn.run_device(dev, ctx, ctx.results)
        self.assertEqual(ctx.results.results[0].status, "skip")

    def test_refused_without_confirm_skips(self):
        state = {}
        client = _mk_client(state)
        dev = _device()
        guard = Guard(dry_run=False, confirm=False, expect_board=dev.board)
        ctx = _ctx([dev], guard, FakeSettle(ready=True))
        drive = MagicMock()
        with patch("suites.faults.socket.Client", return_value=client), \
             patch("suites.faults.socket._drive_sockets", drive), \
             patch("fleetlib.discovery._read_identity", return_value=("esp32-wroom32", "test-host")):
            socket_scn.run_device(dev, ctx, ctx.results)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "skip")
        self.assertIn("refused", r.detail)
        drive.assert_not_called()


# --------------------------------------------------------------------------- broker

class FakeDocker:
    def __init__(self, available=True, on_start=None):
        self.available = available
        self.on_start = on_start
        self.calls = []

    def __call__(self, action, container, timeout=30):
        self.calls.append((action, container))
        if action == "inspect":
            return (0 if self.available else 1, "")
        if action == "start" and self.on_start:
            self.on_start()
        return (0, "")

    def actions(self):
        return [a for a, _ in self.calls]


class TestBrokerOutage(unittest.TestCase):
    def test_unavailable_skips(self):
        dev = _device()
        docker = FakeDocker(available=False)
        ctx = _ctx([dev], _ok_guard(), FakeSettle(ready=True),
                   extra={"docker_runner": docker})
        broker_scn.run_device(dev, ctx, ctx.results)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "skip")
        self.assertIn("unavailable", r.detail)

    def test_recovery_pass(self):
        state = {}
        client = _mk_client(state)
        dev = _device()
        docker = FakeDocker(available=True)
        ctx = _ctx([dev], _ok_guard(), FakeSettle(ready=True),
                   extra={"docker_runner": docker, "cycles": 1,
                          "outage_duration": 0, "reconnect_duration": 0})
        with patch("suites.faults.broker_outage.Client", return_value=client), \
             patch("suites.faults.broker_outage.time.sleep", return_value=None), \
             patch("fleetlib.discovery._read_identity", return_value=("esp32-wroom32", "test-host")):
            broker_scn.run_device(dev, ctx, ctx.results)
        self.assertEqual(ctx.results.results[0].status, "pass",
                         ctx.results.results[0].detail)
        self.assertIn("stop", docker.actions())
        self.assertIn("start", docker.actions())

    def test_new_panic_fails(self):
        state = {}
        client = _mk_client(state)

        def poison():
            state["panic"] = {"task": "mqtt", "panic_reason": "LoadProhibited", "count": 1}

        dev = _device()
        docker = FakeDocker(available=True, on_start=poison)
        ctx = _ctx([dev], _ok_guard(), FakeSettle(ready=True),
                   extra={"docker_runner": docker, "cycles": 1,
                          "outage_duration": 0, "reconnect_duration": 0})
        with patch("suites.faults.broker_outage.Client", return_value=client), \
             patch("suites.faults.broker_outage.time.sleep", return_value=None), \
             patch("fleetlib.discovery._read_identity", return_value=("esp32-wroom32", "test-host")):
            broker_scn.run_device(dev, ctx, ctx.results)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "fail")
        self.assertIn("panic", r.detail)

    def test_dry_run_skips_outage(self):
        state = {}
        client = _mk_client(state)
        dev = _device()
        docker = FakeDocker(available=True)
        guard = Guard(dry_run=True, confirm=True, expect_board=dev.board)
        ctx = _ctx([dev], guard, FakeSettle(ready=True),
                   extra={"docker_runner": docker, "cycles": 1,
                          "outage_duration": 0, "reconnect_duration": 0})
        with patch("suites.faults.broker_outage.Client", return_value=client), \
             patch("suites.faults.broker_outage.time.sleep", return_value=None), \
             patch("fleetlib.discovery._read_identity", return_value=("esp32-wroom32", "test-host")):
            broker_scn.run_device(dev, ctx, ctx.results)
        self.assertEqual(ctx.results.results[0].status, "skip")
        self.assertNotIn("stop", docker.actions())


# --------------------------------------------------------------------------- vcore

_ASIC = "bitaxe-403"
_SPEC_WITH_HOOK = {"paths": {"/api/diag/vcore-drop": {"post": {}}}}


class TestVcoreDrop(unittest.TestCase):
    def test_non_asic_refused(self):
        dev = _device(board="esp32-wroom32")
        ctx = _ctx([dev], _ok_guard(), FakeSettle(ready=True))
        vcore_scn.run_device(dev, ctx, ctx.results)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "skip")
        self.assertIn("non-ASIC", r.detail)

    def test_hook_absent_skips(self):
        state = {"board": _ASIC, "spec": {"paths": {}}}
        client = _mk_client(state)
        dev = _device(board=_ASIC)
        ctx = _ctx([dev], _ok_guard(board=_ASIC), FakeSettle(ready=True))
        with patch("suites.faults.vcore_drop.Client", return_value=client), \
             patch("fleetlib.discovery._read_identity", return_value=(_ASIC, "test-host")):
            vcore_scn.run_device(dev, ctx, ctx.results)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "skip")
        self.assertIn("absent", r.detail)

    def test_recovery_pass_calls_hook(self):
        state = {"board": _ASIC, "spec": _SPEC_WITH_HOOK}
        client = _mk_client(state)
        dev = _device(board=_ASIC)
        ctx = _ctx([dev], _ok_guard(board=_ASIC), FakeSettle(ready=True))
        with patch("suites.faults.vcore_drop.Client", return_value=client), \
             patch("fleetlib.discovery._read_identity", return_value=(_ASIC, "test-host")):
            vcore_scn.run_device(dev, ctx, ctx.results)
        self.assertEqual(ctx.results.results[0].status, "pass",
                         ctx.results.results[0].detail)
        methods = [call.args[0] for call in client.request.call_args_list]
        paths = [call.args[1] for call in client.request.call_args_list]
        self.assertIn("POST", methods)
        self.assertIn(vcore_scn.HOOK_PATH, paths)

    def test_dry_run_skips_hook(self):
        state = {"board": _ASIC, "spec": _SPEC_WITH_HOOK}
        client = _mk_client(state)
        dev = _device(board=_ASIC)
        guard = Guard(dry_run=True, confirm=True, expect_board=_ASIC)
        ctx = _ctx([dev], guard, FakeSettle(ready=True))
        with patch("suites.faults.vcore_drop.Client", return_value=client), \
             patch("fleetlib.discovery._read_identity", return_value=(_ASIC, "test-host")):
            vcore_scn.run_device(dev, ctx, ctx.results)
        self.assertEqual(ctx.results.results[0].status, "skip")
        client.request.assert_not_called()

    def test_identity_mismatch_refused(self):
        state = {"board": _ASIC, "spec": _SPEC_WITH_HOOK}
        client = _mk_client(state)
        dev = _device(board=_ASIC)
        guard = Guard(dry_run=False, confirm=True, expect_board="wrong-board")
        ctx = _ctx([dev], guard, FakeSettle(ready=True))
        with patch("suites.faults.vcore_drop.Client", return_value=client), \
             patch("fleetlib.discovery._read_identity", return_value=(None, None)):
            vcore_scn.run_device(dev, ctx, ctx.results)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "skip")
        self.assertIn("refused", r.detail)
        client.request.assert_not_called()


# --------------------------------------------------------------------------- dispatch + guard_step

class TestGuardStep(unittest.TestCase):
    def test_proceed(self):
        dev = _device()
        ctx = _ctx([dev], Guard(dry_run=False, confirm=True, expect_board=dev.board),
                   FakeSettle())
        with patch("fleetlib.discovery._read_identity", return_value=(dev.board, "test-host")):
            outcome, _ = guard_step(ctx, dev, "POST", "/x")
        self.assertEqual(outcome, PROCEED)

    def test_dryrun(self):
        dev = _device()
        ctx = _ctx([dev], Guard(dry_run=True, confirm=True, expect_board=dev.board),
                   FakeSettle())
        with patch("fleetlib.discovery._read_identity", return_value=(dev.board, "test-host")):
            outcome, _ = guard_step(ctx, dev, "POST", "/x")
        self.assertEqual(outcome, DRYRUN)

    def test_refused(self):
        dev = _device()
        ctx = _ctx([dev], Guard(dry_run=False, confirm=True, expect_board="wrong"),
                   FakeSettle())
        with patch("fleetlib.discovery._read_identity", return_value=(None, None)):
            outcome, _ = guard_step(ctx, dev, "POST", "/x")
        self.assertEqual(outcome, REFUSED)


class TestDispatch(unittest.TestCase):
    def test_all_scenarios_default(self):
        ctx = _ctx([_device()], _ok_guard(), FakeSettle(), extra={})
        active = [n for n, _ in faults._active_scenarios(ctx)]
        self.assertEqual(set(active), {"socket", "broker-outage", "vcore-drop"})

    def test_subset_selection(self):
        ctx = _ctx([_device()], _ok_guard(), FakeSettle(), extra={"scenarios": ["socket"]})
        active = [n for n, _ in faults._active_scenarios(ctx)]
        self.assertEqual(active, ["socket"])

    def test_gate_filters(self):
        ctx = _ctx([_device()], _ok_guard(), FakeSettle(), gates={"socket"}, extra={})
        active = [n for n, _ in faults._active_scenarios(ctx)]
        self.assertEqual(active, ["socket"])

    def test_run_dispatches_per_device(self):
        devs = [_device(ip="192.0.2.1"), _device(ip="192.0.2.2")]
        ctx = _ctx(devs, _ok_guard(), FakeSettle(), extra={"scenarios": ["socket"]})
        with patch.object(socket_scn, "run_device") as rd:
            faults.run(ctx)
        self.assertEqual(rd.call_count, 2)


if __name__ == "__main__":
    unittest.main()
