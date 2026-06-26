"""fleetlib — shared harness library for TaipanMiner fleet tests (TA-433)."""

from .client import (
    Client,
    get_field,
    TIMEOUT_INFO,
    TIMEOUT_HEALTH,
    TIMEOUT_TELEMETRY,
    TIMEOUT_WRITE,
    TIMEOUT_OTA_PUSH,
    TIMEOUT_UPDATE_CHECK,
)
from .spec import Spec
from .discovery import Device, discover, from_hosts
from .profiles import Profile, Profiles, profile_for
from .criteria import Criteria, load as load_criteria, for_profile as criteria_for_profile
from .safety import Guard
from .results import Result, ResultSet, STATUS_PASS, STATUS_FAIL, STATUS_SKIP
from .monitor import (
    poll,
    detectors_from_criteria,
    Anomaly,
    Sample,
)

__all__ = [
    # client
    "Client",
    "get_field",
    "TIMEOUT_INFO",
    "TIMEOUT_HEALTH",
    "TIMEOUT_TELEMETRY",
    "TIMEOUT_WRITE",
    "TIMEOUT_OTA_PUSH",
    "TIMEOUT_UPDATE_CHECK",
    # spec
    "Spec",
    # discovery
    "Device",
    "discover",
    "from_hosts",
    # profiles
    "Profile",
    "Profiles",
    "profile_for",
    # criteria
    "Criteria",
    "load_criteria",
    "criteria_for_profile",
    # safety
    "Guard",
    # results
    "Result",
    "ResultSet",
    "STATUS_PASS",
    "STATUS_FAIL",
    "STATUS_SKIP",
    # monitor
    "poll",
    "detectors_from_criteria",
    "Anomaly",
    "Sample",
]
