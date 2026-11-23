#!/usr/bin/env python3
"""Read-only preflight for a single *owned* Internet Archive item.

Never uploads anything. Does not expose, test, or echo IA-S3 key values.
The presence of keys does NOT establish their validity or upload permission.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

from scripts.archive_backup_batch_preview import item_owned_by_account

IDENTIFIER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$")
METADATA_BASE = "https://archive.org/metadata/"
STATUS_READY = "OWNER_MATCH_PREVIEW"
STATUS_MANUAL = "PENDING_MANUAL"
STATUS_WAITING = "WAITING_ARCHIVE"
STATUS_CONFIG = "CONFIG_REQUIRED"


def approved_item_ids(upload_log: Path) -> set[str]:
    """An own successful upload is a necessary historical hint, not proof."""
    known = set()
    if not upload_log.is_file():
        return known
    with upload_log.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if (row.get("status") or "").strip().casefold() == "ok":
                value = (row.get("identifier") or "").strip()
                if IDENTIFIER_RE.fullmatch(value):
                    known.add(value)
    return known


def fetch_item_metadata(identifier: str, *, opener=urllib.request.urlopen) -> dict:
    if not IDENTIFIER_RE.fullmatch(identifier):
        raise ValueError("Invalid item identifier")
    req = urllib.request.Request(
        METADATA_BASE + identifier,
        headers={"User-Agent": "PSVitaAlive-Archive-Ownership-Preflight/1.0",
                 "Accept": "application/json"})
    with opener(req, timeout=20) as resp:
        if resp.status != 200:
            raise urllib.error.HTTPError(req.full_url, resp.status, "Unexpected HTTP status", None, None)
        payload = resp.read(1_000_001)
    if len(payload) > 1_000_000:
        raise ValueError("Archive.org metadata response too large")
    item = json.loads(payload)
    if not isinstance(item, dict):
        raise ValueError("Malformed Archive.org metadata")
    return item


def evaluate(identifier: str, log_path: Path, environ: dict[str, str],
             *, fetch=fetch_item_metadata) -> dict[str, str | bool]:
    """All outcomes are safe for GitHub logs; do not reveal owner or keys."""
    result: dict[str, str | bool] = {
        "identifier": identifier, "status": STATUS_CONFIG,
        "message": "", "upload_permitted": False,
    }
    if not IDENTIFIER_RE.fullmatch(identifier):
        result.update(status=STATUS_MANUAL, message="Invalid identifier")
        return result
    keys = ("IA_ACCESS_KEY", "IA_SECRET_KEY", "IA_EXPECTED_UPLOADER")
    missing = [k for k in keys if not environ.get(k, "").strip()]
    if missing:
        result.update(status=STATUS_CONFIG, message="Missing GitHub repository secrets: " + ", ".join(missing))
        return result
    if identifier not in approved_item_ids(log_path):
        result.update(status=STATUS_MANUAL, message="Item not present in successful PSVitaAlive upload log")
        return result
    try:
        metadata = fetch(identifier)
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, OSError) as exc:
        if isinstance(exc, urllib.error.HTTPError) and exc.code not in (408, 429, 500, 502, 503, 504):
            result.update(status=STATUS_MANUAL, message=f"Archive.org returned HTTP {exc.code}")
        else:
            result.update(status=STATUS_WAITING, message="Archive.org temporarily unavailable")
        return result
    except (ValueError, json.JSONDecodeError):
        result.update(status=STATUS_WAITING, message="Archive.org metadata unavailable or invalid")
        return result

    owned, reason = item_owned_by_account(metadata, environ["IA_EXPECTED_UPLOADER"])
    if not owned:
        result.update(status=STATUS_MANUAL, message=reason)
        return result
    result.update(status=STATUS_READY,
                  message="Item uploader matches configured account. IA-S3 key validity/write access still untested.")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identifier", default="psvitaalive-z2",
                        help="Existing PSVitaAlive item (default: psvitaalive-z2)")
    parser.add_argument("--upload-log", type=Path,
                        default=Path(__file__).resolve().with_name("archive_upload_log.csv"))
    args = parser.parse_args()
    outcome = evaluate(args.identifier, args.upload_log, os.environ)
    # Output only non-sensitive metadata and status; no emails or tokens.
    print(f"Item: {outcome['identifier']} | State: {outcome['status']}")
    print(outcome["message"])
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write("# Internet Archive ownership preflight (read only)\n\n")
            fh.write(f"- Item: {outcome['identifier']}\n")
            fh.write(f"- State: **{outcome['status']}**\n")
            fh.write(f"- Result: {outcome['message']}\n\n")
            fh.write("No VPK downloaded/uploaded, no catalog edits. IA-S3 write access not tested.\n")
    return 1 if outcome["status"] == STATUS_CONFIG else 0


if __name__ == "__main__":
    sys.exit(main())
