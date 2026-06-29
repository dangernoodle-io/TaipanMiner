"""OTA operations: push, pull, mark-valid, recover, status, verify.

TA-433 Phase 3 — folds reflash_fleet.py / fleetpull.py / otaflash.py /
migrate_board.py / pulltest.py / validate70.py OTA logic into one library.

Contract (the CLI `ota` subcommand dispatches to these by name — signatures are
FIXED):
    push(client, guard, binfile, target_version=None, settle=None,
         elf_path=None, do_mark_valid=False)
    pull(client, guard, mode='auto', target_version=None, settle=None)
    mark_valid(client, guard)
    recover(client, guard)
    status(client)                       # READ-ONLY
    verify(client, profile, criteria, target_version, settle)
    wait_for_boot(client, target_version=None, timeout=240)

All mutating ops route through safety.Guard:
  - --dry-run  -> guard returns its sentinel, NO HTTP mutation happens
  - identity mismatch -> guard raises IdentityMismatch (refuse)
  - no confirm -> guard raises RefusedWithoutConfirmation

Reads use client.get_json; mutations use client.request. Timeout constants come
from client.py — no magic HTTP timeouts here.
"""
from __future__ import annotations
import dataclasses
import json
import logging
import time
from dataclasses import dataclass, field
from typing import Any, Optional, Tuple, TYPE_CHECKING

from .client import (
    TIMEOUT_INFO,
    TIMEOUT_HEALTH,
    TIMEOUT_WRITE,
    TIMEOUT_OTA_PUSH,
    TIMEOUT_UPDATE_CHECK,
    info_field,
)
from .safety import Guard
from .readiness import wait_until_ready

if TYPE_CHECKING:
    from .criteria import Criteria
    from .profiles import Profile

logger = logging.getLogger(__name__)

# Poll cadence (seconds). Module-level so tests can stub time.sleep cheaply.
_BOOT_POLL_INTERVAL = 5
_BOOT_TIMEOUT = 240
_PROGRESS_POLL_INTERVAL = 3
_PROGRESS_TIMEOUT = 300

# Post-OTA readiness grace: how long to wait after the device comes back up for
# mining to spin up and heap to stabilize.  Applied unconditionally in push/pull
# verify — the device always needs this window after an OTA reboot regardless of
# whether --settle was passed.
_POST_OTA_READINESS_TIMEOUT = 120   # seconds: max wait for mining/heap to stabilise
_POST_OTA_SETTLE_GRACE = 0          # settle_delay floor for post-OTA readiness
                                    # (0 = no artificial delay, just wait until ready)

# Terminal states reported by GET /api/update/progress for pull-mode (202) OTA.
_PROGRESS_SUCCESS = {"done", "success", "complete", "completed", "applied", "valid", "ready"}
_PROGRESS_FAILURE = {"error", "failed", "fail", "aborted", "abort", "cancelled", "canceled"}

# Reset reasons that mean the device came back up unhealthy. Mirrors
# Criteria.bad_reset_reasons; used for the lightweight verify in push/pull where
# no Criteria is supplied.
_BAD_RESET_REASONS = {"panic", "task_wdt", "int_wdt", "brownout"}

# OTA reboot always produces reset_reason='software' — this is EXPECTED, not a fault.
_OTA_EXPECTED_RESET_REASON = "software"


@dataclass
class VerifyResult:
    """Structured outcome of an OTA flow / verification.

    ok        — overall success (booted to target + healthy, or a benign no-op)
    pending   — push succeeded and mining is healthy, but firmware has not yet
                self-validated (validated:false).  ok=True, pending=True means
                "PUSH OK — pending validation".  pending=False is the normal case
                (either already validated, or validation is not tracked).
    dry_run   — guard short-circuited; no mutation was performed
    version   — version the device reports now
    healthy   — mining healthy (hashrate > 0, no abnormal reset_reason)
    ready     — readiness gate (settle) was satisfied
    metrics   — hashrate / reset_reason / settle timing for result aggregation
    """
    ok: bool
    detail: str = ""
    version: Optional[str] = None
    target_version: Optional[str] = None
    healthy: bool = False
    ready: bool = False
    pending: bool = False
    dry_run: bool = False
    metrics: dict = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Mutating flows
# ---------------------------------------------------------------------------

def push(
    client,
    guard: "Guard",
    binfile: str,
    target_version: Optional[str] = None,
    settle: Optional[float] = None,
    elf_path: Optional[str] = None,
    do_mark_valid: bool = False,
    criteria: Optional["Criteria"] = None,
    profile: Optional["Profile"] = None,
) -> VerifyResult:
    """OTA-push a local firmware binary (boot-mode: device reboots to apply).

    POST /api/update/push (application/octet-stream, 180s) via guard, then
    wait_for_boot to target, post-OTA readiness grace, verify.

    elf_path — optional explicit .elf path.  When omitted, the function looks
    for a sibling .elf file next to binfile (i.e. .pio/build/<env>/firmware.elf
    alongside firmware.bin).  If found, the ELF is archived before the push so
    that fleet decode can later resolve any panic from this build.

    do_mark_valid — when True, POST /api/update/mark-valid after readiness
    confirms healthy mining, then verify validated:true.  Default is False:
    let the firmware self-validate (first accepted share / 15-min timer).
    """
    g = guard.check(client, "POST", "/api/update/push")
    if Guard.is_dry_run_skip(g):
        return VerifyResult(ok=True, dry_run=True, target_version=target_version,
                            detail="dry-run: would push")

    # --- ELF archival (before the push, non-fatal on failure) ---
    resolved_elf = _resolve_sibling_elf(binfile, elf_path)
    if resolved_elf:
        _archive_elf_for_push(resolved_elf, board=getattr(client, "board", ""),
                              version=target_version or "")
    else:
        logger.info("push: no sibling .elf found for %s; skipping ELF archive", binfile)

    data = _read_binary(binfile)
    logger.info("push %s -> %s (%d bytes)", binfile, client.ip, len(data))
    sc, _ = client.request("POST", "/api/update/push", body=data, timeout=TIMEOUT_OTA_PUSH)
    # sc is None when the connection resets mid-response — the device rebooted
    # to apply the image, which is the expected boot-mode behaviour.
    if sc not in (200, 202, None):
        return VerifyResult(ok=False, target_version=target_version,
                            detail=f"push rejected HTTP {sc}")

    booted = wait_for_boot(client, target_version=target_version)
    if booted is None:
        return VerifyResult(ok=False, target_version=target_version,
                            detail="device did not come back up after push")

    return _post_boot_verify(client, target_version or booted, settle,
                             do_mark_valid=do_mark_valid, guard=guard,
                             criteria=criteria, profile=profile)


def pull(
    client,
    guard: "Guard",
    mode: str = "auto",
    target_version: Optional[str] = None,
    settle: Optional[float] = None,
) -> VerifyResult:
    """OTA-pull: check the manifest, and if an update is available, apply it.

    Mode is auto-detected from the apply response:
      200 -> boot-mode (device reboots -> wait_for_boot)
      202 -> pull-mode (download in progress -> poll /api/update/progress to a
             terminal state, then wait_for_boot)
      409 -> busy (reported, not applied)
    Then settle + verify. `mode` may be forced to 'boot'/'pull'; 409 is always busy.
    """
    g = guard.check(client, "POST", "/api/update/check")
    if Guard.is_dry_run_skip(g):
        return VerifyResult(ok=True, dry_run=True, target_version=target_version,
                            detail="dry-run: would pull")

    sc, body = client.request("POST", "/api/update/check", timeout=TIMEOUT_UPDATE_CHECK)
    chk = _parse_json(body)
    available = bool(chk.get("available"))
    latest = chk.get("latest") or chk.get("target") or chk.get("version")
    target = target_version or latest

    if not available:
        cur = info_field(_get(client, "/api/info", TIMEOUT_INFO) or {}, "version")
        logger.info("%s: no update available (running %s)", client.ip, cur)
        return VerifyResult(ok=True, version=cur, target_version=target,
                            detail="no update available")

    # Re-gate the actual firmware change through the guard (identity re-verify).
    guard.check(client, "POST", "/api/update/apply")
    sc, _ = client.request("POST", "/api/update/apply", timeout=TIMEOUT_WRITE)

    if sc == 409:
        return VerifyResult(ok=False, target_version=target, detail="apply busy (HTTP 409)")

    detected = mode
    if mode == "auto":
        if sc == 200:
            detected = "boot"
        elif sc == 202:
            detected = "pull"
        else:
            return VerifyResult(ok=False, target_version=target,
                                detail=f"apply rejected HTTP {sc}")
    elif sc not in (200, 202):
        return VerifyResult(ok=False, target_version=target,
                            detail=f"apply rejected HTTP {sc}")

    logger.info("%s: apply -> %s-mode (HTTP %s), target=%s", client.ip, detected, sc, target)

    if detected == "pull":
        ok, state = _poll_progress(client)
        if not ok:
            return VerifyResult(ok=False, target_version=target,
                                detail=f"pull-mode download did not complete (state={state})")

    booted = wait_for_boot(client, target_version=target)
    if booted is None:
        return VerifyResult(ok=False, target_version=target,
                            detail="device did not come back up after apply")

    return _post_boot_verify(client, target or booted, settle)


def mark_valid(client, guard: "Guard") -> bool:
    """Mark the running OTA image valid (cancels the rollback timer)."""
    g = guard.check(client, "POST", "/api/update/mark-valid")
    if Guard.is_dry_run_skip(g):
        return True
    sc, _ = client.request("POST", "/api/update/mark-valid", timeout=TIMEOUT_WRITE)
    return sc in (200, 202, 204)


def recover(client, guard: "Guard") -> bool:
    """Roll back to the previous OTA image via POST /api/update/recover."""
    g = guard.check(client, "POST", "/api/update/recover")
    if Guard.is_dry_run_skip(g):
        return True
    sc, _ = client.request("POST", "/api/update/recover", timeout=TIMEOUT_WRITE)
    return sc in (200, 202, 204)


def reboot(
    client,
    guard: "Guard",
    settle: Optional[float] = None,
    criteria: Optional["Criteria"] = None,
    profile: Optional["Profile"] = None,
) -> VerifyResult:
    """Reboot the device via POST /api/reboot (bodyless), safety-gated.

    POST /api/reboot via guard.  sc=None (connection reset mid-response) is the
    expected success case — the device rebooted before it could close the HTTP
    connection.  sc in (200, 202, 204, None) is treated as success.

    When settle is given: wait_for_boot then wait_until_ready (with settle_delay
    set to the given value) to confirm the device recovered.
    """
    g = guard.check(client, "POST", "/api/reboot")
    if Guard.is_dry_run_skip(g):
        return VerifyResult(ok=True, dry_run=True, detail="dry-run: would reboot")

    sc, _ = client.request("POST", "/api/reboot", timeout=TIMEOUT_WRITE)
    # sc=None: connection reset because device rebooted — expected success.
    if sc not in (200, 202, 204, None):
        return VerifyResult(ok=False, detail=f"reboot rejected HTTP {sc}")

    if settle is None:
        return VerifyResult(ok=True, detail="reboot issued")

    # Settle path: wait for device to come back up, then wait_until_ready.
    booted = wait_for_boot(client)
    if booted is None:
        return VerifyResult(ok=False, detail="device did not come back up after reboot")

    from .criteria import Criteria as _Criteria, for_profile as _for_profile
    if criteria is None:
        settle_criteria = _Criteria(
            settle_delay=int(settle),
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        _profile = None
    else:
        eff = _for_profile(criteria, profile) if profile is not None else criteria
        import dataclasses as _dc
        settle_criteria = _dc.replace(eff, settle_delay=int(settle))
        _profile = profile

    readiness = wait_until_ready(client, _profile, settle_criteria)
    ok = readiness.ready
    detail = "ready" if ok else f"not ready: {readiness.reason}"
    return VerifyResult(ok=ok, ready=readiness.ready, detail=detail)


# ---------------------------------------------------------------------------
# Read-only
# ---------------------------------------------------------------------------

def status(client) -> dict:
    """READ-ONLY merged view of GET /api/update/status + /api/update/progress."""
    st = _get(client, "/api/update/status", TIMEOUT_UPDATE_CHECK) or {}
    pr = _get(client, "/api/update/progress", TIMEOUT_INFO) or {}
    merged = dict(st)
    merged["progress"] = pr
    return merged


# ---------------------------------------------------------------------------
# Boot + verify
# ---------------------------------------------------------------------------

def wait_for_boot(
    client,
    target_version: Optional[str] = None,
    timeout: float = _BOOT_TIMEOUT,
) -> Optional[str]:
    """Poll /api/health + /api/info until the device is back up.

    Returns the running version once both endpoints respond (and version ==
    target_version when given), or None on timeout. Connection-refused during
    the reboot window is tolerated (treated as not-up-yet).
    """
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        health = _get(client, "/api/health", TIMEOUT_HEALTH)
        info = _get(client, "/api/info", TIMEOUT_INFO)
        if health is not None and info is not None:
            v = info_field(info, "version")
            if target_version is None or v == target_version:
                logger.debug("%s: back up on %s", client.ip, v)
                return v
        time.sleep(_BOOT_POLL_INTERVAL)
    logger.error("%s: timed out waiting for boot (target=%s)", client.ip, target_version)
    return None


def verify(
    client,
    profile: Optional["Profile"],
    criteria: "Criteria",
    target_version: Optional[str],
    settle: Optional[float],
) -> VerifyResult:
    """Settle-then-assert verification.

    Runs wait_until_ready first (settle), then asserts the running version ==
    target_version AND mining is healthy (hashrate > 0, no abnormal
    reset_reason). `settle`, when given, overrides criteria.settle_delay.
    """
    eff = criteria if settle is None else dataclasses.replace(criteria, settle_delay=settle)
    readiness = wait_until_ready(client, profile, eff)

    info = _get(client, "/api/info", TIMEOUT_INFO) or {}
    v = info_field(info, "version")
    healthy, metrics = _assess_health(client, info, criteria)
    metrics["settle_elapsed_s"] = readiness.elapsed_s

    version_ok = target_version is None or v == target_version
    ok = readiness.ready and version_ok and healthy
    detail = "ok" if ok else _fail_detail(readiness.ready, readiness.reason,
                                          v, target_version, version_ok, healthy, metrics)
    return VerifyResult(ok=ok, version=v, target_version=target_version,
                        healthy=healthy, ready=readiness.ready, metrics=metrics, detail=detail)


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _post_boot_verify(
    client,
    target_version: Optional[str],
    settle: Optional[float],
    do_mark_valid: bool = False,
    guard: Optional["Guard"] = None,
    criteria: Optional["Criteria"] = None,
    profile: Optional["Profile"] = None,
) -> VerifyResult:
    """Post-OTA verify for push/pull.

    Always waits for mining to spin up (wait_until_ready with a real timeout),
    regardless of whether --settle was passed.  The OTA reboot always needs this
    grace period for hashrate to come up and heap to stabilise.

    After readiness:
    - Checks version, hashrate, and reset_reason for real failures.
    - Distinguishes PENDING-VALIDATION (healthy mining + validated:false) from
      actual failure.  A pending result reports ok=True, pending=True.
    - reset_reason='software' after OTA reboot is expected — not a failure.
    - If do_mark_valid=True, POSTs /api/update/mark-valid and confirms
      validated:true; reports ok=True, pending=False (VALIDATED).

    Failures (ok=False):
    - Device unreachable / never came back (caught upstream in push/pull)
    - reset_reason is an abnormal fault (panic / brownout / wdt)
    - Mining never resumed within the readiness window
    - Version mismatch (wrong image applied)
    """
    from .criteria import Criteria, for_profile

    # Build the criteria for wait_until_ready.  settle_delay is always forced
    # to _POST_OTA_SETTLE_GRACE (0) so there is no artificial floor — just wait
    # until the board is ready.  The extra --settle value is applied AFTER
    # readiness, below.
    #
    # When criteria is None: use the hardcoded backward-compatible defaults
    # (readiness_heap_floor=50_000, etc.).
    # When criteria is provided: derive from it (applying per-board profile
    # overrides when profile is not None), then pin settle_delay to
    # _POST_OTA_SETTLE_GRACE so the OTA readiness gate never adds an artificial
    # delay on top of the post-OTA reboot window.
    if criteria is None:
        post_ota_criteria = Criteria(
            settle_delay=_POST_OTA_SETTLE_GRACE,
            readiness_heap_floor=50_000,
            readiness_hashrate_min=0.0,
            readiness_vcore_floor=0,
        )
        _readiness_profile = None
    else:
        effective = for_profile(criteria, profile) if profile is not None else criteria
        post_ota_criteria = dataclasses.replace(effective, settle_delay=_POST_OTA_SETTLE_GRACE)
        _readiness_profile = profile

    readiness = wait_until_ready(
        client, _readiness_profile, post_ota_criteria,
        timeout=_POST_OTA_READINESS_TIMEOUT,
    )

    # Additional explicit settle on top of readiness (from --settle flag).
    if settle:
        time.sleep(settle)

    info = _get(client, "/api/info", TIMEOUT_INFO) or {}
    v = info_field(info, "version")
    reset_reason = info.get("reset_reason")

    # reset_reason='software' is the expected OTA reboot reason — not a fault.
    # Only abnormal reasons (panic/brownout/wdt) indicate a real problem.
    bad = _BAD_RESET_REASONS
    reset_ok = reset_reason is None or reset_reason not in bad

    stats = _get(client, "/api/stats", TIMEOUT_INFO) or {}
    hashrate = stats.get("hashrate_ghs") or stats.get("hashrate") or info.get("hashrate") or 0.0
    hashrate = float(hashrate)

    metrics = {"hashrate": hashrate, "reset_reason": reset_reason}
    metrics["settle_elapsed_s"] = readiness.elapsed_s

    # Version check
    version_ok = target_version is None or v == target_version

    # Health: mining must be up (hashrate>0) and no abnormal reset
    healthy = hashrate > 0 and reset_ok

    if not version_ok:
        return VerifyResult(
            ok=False, version=v, target_version=target_version,
            healthy=healthy, ready=readiness.ready, metrics=metrics,
            detail=_fail_detail(readiness.ready, readiness.reason,
                                v, target_version, version_ok, healthy, metrics),
        )

    if not reset_ok:
        return VerifyResult(
            ok=False, version=v, target_version=target_version,
            healthy=False, ready=readiness.ready, metrics=metrics,
            detail=f"abnormal reset_reason={reset_reason!r} after OTA reboot",
        )

    if not readiness.ready or hashrate <= 0:
        # Mining never resumed within the window — real failure
        return VerifyResult(
            ok=False, version=v, target_version=target_version,
            healthy=False, ready=readiness.ready, metrics=metrics,
            detail=_fail_detail(readiness.ready, readiness.reason,
                                v, target_version, version_ok, healthy, metrics),
        )

    # Mining is healthy.  Now check validation state.
    validated = _check_validated(client)

    if do_mark_valid:
        # Force-validate: POST mark-valid then confirm validated:true.
        if guard is not None:
            mv_ok = mark_valid(client, guard)
        else:
            sc, _ = client.request("POST", "/api/update/mark-valid", timeout=TIMEOUT_WRITE)
            mv_ok = sc in (200, 202, 204)
        if mv_ok:
            validated = _check_validated(client)
            if validated:
                return VerifyResult(
                    ok=True, version=v, target_version=target_version,
                    healthy=True, ready=True, pending=False, metrics=metrics,
                    detail="VALIDATED (mark-valid confirmed)",
                )
            else:
                return VerifyResult(
                    ok=False, version=v, target_version=target_version,
                    healthy=True, ready=True, pending=True, metrics=metrics,
                    detail="mark-valid posted but validated:true not confirmed",
                )
        else:
            return VerifyResult(
                ok=False, version=v, target_version=target_version,
                healthy=True, ready=True, pending=True, metrics=metrics,
                detail="mark-valid POST failed",
            )

    if not validated:
        # Mining healthy but not yet validated — PENDING (firmware self-validates).
        return VerifyResult(
            ok=True, version=v, target_version=target_version,
            healthy=True, ready=True, pending=True, metrics=metrics,
            detail="PUSH OK — pending validation (firmware self-validates on first share / timer)",
        )

    return VerifyResult(
        ok=True, version=v, target_version=target_version,
        healthy=True, ready=True, pending=False, metrics=metrics,
        detail="ok",
    )


def _check_validated(client) -> bool:
    """Return True when /api/health reports validated:true (or field absent, assume ok)."""
    health = _get(client, "/api/health", TIMEOUT_HEALTH)
    if health is None:
        return False
    # validated field: absent means the endpoint doesn't track it (older fw) — treat as ok
    if "validated" not in health:
        return True
    return bool(health.get("validated"))


def _basic_verify(client, target_version: Optional[str], settle: Optional[float]) -> VerifyResult:
    """Lightweight post-boot verify (no profile/criteria available).

    Legacy helper retained for callers that do NOT go through push/pull (e.g.
    tests that call it directly).  Post-OTA paths use _post_boot_verify instead.
    """
    if settle:
        time.sleep(settle)
    info = _get(client, "/api/info", TIMEOUT_INFO) or {}
    v = info_field(info, "version")
    healthy, metrics = _assess_health(client, info, None)
    version_ok = target_version is None or v == target_version
    ok = version_ok and healthy
    detail = "ok" if ok else _fail_detail(True, "ready", v, target_version,
                                          version_ok, healthy, metrics)
    return VerifyResult(ok=ok, version=v, target_version=target_version,
                        healthy=healthy, ready=True, metrics=metrics, detail=detail)


def _assess_health(client, info: dict, criteria: Optional["Criteria"]) -> Tuple[bool, dict]:
    """Mining health: hashrate > 0 and reset_reason not abnormal."""
    stats = _get(client, "/api/stats", TIMEOUT_INFO) or {}
    hashrate = stats.get("hashrate_ghs")
    if hashrate is None:
        hashrate = stats.get("hashrate")
    if hashrate is None:
        hashrate = info.get("hashrate")
    hashrate = hashrate or 0.0

    reset_reason = info.get("reset_reason")
    bad = criteria.bad_reset_reasons if criteria is not None else _BAD_RESET_REASONS
    reset_ok = reset_reason is None or reset_reason not in bad

    healthy = hashrate > 0 and reset_ok
    metrics = {"hashrate": hashrate, "reset_reason": reset_reason}
    return healthy, metrics


def _poll_progress(client, timeout: float = _PROGRESS_TIMEOUT) -> Tuple[bool, str]:
    """Poll GET /api/update/progress until a terminal state. (success, state)."""
    t0 = time.monotonic()
    last = "unknown"
    while time.monotonic() - t0 < timeout:
        pr = _get(client, "/api/update/progress", TIMEOUT_INFO) or {}
        state = str(pr.get("state") or pr.get("status") or "").lower()
        if state:
            last = state
        if state in _PROGRESS_SUCCESS:
            return True, state
        if state in _PROGRESS_FAILURE:
            return False, state
        time.sleep(_PROGRESS_POLL_INTERVAL)
    return False, last


def _fail_detail(ready, reason, v, target, version_ok, healthy, metrics) -> str:
    parts = []
    if not ready:
        parts.append(f"not ready ({reason})")
    if not version_ok:
        parts.append(f"version {v!r} != target {target!r}")
    if not healthy:
        parts.append(
            f"unhealthy (hashrate={metrics.get('hashrate')}, "
            f"reset_reason={metrics.get('reset_reason')!r})"
        )
    return "; ".join(parts) or "verification failed"


def _read_binary(binfile: str) -> bytes:
    with open(binfile, "rb") as f:
        return f.read()


def _resolve_sibling_elf(binfile: str, explicit_elf: Optional[str]) -> Optional[str]:
    """Return an ELF path to archive, or None if not found.

    Priority:
      1. explicit_elf if given
      2. <binfile_stem>.elf in the same directory (e.g. firmware.elf beside firmware.bin)
    """
    if explicit_elf:
        import os as _os
        return explicit_elf if _os.path.isfile(explicit_elf) else None
    import os as _os
    stem = _os.path.splitext(binfile)[0]
    candidate = stem + ".elf"
    return candidate if _os.path.isfile(candidate) else None


def _archive_elf_for_push(elf_path: str, board: str = "", version: str = "") -> Optional[str]:
    """Archive the ELF before a push.  Non-fatal — logs a warning on any error."""
    try:
        from .elfstore import archive as _archive
        key = _archive(elf_path, board=board, version=version)
        logger.info("push: archived ELF %s (sha=%s)", elf_path, key[:16])
        return key
    except Exception as exc:
        logger.warning("push: ELF archive failed (%s): %s", elf_path, exc)
        return None


def _parse_json(body: Any) -> dict:
    """Parse a response body (bytes/str) to a dict; {} on anything unexpected."""
    if body is None:
        return {}
    if isinstance(body, dict):
        return body
    try:
        if isinstance(body, (bytes, bytearray)):
            body = body.decode()
        obj = json.loads(body)
        return obj if isinstance(obj, dict) else {}
    except Exception:
        return {}


def _get(client, path: str, timeout: float) -> Optional[dict]:
    """GET path tolerating any transport error (connection-refused -> None)."""
    try:
        return client.get_json(path, timeout=timeout)
    except Exception:
        return None
