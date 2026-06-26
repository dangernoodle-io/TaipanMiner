"""ELF archive store for TaipanMiner fleet harness (TA-445).

Archive layout
--------------
  <repo-root>/.elf-archive/<full_sha256>.elf   — ELF binary
  <repo-root>/.elf-archive/<full_sha256>.json  — sidecar metadata

Archive key
-----------
  The key is the full 64-hex-char SHA256 of the ELF file bytes:
      key = hashlib.sha256(open(elf_path, "rb").read()).hexdigest()

  This matches what ESP-IDF / esptool embeds in the firmware image as
  esp_app_desc_t.app_elf_sha256 (the first CONFIG_APP_RETRIEVE_LEN_ELF_SHA
  hex chars of the same hash, default 9).

Panic prefix matching
---------------------
  The /api/diag/panic response carries app_sha256 as a *truncated* hex string
  whose length = CONFIG_APP_RETRIEVE_LEN_ELF_SHA (range 8-64, default 9).
  find() resolves the full archive key by prefix match:
      archive_key.startswith(panic_app_sha256)

  9 hex chars = 4.5 bytes of SHA256 — collision probability is negligible for
  any realistic build corpus.
"""
from __future__ import annotations

import hashlib
import json
import logging
import os
import time
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import List, Optional, Tuple

logger = logging.getLogger(__name__)

# Default retention: keep the 20 most-recently archived ELFs.
DEFAULT_KEEP = 20

# Locate the archive root relative to this file:
# scripts/test/fleetlib/ -> scripts/test/ -> scripts/ -> repo-root
_HARNESS_DIR = Path(__file__).parent.parent  # scripts/test/
_REPO_ROOT = _HARNESS_DIR.parent.parent      # repo root
_ARCHIVE_DEFAULT = _REPO_ROOT / ".elf-archive"


@dataclass
class ElfMeta:
    """Sidecar metadata stored alongside each archived ELF."""
    sha256: str          # full 64-hex-char key
    board: str
    version: str
    build_time: str      # ISO-8601 string or empty
    git_sha: str         # short git sha or empty
    dirty: bool
    archived_at: str     # ISO-8601 string (UTC)


def _archive_root(store_dir: Optional[Path] = None) -> Path:
    root = store_dir if store_dir is not None else _ARCHIVE_DEFAULT
    root.mkdir(parents=True, exist_ok=True)
    return root


def sha256_of_elf(elf_path: str) -> str:
    """Compute SHA256 of an ELF file — same value esp_app_desc_t.app_elf_sha256 holds."""
    h = hashlib.sha256()
    with open(elf_path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def archive(
    elf_path: str,
    board: str = "",
    version: str = "",
    build_time: str = "",
    git_sha: str = "",
    dirty: bool = False,
    store_dir: Optional[Path] = None,
    keep: int = DEFAULT_KEEP,
    max_age: Optional[float] = None,
    _archived_at: Optional[str] = None,
) -> str:
    """Copy elf_path into the archive and return its full SHA256 key.

    Prunes the archive after writing (keep + max_age budget).
    Idempotent: if the same SHA already exists, the sidecar is updated.
    """
    root = _archive_root(store_dir)
    key = sha256_of_elf(elf_path)
    dest_elf = root / f"{key}.elf"
    dest_json = root / f"{key}.json"

    # Copy ELF (skip if already there — same content by definition)
    if not dest_elf.exists():
        import shutil
        shutil.copy2(elf_path, dest_elf)
        logger.debug("elfstore: archived %s -> %s", elf_path, dest_elf)
    else:
        logger.debug("elfstore: %s already archived", key[:16])

    # Write / overwrite sidecar
    meta = ElfMeta(
        sha256=key,
        board=board,
        version=version,
        build_time=build_time,
        git_sha=git_sha,
        dirty=dirty,
        archived_at=_archived_at if _archived_at is not None else _utc_now(),
    )
    dest_json.write_text(json.dumps(asdict(meta), indent=2))

    # Prune-on-write
    prune(keep=keep, max_age=max_age, store_dir=store_dir)
    return key


def list_entries(store_dir: Optional[Path] = None) -> List[Tuple[ElfMeta, int]]:
    """Return all archived entries sorted by archived_at ascending.

    Returns list of (meta, elf_size_bytes).
    """
    root = _archive_root(store_dir)
    entries: List[Tuple[ElfMeta, int]] = []
    for jpath in root.glob("*.json"):
        elf_path = jpath.with_suffix(".elf")
        if not elf_path.exists():
            continue
        try:
            meta = _load_meta(jpath)
        except Exception as exc:
            logger.warning("elfstore: bad sidecar %s: %s", jpath, exc)
            continue
        entries.append((meta, elf_path.stat().st_size))
    entries.sort(key=lambda t: t[0].archived_at)
    return entries


def find(
    panic_sha: str,
    store_dir: Optional[Path] = None,
) -> Optional[str]:
    """Find archived ELF path by prefix-matching panic_sha against full keys.

    panic_sha is the truncated app_sha256 from /api/diag/panic
    (CONFIG_APP_RETRIEVE_LEN_ELF_SHA chars, default 9).

    Returns the absolute path string, or None if not found.
    On multiple matches (very unlikely with ≥9 hex chars), returns the most
    recently archived entry.
    """
    panic_sha = panic_sha.lower().strip()
    root = _archive_root(store_dir)
    matches: List[Tuple[str, str]] = []  # (archived_at, path)
    for jpath in root.glob("*.json"):
        key = jpath.stem
        if key.startswith(panic_sha):
            elf_path = jpath.with_suffix(".elf")
            if elf_path.exists():
                try:
                    meta = _load_meta(jpath)
                    matches.append((meta.archived_at, str(elf_path)))
                except Exception:
                    matches.append(("", str(elf_path)))
    if not matches:
        return None
    matches.sort(key=lambda t: t[0], reverse=True)
    if len(matches) > 1:
        logger.warning(
            "elfstore: %d entries match prefix %r; using most-recent %s",
            len(matches), panic_sha, matches[0][1],
        )
    return matches[0][1]


def prune(
    keep: int = DEFAULT_KEEP,
    max_age: Optional[float] = None,
    store_dir: Optional[Path] = None,
    protected_shas: Optional[set] = None,
    dry_run: bool = False,
) -> List[str]:
    """Remove old ELF entries exceeding budget.

    keep        — keep the N most-recently archived entries.
    max_age     — also remove entries older than max_age seconds.
    protected_shas — set of full sha256 keys to never delete (in-use guard).
    dry_run     — return what would be deleted without deleting.

    Both criteria are applied: an entry is deleted when it exceeds either
    the keep count OR is older than max_age (unless protected).

    Returns list of deleted sha256 keys.
    """
    root = _archive_root(store_dir)
    entries = list_entries(store_dir)  # sorted oldest-first
    now = time.time()
    deleted: List[str] = []

    # Determine which entries are candidates for deletion (oldest first)
    # Keep the most-recent `keep` entries
    if keep > 0 and len(entries) > keep:
        candidates_by_count = set(m.sha256 for m, _ in entries[: len(entries) - keep])
    else:
        candidates_by_count = set()

    # Add entries that exceed max_age
    candidates_by_age: set = set()
    if max_age is not None:
        cutoff = now - max_age
        for meta, _ in entries:
            try:
                entry_ts = _parse_ts(meta.archived_at)
            except Exception:
                continue
            if entry_ts < cutoff:
                candidates_by_age.add(meta.sha256)

    to_delete = candidates_by_count | candidates_by_age

    # Never delete protected entries
    if protected_shas:
        to_delete -= protected_shas

    for meta, _ in entries:
        if meta.sha256 not in to_delete:
            continue
        elf_path = root / f"{meta.sha256}.elf"
        json_path = root / f"{meta.sha256}.json"
        if dry_run:
            logger.info("elfstore: [dry-run] would delete %s", meta.sha256[:16])
        else:
            for p in (elf_path, json_path):
                try:
                    p.unlink(missing_ok=True)
                except Exception as exc:
                    logger.warning("elfstore: delete %s: %s", p, exc)
            logger.debug("elfstore: pruned %s (board=%s ver=%s)", meta.sha256[:16],
                         meta.board, meta.version)
        deleted.append(meta.sha256)

    return deleted


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _load_meta(json_path: Path) -> ElfMeta:
    d = json.loads(json_path.read_text())
    return ElfMeta(
        sha256=d.get("sha256", json_path.stem),
        board=d.get("board", ""),
        version=d.get("version", ""),
        build_time=d.get("build_time", ""),
        git_sha=d.get("git_sha", ""),
        dirty=bool(d.get("dirty", False)),
        archived_at=d.get("archived_at", ""),
    )


def _utc_now() -> str:
    """Return current UTC time as ISO-8601 string."""
    import datetime
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _parse_ts(s: str) -> float:
    """Parse ISO-8601 string to POSIX timestamp."""
    import datetime
    s = s.rstrip("Z")
    dt = datetime.datetime.fromisoformat(s)
    return dt.replace(tzinfo=datetime.timezone.utc).timestamp()
