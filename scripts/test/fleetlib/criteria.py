"""Canonical soak criteria — harness defaults, overridable per board profile."""
from __future__ import annotations
import copy
import os
from dataclasses import dataclass, field
from typing import Set, TYPE_CHECKING

if TYPE_CHECKING:
    from .profiles import Profile


@dataclass
class Criteria:
    """Canonical soak pass/fail thresholds.

    All values are self-contained defaults; no external file is required.
    Load a YAML overlay with load(); apply board-class overrides with for_profile().
    """
    poll_interval: float = 60.0          # seconds between samples
    duration: float = 3600.0             # total soak window (seconds)
    heap_floor: int = 50_000             # bytes — heap.internal.free must stay >= this
    heap_leak_check: bool = True         # min_free must not decline over the soak window
    reboot_tolerance_ms: int = 30_000    # uptime regression > this => reboot detected
    bad_reset_reasons: Set[str] = field(
        default_factory=lambda: {"panic", "task_wdt", "int_wdt", "brownout"}
    )
    wdt_resets_flat: bool = True         # wdt_resets count must not increase
    publisher_max_polls: int = 6         # consecutive polls with pub_ok=false before anomaly
    hashrate_floor_pct: float = 80.0     # % of expected_ghs that hashrate must meet
    vcore_floor_mv: int = 500            # mV floor (ASIC only)
    vcore_restart_flat: bool = True      # vcore_restart_count must not increase (ASIC)
    version_check: bool = False          # require running version == target version
    max_missed_polls: int = 4            # consecutive missed polls => downtime anomaly
    # settle / readiness gate
    settle_delay: int = 120             # minimum warmup floor in seconds
    readiness_heap_floor: int = 50_000  # heap_internal.free must reach this before ready
    readiness_hashrate_min: float = 0.0 # minimum hashrate to declare ready (0 = skip)
    readiness_vcore_floor: int = 0      # mV floor for ASIC boards (0 = skip)


def load(path: str = "config/criteria.yaml") -> Criteria:
    """Load criteria from a YAML file, merging over defaults.

    Silently returns defaults when:
    - the file does not exist
    - pyyaml is not installed
    - the file is malformed
    """
    defaults = Criteria()
    if not os.path.exists(path):
        return defaults
    try:
        import yaml  # type: ignore[import]
    except ImportError:
        return defaults
    try:
        with open(path) as f:
            data = yaml.safe_load(f) or {}
    except Exception:
        return defaults

    for k, v in data.items():
        if hasattr(defaults, k):
            if k == "bad_reset_reasons":
                setattr(defaults, k, set(v))
            else:
                setattr(defaults, k, v)
    return defaults


def for_profile(criteria: Criteria, profile: "Profile") -> Criteria:
    """Return a shallow copy of criteria with board-class overrides applied from profile.

    Only Profile fields that are not None override the corresponding Criteria field.
    bad_reset_reasons is copied so mutations don't affect the original.
    """
    c = copy.copy(criteria)
    c.bad_reset_reasons = set(criteria.bad_reset_reasons)  # deep copy the set

    if profile.poll_interval is not None:
        c.poll_interval = profile.poll_interval
    if profile.heap_floor is not None:
        c.heap_floor = profile.heap_floor
    if profile.vcore_floor_mv is not None:
        c.vcore_floor_mv = profile.vcore_floor_mv
    if profile.publisher_polls is not None:
        c.publisher_max_polls = profile.publisher_polls
    if profile.hashrate_floor_pct is not None:
        c.hashrate_floor_pct = profile.hashrate_floor_pct
    return c
