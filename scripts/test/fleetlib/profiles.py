"""Board class detection and capability profiles."""
from __future__ import annotations
import dataclasses
import os
from dataclasses import dataclass
from typing import Optional


@dataclass
class Profile:
    """Capability profile for a board class.

    Criteria overrides (None = use Criteria default):
      poll_interval, heap_floor, vcore_floor_mv, publisher_polls
    """
    board: str
    is_asic: bool = False
    has_psram: bool = False
    max_concurrent: int = 4   # max concurrent HTTP connections during stress
    max_rps: float = 10.0     # max requests/second during stress
    # criteria overrides
    poll_interval: Optional[float] = None   # seconds
    heap_floor: Optional[int] = None        # bytes; None = use Criteria default
    vcore_floor_mv: Optional[int] = None    # mV; ASIC only
    publisher_polls: Optional[int] = None   # None = use Criteria default
    hashrate_floor_pct: Optional[float] = None  # % floor; None = use Criteria default


class Profiles:
    """Named board-class profile overrides loaded from config/profiles.yaml.

    Keys are the board-class prefix strings used by profile_for() (e.g. 'bitaxe',
    'esp32-c3').  Use load() to read the YAML; use get() to look up a class by name.
    """

    def __init__(self, overrides: Optional[dict] = None) -> None:
        self.overrides: dict = overrides or {}

    @classmethod
    def load(cls, path: str = "config/profiles.yaml") -> "Profiles":
        """Load profile overrides from YAML.  Returns empty Profiles if file absent."""
        if not os.path.exists(path):
            return cls()
        try:
            import yaml  # type: ignore[import]
        except ImportError:
            return cls()
        try:
            with open(path) as f:
                data = yaml.safe_load(f) or {}
        except Exception:
            return cls()
        return cls(overrides=data)

    def get(self, board_class: str) -> Optional[Profile]:
        """Return a Profile for the given board_class key, or None if not defined."""
        entry = self.overrides.get(board_class)
        if entry is None:
            return None
        valid = {f.name for f in dataclasses.fields(Profile)}
        kw = {k: v for k, v in entry.items() if k in valid and k != "board"}
        return Profile(board=board_class, **kw)

    def __repr__(self) -> str:
        return f"Profiles({list(self.overrides.keys())})"


_ASIC_PREFIXES = ("bitaxe",)
_NOPSRAM_BOARDS = ("esp32-wroom32", "tdongle-s3")
_NOPSRAM_PREFIXES = ("tdongle", "esp32-wroom")
_C3_PREFIXES = ("esp32-c3",)
_S3_PREFIXES = ("esp32-s3",)
_S2_PREFIXES = ("esp32-s2",)


def profile_for(board: str, profiles: Optional["Profiles"] = None) -> Profile:
    """Detect board class from board string and return a Profile.

    If *profiles* is supplied (a :class:`Profiles` loaded from YAML), it is
    checked first: keys are matched against the board string by prefix so that
    e.g. ``bitaxe`` matches ``bitaxe-403``.  The first matching YAML entry wins;
    the hardcoded class detection below is the fallback.

    Classes:
      bitaxe-*          => ASIC (BM1368/BM1370; has miner/sensors, vcore checks)
      esp32-wroom32 /
      tdongle-s3        => no-PSRAM SW-miner; conservative concurrency/RPS ceilings
      esp32-c3*         => single-core, no-PSRAM, small heap; tightest ceilings
      esp32-s2*         => single-core, no-PSRAM
      esp32-s3*         => dual-core, has PSRAM
    """
    if profiles is not None:
        b_lower = board.lower()
        for key in profiles.overrides:
            if b_lower == key or b_lower.startswith(key):
                override = profiles.get(key)
                if override is not None:
                    return override
    b = board.lower()

    if any(b.startswith(p) for p in _ASIC_PREFIXES):
        return Profile(
            board=board,
            is_asic=True,
            has_psram=False,
            max_concurrent=2,
            max_rps=4.0,
            poll_interval=60.0,
            vcore_floor_mv=500,
        )

    if b in _NOPSRAM_BOARDS or any(b.startswith(p) for p in _NOPSRAM_PREFIXES):
        return Profile(
            board=board,
            is_asic=False,
            has_psram=False,
            max_concurrent=2,
            max_rps=4.0,
        )

    if any(b.startswith(p) for p in _C3_PREFIXES):
        return Profile(
            board=board,
            is_asic=False,
            has_psram=False,
            max_concurrent=1,
            max_rps=2.0,
            heap_floor=30_000,  # C3 has less RAM than S3/wroom
        )

    if any(b.startswith(p) for p in _S3_PREFIXES):
        return Profile(
            board=board,
            is_asic=False,
            has_psram=True,
            max_concurrent=4,
            max_rps=10.0,
        )

    if any(b.startswith(p) for p in _S2_PREFIXES):
        return Profile(
            board=board,
            is_asic=False,
            has_psram=False,
            max_concurrent=2,
            max_rps=4.0,
        )

    # unknown board — return safe defaults
    return Profile(board=board)
